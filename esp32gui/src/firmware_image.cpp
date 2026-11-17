#include "firmware_image.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <dirent.h>
#include <driver/gpio.h>
#include <driver/sdspi_host.h>
#include <esp_heap_caps.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>

#include "app_log.h"
#include "board_ws7.h"
#include "fly_image.h"
#include "panel_core.h"

#ifndef SS_FW_VERSION
#define SS_FW_VERSION "dev"
#endif
#ifndef SS_BUILD_ID
#define SS_BUILD_ID 0
#endif

namespace {

constexpr const char *kMount = "/sdcard";
constexpr const char *kFlyDir = "/sdcard/firmware/fly";
constexpr const char *kPanelDir = "/sdcard/firmware/panel";
/* Cardless fallback: one Fly generation in the config partition.  The bundle
 * is ~94 KiB; the SPIFFS partition is 3.69 MiB. */
constexpr const char *kSpiffsFlyBin = "/fw_fly.bin";
constexpr const char *kSpiffsFlyJson = "/fw_fly.json";
constexpr size_t kSpiffsReserve = 8192;
constexpr int kMaxCandidates = PC_MAX_IMAGES;
constexpr gpio_num_t kDummyCs = GPIO_NUM_6;

struct candidate_t {
  rc_manifest_t manifest;
  uint8_t *data;
  uint32_t size;
  char path[96];
};

bool s_sd_mounted = false;
bool s_fly_psram = false;
ss_fw_image_t s_fly;
rc_manifest_t s_panel;
char s_panel_path[96];
bool s_panel_present = false;

void set_version(ss_fw_image_t *out, const rc_manifest_t *m) {
  rc_semver_format(&m->version, out->version);
}

bool read_manifest(const char *path, rc_manifest_t *out) {
  char text[2048];
  FILE *f = fopen(path, "rb");
  if (f == nullptr) return false;
  const size_t n = fread(text, 1, sizeof(text) - 1, f);
  fclose(f);
  if (n == 0) return false;
  text[n] = '\0';
  return rc_manifest_parse(text, out) == 0;
}

bool read_image(const candidate_t *c, uint8_t **out) {
  if (c->manifest.size == 0) return false;
  FILE *f = fopen(c->path, "rb");
  if (f == nullptr) return false;
  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len <= 0 || (uint32_t)len != c->manifest.size) {
    fclose(f);
    return false;
  }
  uint8_t *data = (uint8_t *)heap_caps_malloc((size_t)len, MALLOC_CAP_SPIRAM);
  if (data == nullptr) data = (uint8_t *)heap_caps_malloc((size_t)len,
                                                          MALLOC_CAP_INTERNAL);
  if (data == nullptr) {
    fclose(f);
    SS_LOGE("image", "no memory for %s (%ld bytes)", c->path, len);
    return false;
  }
  const size_t got = fread(data, 1, (size_t)len, f);
  fclose(f);
  if (got != (size_t)len) {
    heap_caps_free(data);
    return false;
  }
  char sha[65];
  rc_sha256_hex(data, (size_t)len, sha);
  if (std::strcmp(sha, c->manifest.sha256) != 0) {
    SS_LOGW("image", "%s: SHA-256 mismatch", c->path);
    heap_caps_free(data);
    return false;
  }
  *out = data;
  return true;
}

int scan_dir(const char *dir, const char *target, candidate_t *out, int cap) {
  DIR *d = opendir(dir);
  if (d == nullptr) {
    SS_LOGW("image", "%s not found", dir);
    return 0;
  }
  int count = 0;
  struct dirent *ent;
  while ((ent = readdir(d)) != nullptr && count < cap) {
    const size_t n = std::strlen(ent->d_name);
    if (n < 6 || std::strcmp(ent->d_name + n - 5, ".json") != 0) continue;
    char manifest_path[128];
    std::snprintf(manifest_path, sizeof(manifest_path), "%s/%s", dir,
                  ent->d_name);
    candidate_t *c = &out[count];
    std::memset(c, 0, sizeof(*c));
    if (!read_manifest(manifest_path, &c->manifest)) {
      SS_LOGW("image", "%s: bad manifest", manifest_path);
      continue;
    }
    if (std::strcmp(c->manifest.target, target) != 0) continue;
    if (std::strcmp(target, RC_TARGET_FLY) == 0 &&
        !rc_manifest_katapult_ok(&c->manifest))
      continue;
    if (std::strcmp(target, RC_TARGET_PANEL) == 0 &&
        !rc_manifest_ota_ok(&c->manifest))
      continue;
    std::snprintf(c->path, sizeof(c->path), "%s/%s", dir,
                  c->manifest.file);
    if (!read_image(c, &c->data)) continue;
    c->size = c->manifest.size;
    ++count;
  }
  closedir(d);
  return count;
}

