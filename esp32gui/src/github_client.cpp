#include "github_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <time.h>

#include <cstdio>
#include <cstring>

#include "app_log.h"
#include "firmware_image.h"
#include "net_core.h"
#include "panel_ota.h"
#include "panel_update.h"
#include "release_core.h"

#ifndef SS_GITHUB_OWNER
#define SS_GITHUB_OWNER ""
#endif
#ifndef SS_GITHUB_REPO
#define SS_GITHUB_REPO ""
#endif
#ifndef SS_FW_VERSION
#define SS_FW_VERSION "dev"
#endif
#ifndef SS_BUILD_ID
#define SS_BUILD_ID 0
#endif

namespace {

char s_status[96] = "not queried";
char s_release[4096];
net_github_result_t s_result;
bool s_have_release = false;
bool s_fly_update = false;

bool sntp_valid(void) { return time(nullptr) > 1700000000; }

bool fetch(const char *url, size_t max, uint8_t **out, size_t *out_len) {
  WiFiClientSecure client;
  client.useBuiltinCACertBundle();
  client.setTimeout(15);
  HTTPClient http;
  http.setTimeout(15000);
  http.addHeader("Cache-Control", "no-cache");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    std::snprintf(s_status, sizeof(s_status), "error: cannot open URL");
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    std::snprintf(s_status, sizeof(s_status), "error: HTTP %d", code);
    http.end();
    return false;
  }
  const int size = http.getSize();
  if (size <= 0 || (size_t)size > max) {
    std::snprintf(s_status, sizeof(s_status), "error: bad size %d", size);
    http.end();
    return false;
  }
  uint8_t *buf = (uint8_t *)heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM);
  if (buf == nullptr) buf = (uint8_t *)heap_caps_malloc((size_t)size,
                                                         MALLOC_CAP_INTERNAL);
  if (buf == nullptr) {
    std::snprintf(s_status, sizeof(s_status), "error: no memory");
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  size_t off = 0;
  const uint32_t start = millis();
  panel_update_progress(0);
  while (off < (size_t)size && (uint32_t)(millis() - start) < 20000u) {
    const size_t avail = stream->available();
    if (avail == 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    size_t take = (size_t)size - off;
    if (take > avail) take = avail;
    const size_t got = stream->readBytes(buf + off, take);
    if (got == 0) break;
    off += got;
    panel_update_progress((int)((off * 100u) / (size_t)size));
  }
  http.end();
  if (off != (size_t)size) {
    heap_caps_free(buf);
    std::snprintf(s_status, sizeof(s_status), "error: short read %u/%d",
                  (unsigned)off, size);
    return false;
  }
  *out = buf;
  *out_len = off;
  return true;
}

bool fetch_text(const char *url, char *out, size_t cap) {
  uint8_t *data = nullptr;
  size_t len = 0;
  if (!fetch(url, cap - 1, &data, &len)) return false;
  std::memcpy(out, data, len);
  out[len] = '\0';
  heap_caps_free(data);
  return true;
}

bool is_json_asset(const char *asset) {
  const size_t n = std::strlen(asset);
  return n >= 5 && std::strcmp(asset + n - 5, ".json") == 0;
}

bool fetch_manifest(const rc_release_t *rel, const char *asset,
                    const char *target, rc_manifest_t *m) {
  char url[512];
  std::snprintf(url, sizeof(url),
                "https://github.com/%s/%s/releases/download/%s/%s",
                SS_GITHUB_OWNER, SS_GITHUB_REPO, rel->tag, asset);
  if (!net_github_url_ok(url, SS_GITHUB_OWNER, SS_GITHUB_REPO)) {
    std::snprintf(s_status, sizeof(s_status), "error: asset host");
    return false;
  }
  char text[4096];
  if (!fetch_text(url, text, sizeof(text))) return false;
  if (rc_manifest_parse(text, m) != 0) {
    std::snprintf(s_status, sizeof(s_status), "error: bad manifest");
    return false;
  }
  if (std::strcmp(m->target, target) != 0 || std::strcmp(m->kind, "app") != 0) {
    std::snprintf(s_status, sizeof(s_status), "error: wrong target/kind");
    return false;
  }
  return true;
}

bool fetch_bin(const rc_release_t *rel, const rc_manifest_t *m, size_t max,
               uint8_t **data, size_t *len) {
  char url[512];
  std::snprintf(url, sizeof(url),
                "https://github.com/%s/%s/releases/download/%s/%s",
                SS_GITHUB_OWNER, SS_GITHUB_REPO, rel->tag, m->file);
  if (!net_github_url_ok(url, SS_GITHUB_OWNER, SS_GITHUB_REPO)) {
    std::snprintf(s_status, sizeof(s_status), "error: asset host");
    return false;
  }
  return fetch(url, max, data, len);
}

bool fly_needs_update(const rc_release_t *rel) {
  if (rel == nullptr || !rel->fly.present) return false;
  const ss_fw_image_t *local = firmware_image_fly();
  return local == nullptr || rel->fly.build > local->build;
}

void install_release(const rc_release_t *rel) {
  const bool need_fly = fly_needs_update(rel);
  const bool need_panel = rel->panel.present && rel->panel.build > SS_BUILD_ID;
  bool fly_stored = false;
  rc_manifest_t fly_m;
  uint8_t *fly_bin = nullptr;
  size_t fly_len = 0;
  bool fly_ok = false;
  if (need_fly) {
    for (int i = 0; i < rel->fly.asset_count; ++i) {
      if (!is_json_asset(rel->fly.assets[i])) continue;
      if (fetch_manifest(rel, rel->fly.assets[i], RC_TARGET_FLY, &fly_m)) {
        fly_ok = true;
        break;
      }
    }
  }
  if (need_fly) panel_update_stage("node image");
  if (fly_ok &&
      fetch_bin(rel, &fly_m, 2u * 1024u * 1024u, &fly_bin, &fly_len)) {
    char text[80];
    fly_stored = firmware_image_store_fly(&fly_m, fly_bin, fly_len, text,
                                          sizeof(text));
    if (fly_stored)
      SS_LOGI("github", "Fly stored: %s", text);
    else
      SS_LOGW("github", "Fly store failed: %s", text);
    heap_caps_free(fly_bin);
  }

  if (!need_panel) {
    char version[16] = "--";
    if (rel->fly.present) rc_semver_format(&rel->fly.version, version);
    if (need_fly && !fly_stored) {
      std::snprintf(s_status, sizeof(s_status), "node store failed");
    } else if (need_fly) {
      s_fly_update = false;
      panel_update_clear();
      std::snprintf(s_status, sizeof(s_status), "node %s stored", version);
    } else {
      std::snprintf(s_status, sizeof(s_status), "nothing to apply");
    }
    SS_LOGI("github", "%s", s_status);
    return;
  }

  rc_manifest_t panel_m;
  bool panel_ok = false;
  if (rel->panel.present) {
    for (int i = 0; i < rel->panel.asset_count; ++i) {
      if (!is_json_asset(rel->panel.assets[i])) continue;
      if (fetch_manifest(rel, rel->panel.assets[i], RC_TARGET_PANEL,
                         &panel_m)) {
        panel_ok = true;
        break;
      }
    }
  }
  if (!panel_ok) {
    std::snprintf(s_status, sizeof(s_status), "panel asset unavailable");
    return;
  }
  panel_update_begin("panel image");
  uint8_t *panel_bin = nullptr;
  size_t panel_len = 0;
  if (!fetch_bin(rel, &panel_m, 6u * 1024u * 1024u, &panel_bin, &panel_len)) {
    panel_update_error("panel download failed");
    return;
  }
  panel_update_begin_write();
  /* The main loop blanks the backlight after the overlay has been visible;
   * write only then, so a cache stall cannot draw a garbled frame. */
  for (int i = 0; i < 40 && !panel_update_blanked(); ++i)
    vTaskDelay(pdMS_TO_TICKS(100));
  char text[80];
  const bool ok = panel_ota_apply(&panel_m, panel_bin, panel_len, text,
                                  sizeof(text));
  heap_caps_free(panel_bin);
  if (!ok) {
    std::snprintf(s_status, sizeof(s_status), "panel: %s", text);
    SS_LOGE("github", "panel install failed: %s", text);
    panel_update_error(text);
    return;
  }
  panel_update_done("rebooting");
  std::snprintf(s_status, sizeof(s_status), "installed; rebooting");
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();
}

}  // namespace