int find_candidate(const candidate_t *c, int count, const rc_manifest_t *m) {
  if (m == nullptr) return -1;
  for (int i = 0; i < count; ++i) {
    if (c[i].size == m->size &&
        std::strcmp(c[i].manifest.sha256, m->sha256) == 0)
      return i;
  }
  return -1;
}

bool spiffs_ready(void) {
  /* config_store_init() mounts SPIFFS first at boot; this is a no-op then. */
  return SPIFFS.begin(true);
}

bool load_spiffs_fly(void) {
  if (!spiffs_ready()) return false;
  File jf = SPIFFS.open(kSpiffsFlyJson, FILE_READ);
  if (!jf) return false;
  char text[2048];
  const size_t n = jf.readBytes(text, sizeof(text) - 1);
  jf.close();
  text[n] = '\0';
  rc_manifest_t m;
  if (rc_manifest_parse(text, &m) != 0 ||
      std::strcmp(m.target, RC_TARGET_FLY) != 0 ||
      !rc_manifest_katapult_ok(&m)) {
    SS_LOGW("image", "SPIFFS fly manifest rejected");
    return false;
  }
  File bf = SPIFFS.open(kSpiffsFlyBin, FILE_READ);
  if (!bf) {
    SS_LOGW("image", "SPIFFS fly binary missing");
    return false;
  }
  const size_t len = bf.size();
  if (len == 0 || len != m.size) {
    bf.close();
    SS_LOGW("image", "SPIFFS fly size mismatch");
    return false;
  }
  uint8_t *data = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (data == nullptr)
    data = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_INTERNAL);
  if (data == nullptr) {
    bf.close();
    SS_LOGE("image", "no memory for SPIFFS fly (%u bytes)", (unsigned)len);
    return false;
  }
  const size_t got = bf.read(data, len);
  bf.close();
  char sha[65];
  rc_sha256_hex(data, len, sha);
  if (got != len || std::strcmp(sha, m.sha256) != 0) {
    heap_caps_free(data);
    SS_LOGW("image", "SPIFFS fly read/SHA-256 failed");
    return false;
  }
  /* Both stores may hold a generation; the highest build wins. */
  if (s_fly.present && m.build < s_fly.build) {
    heap_caps_free(data);
    return false;
  }
  if (s_fly_psram && s_fly.data != nullptr)
    heap_caps_free((void *)s_fly.data);
  std::memset(&s_fly, 0, sizeof(s_fly));
  s_fly.present = true;
  s_fly_psram = true;
  s_fly.data = data;
  s_fly.size = (uint32_t)len;
  s_fly.build = m.build;
  set_version(&s_fly, &m);
  std::snprintf(s_fly.sha256, sizeof(s_fly.sha256), "%s", m.sha256);
  std::snprintf(s_fly.source, sizeof(s_fly.source), "%s", "SPIFFS");
  std::snprintf(s_fly.path, sizeof(s_fly.path), "%s", kSpiffsFlyBin);
  SS_LOGI("image", "Fly %s build %lu from SPIFFS", s_fly.version,
          (unsigned long)s_fly.build);
  return true;
}

bool store_spiffs_fly(const rc_manifest_t *m, const uint8_t *data, size_t len,
                      char *status, unsigned status_len) {
  if (!spiffs_ready()) {
    if (status && status_len) std::snprintf(status, status_len, "no SPIFFS");
    return false;
  }
  char text[2048];
  if (rc_manifest_emit(m, text, sizeof(text)) != 0) {
    if (status && status_len) std::snprintf(status, status_len, "manifest failed");
    return false;
  }
  const size_t tn = std::strlen(text);
  const size_t used = SPIFFS.usedBytes();
  const size_t total = SPIFFS.totalBytes();
  if (total <= used || total - used < len + tn + kSpiffsReserve) {
    if (status && status_len) std::snprintf(status, status_len, "SPIFFS full");
    return false;
  }
  const char *tmp_bin = "/fw_fly.bin.tmp";
  const char *tmp_json = "/fw_fly.json.tmp";
  File f = SPIFFS.open(tmp_bin, FILE_WRITE);
  if (!f) {
    if (status && status_len) std::snprintf(status, status_len, "open failed");
    return false;
  }
  const bool bin_ok = f.write(data, len) == len;
  f.close();
  if (!bin_ok) {
    SPIFFS.remove(tmp_bin);
    if (status && status_len) std::snprintf(status, status_len, "short write");
    return false;
  }
  f = SPIFFS.open(tmp_json, FILE_WRITE);
  if (!f) {
    SPIFFS.remove(tmp_bin);
    if (status && status_len) std::snprintf(status, status_len, "json open failed");
    return false;
  }
  const bool json_ok = f.write((const uint8_t *)text, tn) == tn;
  f.close();
  if (!json_ok) {
    SPIFFS.remove(tmp_bin);
    SPIFFS.remove(tmp_json);
    if (status && status_len) std::snprintf(status, status_len, "json write failed");
    return false;
  }
  SPIFFS.remove(kSpiffsFlyBin);
  SPIFFS.remove(kSpiffsFlyJson);
  if (!SPIFFS.rename(tmp_bin, kSpiffsFlyBin) ||
      !SPIFFS.rename(tmp_json, kSpiffsFlyJson)) {
    if (status && status_len) std::snprintf(status, status_len, "rename failed");
    return false;
  }
  firmware_image_reload();
  if (status && status_len)
    std::snprintf(status, status_len, "stored %s in SPIFFS", m->file);
  SS_LOGI("image", "Fly bundle stored in SPIFFS");
  return true;
}

void load_sd(void) {
  board_ws7_sd_cs_enable_oc_pp();
  board_ws7_sd_select(true);

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = board_ws7_sd_bus();
  host.max_freq_khz = 10000;

  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.host_id = (spi_host_device_t)host.slot;
  /* The real SD_CS is CH422G IO4.  Hold it low through the whole scan and let
   * the SDSPI driver toggle this unconnected GPIO. */
  slot.gpio_cs = kDummyCs;
  slot.gpio_cd = SDSPI_SLOT_NO_CD;
  slot.gpio_wp = SDSPI_SLOT_NO_WP;

  esp_vfs_fat_sdmmc_mount_config_t cfg = {};
  cfg.format_if_mount_failed = false;
  cfg.max_files = 4;
  cfg.allocation_unit_size = 16 * 1024;

  sdmmc_card_t *card = nullptr;
  const esp_err_t err =
      esp_vfs_fat_sdspi_mount(kMount, &host, &slot, &cfg, &card);
  if (err != ESP_OK) {
    board_ws7_sd_select(false);
    SS_LOGW("image", "no SD card: %s", esp_err_to_name(err));
    return;
  }
  s_sd_mounted = true;
  SS_LOGI("image", "SD mounted at %s", kMount);

  candidate_t fly[kMaxCandidates];
  const int fly_n = scan_dir(kFlyDir, RC_TARGET_FLY, fly, kMaxCandidates);
  if (fly_n > 0) {
    pc_image_list_t list = {};
    for (int i = 0; i < fly_n; ++i)
      pc_image_list_add(&list, &fly[i].manifest, fly[i].manifest.sha256);
    const rc_manifest_t *sel = pc_image_select_fly(&list);
    const int idx = find_candidate(fly, fly_n, sel);
    if (idx >= 0) {
      s_fly.present = true;
      s_fly_psram = true;
      s_fly.data = fly[idx].data;
      s_fly.size = fly[idx].size;
      s_fly.build = fly[idx].manifest.build;
      set_version(&s_fly, &fly[idx].manifest);
      std::snprintf(s_fly.sha256, sizeof(s_fly.sha256), "%s",
                    fly[idx].manifest.sha256);
      std::snprintf(s_fly.source, sizeof(s_fly.source), "SD");
      std::snprintf(s_fly.path, sizeof(s_fly.path), "%s", fly[idx].path);
      SS_LOGI("image", "Fly %s build %lu from %s", s_fly.version,
              (unsigned long)s_fly.build, s_fly.path);
    }
    for (int i = 0; i < fly_n; ++i)
      if (i != idx && fly[i].data != nullptr) heap_caps_free(fly[i].data);
  }

  candidate_t panel[kMaxCandidates];
  const int panel_n = scan_dir(kPanelDir, RC_TARGET_PANEL, panel,
                               kMaxCandidates);
  if (panel_n > 0) {
    pc_image_list_t list = {};
    for (int i = 0; i < panel_n; ++i)
      pc_image_list_add(&list, &panel[i].manifest, panel[i].manifest.sha256);
    const rc_manifest_t *sel = pc_image_select_panel(&list);
    const int idx = find_candidate(panel, panel_n, sel);
    if (idx >= 0) {
      s_panel = panel[idx].manifest;
      s_panel_present = true;
      std::snprintf(s_panel_path, sizeof(s_panel_path), "%s",
                    panel[idx].path);
      SS_LOGI("image", "panel %lu from %s", (unsigned long)s_panel.build,
              s_panel_path);
    }
    for (int i = 0; i < panel_n; ++i)
      if (panel[i].data != nullptr) heap_caps_free(panel[i].data);
  }
}