extern "C" void github_client_check(void) {
  if (SS_GITHUB_OWNER[0] == '\0' || SS_GITHUB_REPO[0] == '\0') {
    std::snprintf(s_status, sizeof(s_status), "error: repo not configured");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    std::snprintf(s_status, sizeof(s_status), "error: WiFi off");
    return;
  }
  char reason[80];
  if (!net_github_enabled(sntp_valid(), reason, sizeof(reason))) {
    std::snprintf(s_status, sizeof(s_status), "error: %s", reason);
    return;
  }
  char url[288];
  /* The timestamp busts the edge cache on releases/latest; the tag-specific
   * asset URL is immutable and does not need it. */
  std::snprintf(url, sizeof(url),
                "https://github.com/%s/%s/releases/latest/download/release.json?ts=%lu",
                SS_GITHUB_OWNER, SS_GITHUB_REPO,
                (unsigned long)time(nullptr));
  std::snprintf(s_status, sizeof(s_status), "checking...");
  if (!fetch_text(url, s_release, sizeof(s_release))) return;

  rc_release_t rel;
  if (rc_release_parse(s_release, &rel) != 0) {
    std::snprintf(s_status, sizeof(s_status), "error: invalid release.json");
    s_have_release = false;
    return;
  }
  const bool panel_ok = net_github_check(s_release, SS_BUILD_ID, &s_result) == 0;
  if (!panel_ok && !rel.fly.present) {
    std::snprintf(s_status, sizeof(s_status), "error: %s", s_result.reason);
    s_have_release = false;
    s_fly_update = false;
    return;
  }
  s_fly_update = fly_needs_update(&rel);
  s_have_release = true;

  if (s_result.update_available) {
    std::snprintf(s_status, sizeof(s_status), "newer %s  you %s",
                  s_result.version, SS_FW_VERSION);
  } else if (s_fly_update) {
    char version[16] = "--";
    if (rel.fly.present) rc_semver_format(&rel.fly.version, version);
    std::snprintf(s_status, sizeof(s_status), "node %s  you %s", version,
                  SS_FW_VERSION);
  } else if (s_result.build == SS_BUILD_ID) {
    std::snprintf(s_status, sizeof(s_status), "same %s", s_result.version);
  } else {
    std::snprintf(s_status, sizeof(s_status), "older %s", s_result.version);
  }
  SS_LOGI("github", "%s", s_status);
}

extern "C" void github_client_install(void) {
  if (!s_have_release || (!s_result.update_available && !s_fly_update)) {
    std::snprintf(s_status, sizeof(s_status), "no update checked");
    return;
  }
  panel_update_clear();
  rc_release_t rel;
  if (rc_release_parse(s_release, &rel) != 0) {
    std::snprintf(s_status, sizeof(s_status), "bad release.json");
    return;
  }
  install_release(&rel);
}

extern "C" void github_client_status(char *out, unsigned len) {
  if (out == nullptr || len == 0) return;
  std::snprintf(out, len, "%s", s_status);
}

extern "C" bool github_client_update_available(void) {
  return s_have_release && (s_result.update_available || s_fly_update);
}