void load_embedded(void) {
  const uint8_t *data = ss_fly_image_data();
  const unsigned size = ss_fly_image_size();
  if (data == nullptr || size == 0 || SS_BUILD_ID == 0) return;
  s_fly.present = true;
  s_fly_psram = false;
  s_fly.data = data;
  s_fly.size = size;
  s_fly.build = (uint32_t)SS_BUILD_ID;
  std::snprintf(s_fly.version, sizeof(s_fly.version), "%s", SS_FW_VERSION);
  rc_sha256_hex(data, size, s_fly.sha256);
  std::snprintf(s_fly.source, sizeof(s_fly.source), "UART");
  std::snprintf(s_fly.path, sizeof(s_fly.path), "embedded");
  SS_LOGI("image", "Fly %s build %lu embedded", s_fly.version,
          (unsigned long)s_fly.build);
}

}  // namespace

extern "C" void firmware_image_init(void) {
  std::memset(&s_fly, 0, sizeof(s_fly));
  std::memset(&s_panel, 0, sizeof(s_panel));
  s_panel_path[0] = '\0';
  s_panel_present = false;
  s_sd_mounted = false;
  s_fly_psram = false;

  load_sd();
  load_spiffs_fly();
  if (!s_fly.present) load_embedded();
}

extern "C" void firmware_image_reload(void) {
  if (s_fly_psram && s_fly.data != nullptr)
    heap_caps_free((void *)s_fly.data);
  std::memset(&s_fly, 0, sizeof(s_fly));
  std::memset(&s_panel, 0, sizeof(s_panel));
  s_panel_path[0] = '\0';
  s_panel_present = false;
  s_fly_psram = false;
  load_sd();
  load_spiffs_fly();
  if (!s_fly.present) load_embedded();
}

extern "C" bool firmware_image_store_fly(const rc_manifest_t *m,
                                         const uint8_t *data, size_t len,
                                         char *status, unsigned status_len) {
  if (m == nullptr || data == nullptr || len == 0 || len != m->size) {
    if (status && status_len) std::snprintf(status, status_len, "bad bundle");
    return false;
  }
  if (!s_sd_mounted)
    return store_spiffs_fly(m, data, len, status, status_len);
  if (std::strchr(m->file, '/') != nullptr ||
      std::strstr(m->file, "..") != nullptr) {
    if (status && status_len) std::snprintf(status, status_len, "bad filename");
    return false;
  }
  mkdir("/sdcard/firmware", 0777);
  mkdir("/sdcard/firmware/fly", 0777);

  char bin_path[128];
  char json_path[128];
  std::snprintf(bin_path, sizeof(bin_path), "%s/%s", kFlyDir, m->file);
  std::snprintf(json_path, sizeof(json_path), "%s", bin_path);
  const size_t n = std::strlen(json_path);
  if (n > 4 && std::strcmp(json_path + n - 4, ".bin") == 0)
    std::strcpy(json_path + n - 4, ".json");
  else
    std::snprintf(json_path + n, sizeof(json_path) - n, ".json");

  FILE *f = fopen(bin_path, "wb");
  if (f == nullptr) {
    if (status && status_len) std::snprintf(status, status_len, "open failed");
    return false;
  }
  const bool bin_ok = fwrite(data, 1, len, f) == len;
  fclose(f);
  if (!bin_ok) {
    if (status && status_len) std::snprintf(status, status_len, "short write");
    return false;
  }

  char text[2048];
  if (rc_manifest_emit(m, text, sizeof(text)) != 0) {
    if (status && status_len) std::snprintf(status, status_len, "manifest failed");
    return false;
  }
  f = fopen(json_path, "wb");
  if (f == nullptr) {
    if (status && status_len) std::snprintf(status, status_len, "json open failed");
    return false;
  }
  const size_t tn = std::strlen(text);
  const bool json_ok = fwrite(text, 1, tn, f) == tn;
  fclose(f);
  if (!json_ok) {
    if (status && status_len) std::snprintf(status, status_len, "json write failed");
    return false;
  }
  firmware_image_reload();
  if (status && status_len)
    std::snprintf(status, status_len, "stored %s", m->file);
  SS_LOGI("image", "Fly bundle stored at %s", bin_path);
  return true;
}

extern "C" bool firmware_image_sd_present(void) { return s_sd_mounted; }

extern "C" const ss_fw_image_t *firmware_image_fly(void) {
  return s_fly.present ? &s_fly : nullptr;
}

extern "C" const rc_manifest_t *firmware_image_panel(void) {
  return s_panel_present ? &s_panel : nullptr;
}

extern "C" const char *firmware_image_panel_path(void) {
  return s_panel_present ? s_panel_path : nullptr;
}
