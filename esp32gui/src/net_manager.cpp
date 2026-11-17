#include "net_manager.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_event.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <mbedtls/base64.h>
#include <nvs_flash.h>
#include <time.h>

#include <cstdio>
#include <cstring>

#include "app_log.h"
#include "firmware_image.h"
#include "github_client.h"
#include "net_core.h"
#include "panel_core.h"
#include "panel_ota.h"
#include "release_core.h"
#include "upgrade_manager.h"

#ifndef SS_FW_VERSION
#define SS_FW_VERSION "dev"
#endif
#ifndef SS_BUILD_ID
#define SS_BUILD_ID 0
#endif

namespace {

WebServer s_server(80);
Preferences s_prefs;
SemaphoreHandle_t s_mutex = nullptr;

net_link_t s_link;
bool s_have_cred = false;
char s_ssid[33] = "";
char s_pass[65] = "";
char s_admin_salt[33] = "";
char s_admin_hash[65] = "";
char s_ap_ssid[33] = "";
char s_ap_pass[16] = "";
char s_csrf[17] = "";
char s_ip[16] = "";
int16_t s_rssi = 0;
bool s_ap_up = false;
bool s_service_on = false;
bool s_server_on = false;
bool s_sntp_started = false;
uint32_t s_connect_deadline = 0;
uint32_t s_retry_deadline = 0;
uint8_t s_retry_count = 0;

enum cmd_t { CMD_NONE, CMD_SETUP, CMD_FORGET, CMD_SCAN, CMD_SERVICE,
             CMD_GITHUB_CHECK, CMD_GITHUB_INSTALL, CMD_CONNECT, CMD_SET_ADMIN,
             CMD_RETRY, CMD_OFF, CMD_AP_NEW_KEY, CMD_AP_SET_KEY };
volatile cmd_t s_cmd = CMD_NONE;
volatile bool s_cmd_service_on = false;

char s_scan[12][33];
int s_scan_rssi[12];
int s_scan_count = 0;
char s_pending_ssid[33] = "";
char s_pending_pass[65] = "";
char s_pending_admin[65] = "";
char s_pending_ap_key[64] = "";
bool s_scanning = false;

uint8_t *s_up_json = nullptr;
size_t s_up_json_len = 0;
size_t s_up_json_cap = 0;
uint8_t *s_up_bin = nullptr;
size_t s_up_bin_len = 0;
size_t s_up_bin_cap = 0;
bool s_up_started = false;
bool s_up_denied = false;
bool s_up_too_large = false;

void lock(void) {
  if (s_mutex != nullptr) xSemaphoreTake(s_mutex, portMAX_DELAY);
}

void unlock(void) {
  if (s_mutex != nullptr) xSemaphoreGive(s_mutex);
}

void random_hex(char *out, int bytes) {
  for (int i = 0; i < bytes; ++i)
    std::snprintf(out + i * 2, 3, "%02x", (unsigned)(esp_random() & 0xFF));
  out[bytes * 2] = '\0';
}

void random_password(char *out, int n) {
  static const char chars[] =
      "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  for (int i = 0; i < n; ++i)
    out[i] = chars[esp_random() % (sizeof(chars) - 1)];
  out[n] = '\0';
}

void hash_admin(const char *salt, const char *pass, char out[65]) {
  char buf[128];
  std::snprintf(buf, sizeof(buf), "%s:%s", salt, pass);
  rc_sha256_hex(buf, std::strlen(buf), out);
}

void load_nvs(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    SS_LOGE("net", "NVS init failed: %s", esp_err_to_name(err));
    return;
  }
  if (!s_prefs.begin("ssnet", false)) {
    SS_LOGE("net", "NVS namespace open failed");
    return;
  }
  const String ssid = s_prefs.getString("ssid", "");
  const String pass = s_prefs.getString("pass", "");
  std::snprintf(s_ssid, sizeof(s_ssid), "%s", ssid.c_str());
  std::snprintf(s_pass, sizeof(s_pass), "%s", pass.c_str());
  s_have_cred = s_ssid[0] != '\0';

  const String salt = s_prefs.getString("adm_salt", "");
  const String hash = s_prefs.getString("adm_hash", "");
  std::snprintf(s_admin_salt, sizeof(s_admin_salt), "%s", salt.c_str());
  std::snprintf(s_admin_hash, sizeof(s_admin_hash), "%s", hash.c_str());
  if (s_admin_salt[0] == '\0') {
    random_hex(s_admin_salt, 16);
    s_prefs.putString("adm_salt", s_admin_salt);
  }

  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  std::snprintf(s_ap_ssid, sizeof(s_ap_ssid), "SuspendedSpheres-%02X%02X",
                mac[4], mac[5]);
  const String ap = s_prefs.getString("ap_pass", "");
  std::snprintf(s_ap_pass, sizeof(s_ap_pass), "%s", ap.c_str());
  if (s_ap_pass[0] == '\0') {
    random_password(s_ap_pass, 10);
    s_prefs.putString("ap_pass", s_ap_pass);
  }
  SS_LOGI("net", "NVS ready: credentials=%d admin=%d ap=%s",
          s_have_cred ? 1 : 0, s_admin_hash[0] != '\0' ? 1 : 0, s_ap_ssid);
}

bool admin_set(void) { return s_admin_hash[0] != '\0'; }

bool authorized(void) {
  if (!admin_set()) return false;
  const String auth = s_server.header("Authorization");
  if (!auth.startsWith("Basic ")) return false;
  uint8_t decoded[128];
  size_t out = 0;
  const int rc = mbedtls_base64_decode(
      decoded, sizeof(decoded) - 1, &out,
      reinterpret_cast<const uint8_t *>(auth.c_str() + 6),
      auth.length() - 6);
  if (rc != 0) return false;
  decoded[out] = '\0';
  char *colon = std::strchr(reinterpret_cast<char *>(decoded), ':');
  if (colon == nullptr) return false;
  *colon = '\0';
  const char *user = reinterpret_cast<const char *>(decoded);
  if (std::strcmp(user, "admin") != 0) return false;
  char hash[65];
  hash_admin(s_admin_salt, colon + 1, hash);
  return std::strcmp(hash, s_admin_hash) == 0;
}

void send_unauthorized(void) {
  s_server.sendHeader("WWW-Authenticate", "Basic realm=\"Suspended Spheres\"");
  s_server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
}

void send_error(int code, const char *text) {
  char json[128];
  std::snprintf(json, sizeof(json), "{\"error\":\"%s\"}", text);
  s_server.send(code, "application/json", json);
}

String html_escape(const char *s) {
  String out;
  for (const char *p = s; p != nullptr && *p != '\0'; ++p) {
    switch (*p) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += *p; break;
    }
  }
  return out;
}

String csrf_input(void) {
  String s = "<input type=hidden name=csrf value=\"";
  s += s_csrf;
  s += "\">";
  return s;
}

void start_server(void) {
  if (s_server_on) return;
  random_hex(s_csrf, 8);
  s_server.begin();
  s_server_on = true;
  SS_LOGI("net", "HTTP service started");
}

void stop_server(void) {
  if (!s_server_on) return;
  s_server.stop();
  s_server_on = false;
  SS_LOGI("net", "HTTP service stopped");
}

void set_reason(const char *text) {
  lock();
  std::snprintf(s_link.reason, sizeof(s_link.reason), "%s",
                text != nullptr ? text : "");
  unlock();
}

void start_ap(void) {
  SS_LOGI("net", "AP start: internal free=%u largest=%u",
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(s_ap_ssid, s_ap_pass, 6, 0, 4)) {
    set_reason("setup AP failed");
    SS_LOGE("net", "setup AP failed");
    return;
  }
  s_ap_up = true;
  const IPAddress ip = WiFi.softAPIP();
  lock();
  s_link.state = NET_AP_SETUP;
  s_link.have_credentials = s_have_cred;
  s_link.setup_deadline_ms = millis() + NET_SETUP_WINDOW_MS;
  s_link.reason[0] = '\0';
  s_retry_deadline = 0;
  s_retry_count = 0;
  std::snprintf(s_ip, sizeof(s_ip), "%s", ip.toString().c_str());
  s_rssi = 0;
  s_service_on = true;
  unlock();
  s_service_on = true;
  start_server();
  SS_LOGI("net", "setup AP %s at %s", s_ap_ssid, s_ip);
}

void connect_sta(void) {
  if (s_ap_up) {
    WiFi.softAPdisconnect(true);
    s_ap_up = false;
    stop_server();
    s_service_on = false;
  }
  if (s_ssid[0] == '\0') {
    lock();
    s_link.state = NET_OFF;
    s_link.reason[0] = '\0';
    std::snprintf(s_link.reason, sizeof(s_link.reason), "no stored SSID");
    unlock();
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(s_ssid, s_pass);
  lock();
  s_link.state = NET_CONNECTING;
  s_link.have_credentials = true;
  s_link.reason[0] = '\0';
  s_connect_deadline = millis() + 20000u;
  s_retry_deadline = 0;
  unlock();
  SS_LOGI("net", "connecting to %s", s_ssid);
}

void do_setup(void) { start_ap(); }

void do_forget(void) {
  s_prefs.remove("ssid");
  s_prefs.remove("pass");
  s_prefs.remove("adm_hash");
  s_ssid[0] = '\0';
  s_pass[0] = '\0';
  s_admin_hash[0] = '\0';
  s_have_cred = false;
  if (!s_ap_up) WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  stop_server();
  lock();
  s_ap_up = false;
  s_service_on = false;
  s_retry_deadline = 0;
  s_retry_count = 0;
  s_link.state = NET_OFF;
  s_link.have_credentials = false;
  s_link.reason[0] = '\0';
  std::snprintf(s_link.reason, sizeof(s_link.reason),
                "credentials forgotten; WiFi off");
  std::snprintf(s_ip, sizeof(s_ip), "%s", "");
  s_rssi = 0;
  unlock();
  SS_LOGI("net", "credentials forgotten; WiFi off");
}

void do_scan(void) {
  lock();
  s_scanning = true;
  unlock();
  const int n = WiFi.scanNetworks();
  lock();
  s_scan_count = 0;
  for (int i = 0; i < n && s_scan_count < 12; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    const int rssi = WiFi.RSSI(i);
    int found = -1;
    for (int j = 0; j < s_scan_count; ++j) {
      if (ssid == s_scan[j]) {
        found = j;
        break;
      }
    }
    if (found >= 0) {
      if (rssi > s_scan_rssi[found]) s_scan_rssi[found] = rssi;
    } else {
      std::snprintf(s_scan[s_scan_count], sizeof(s_scan[s_scan_count]), "%s",
                    ssid.c_str());
      s_scan_rssi[s_scan_count] = rssi;
      ++s_scan_count;
    }
  }
  unlock();
  WiFi.scanDelete();
  lock();
  s_scanning = false;
  std::snprintf(s_link.reason, sizeof(s_link.reason), "scan: %d networks",
                s_scan_count);
  unlock();
  SS_LOGI("net", "scan found %d networks", s_scan_count);
}

void do_service(bool on) {
  lock();
  const net_state_t state = s_link.state;
  unlock();
  if (on && state != NET_CONNECTED && state != NET_AP_SETUP) {
    lock();
    s_service_on = false;
    std::snprintf(s_link.reason, sizeof(s_link.reason), "WiFi is off");
    unlock();
    stop_server();
    return;
  }
  lock();
  s_service_on = on;
  s_link.reason[0] = '\0';
  unlock();
  if (on) start_server();
  else stop_server();
}

void finish_connected(void) {
  s_prefs.putString("ssid", s_ssid);
  s_prefs.putString("pass", s_pass);
  s_have_cred = true;
  s_retry_deadline = 0;
  s_retry_count = 0;
  if (s_ap_up) {
    WiFi.softAPdisconnect(true);
    s_ap_up = false;
  }
  lock();
  net_link_result(&s_link, true, nullptr);
  std::snprintf(s_ip, sizeof(s_ip), "%s", WiFi.localIP().toString().c_str());
  s_rssi = (int16_t)WiFi.RSSI();
  unlock();
  if (!s_sntp_started) {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    s_sntp_started = true;
  }
  SS_LOGI("net", "connected to %s, IP %s", s_ssid, s_ip);
}

void schedule_retry(const char *reason) {
  WiFi.disconnect();
  lock();
  s_link.state = NET_CONNECTING;
  s_link.have_credentials = s_have_cred;
  ++s_retry_count;
  s_retry_deadline = millis() + 5000u;
  std::snprintf(s_link.reason, sizeof(s_link.reason),
                "%s; retry %u in 5 s",
                reason != nullptr ? reason : "connect failed",
                (unsigned)s_retry_count);
  std::snprintf(s_ip, sizeof(s_ip), "%s", "");
  s_rssi = 0;
  unlock();
  SS_LOGW("net", "connect failed: %s; retrying",
          reason != nullptr ? reason : "?");
}

void poll_link(void) {
  lock();
  const net_state_t state = s_link.state;
  unlock();

  if (state == NET_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      finish_connected();
    } else {
      lock();
      const uint32_t now = millis();
      const bool retry_due =
          s_retry_deadline != 0 && (int32_t)(now - s_retry_deadline) >= 0;
      const bool retry_wait = s_retry_deadline != 0 && !retry_due;
      const bool timeout =
          s_retry_deadline == 0 && (int32_t)(now - s_connect_deadline) >= 0;
      if (retry_due) s_retry_deadline = 0;
      unlock();
      if (retry_due) {
        connect_sta();
      } else if (timeout) {
        schedule_retry("connect timeout");
      } else if (retry_wait) {
        /* waiting for the next visible retry */
      }
    }
  } else if (state == NET_CONNECTED) {
    if (WiFi.status() == WL_CONNECTED) {
      lock();
      std::snprintf(s_ip, sizeof(s_ip), "%s",
                    WiFi.localIP().toString().c_str());
      s_rssi = (int16_t)WiFi.RSSI();
      unlock();
    } else {
      schedule_retry("link lost");
    }
  } else if (state == NET_AP_SETUP || state == NET_FAILED) {
    if (WiFi.status() == WL_CONNECTED && !s_ap_up) finish_connected();
  }

  lock();
  net_link_poll(&s_link, millis());
  const bool expired = s_link.state == NET_OFF && s_ap_up;
  unlock();
  if (expired) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    s_ap_up = false;
    do_service(false);
    SS_LOGI("net", "setup window expired");
  }
}

/* ------------------------------------------------------------------ */
/*  HTTP handlers                                                      */
/* ------------------------------------------------------------------ */

String setup_page(void) {
  String p;
  p += "<!doctype html><html><head><meta charset=utf-8><title>Suspended "
       "Spheres setup</title></head><body><h1>Setup</h1>";
  p += "<p>Join <b>";
  p += html_escape(s_ap_ssid);
  p += "</b> with the key shown on the panel, then submit.</p>";
  p += "<form method=post action=/api/scan>";
  p += csrf_input();
  p += "<button>SCAN</button></form>";
  if (s_scan_count > 0) {
    p += "<ul>";
    for (int i = 0; i < s_scan_count; ++i) {
      p += "<li>";
      p += html_escape(s_scan[i]);
      p += "</li>";
    }
    p += "</ul>";
  }
  p += "<form method=post action=/api/wifi>";
  p += csrf_input();
  p += "SSID <input name=ssid maxlength=32><br>";
  p += "Password <input name=password type=password maxlength=64><br>";
  p += "Admin password <input name=admin type=password maxlength=64><br>";
  p += "Repeat admin <input name=admin2 type=password maxlength=64><br>";
  p += "<button>CONNECT</button></form></body></html>";
  return p;
}

String control_page(void) {
  String p;
  p += "<!doctype html><html><head><meta charset=utf-8><title>Suspended "
       "Spheres</title></head><body><h1>Suspended Spheres</h1><pre>";
  p += html_escape(net_state_text(s_link.state));
  p += " ";
  p += html_escape(s_ip);
  p += "</pre>";
  p += "<form method=post action=/api/wifi>";
  p += csrf_input();
  p += "SSID <input name=ssid value=\"";
  p += html_escape(s_ssid);
  p += "\"><br>Password <input name=password type=password maxlength=64><br>";
  p += "<button>RECONNECT</button></form>";
  p += "<form method=post action=/api/service>";
  p += csrf_input();
  p += "<input type=hidden name=on value=\"";
  p += s_service_on ? "0" : "1";
  p += "\"><button>";
  p += s_service_on ? "SERVER OFF" : "SERVER ON";
  p += "</button></form>";
  p += "<form method=post enctype=multipart/form-data "
       "action=/api/update/panel>";
  p += csrf_input();
  p += "<input type=file name=json accept=.json> ";
  p += "<input type=file name=bin accept=.bin> ";
  p += "<button>UPLOAD PANEL</button></form>";
  p += "<form method=post enctype=multipart/form-data "
       "action=/api/images/fly>";
  p += csrf_input();
  p += "<input type=file name=json accept=.json> ";
  p += "<input type=file name=bin accept=.bin> ";
  p += "<button>STORE FLY</button></form>";
  p += "<form method=post action=/api/upgrade/all>";
  p += csrf_input();
  p += "<button>UPGRADE ALL OUTDATED</button></form>";
  p += "<form method=post action=/api/github/check>";
  p += csrf_input();
  p += "<button>CHECK GITHUB</button></form>";
  p += "<form method=post action=/api/github/install>";
  p += csrf_input();
  p += "<button>INSTALL GITHUB UPDATE</button></form>";
  p += "</body></html>";
  return p;
}

bool csrf_ok(void) {
  const String t = s_server.arg("csrf");
  return t.length() > 0 && t == s_csrf;
}

void handle_root(void) {
  if (!admin_set()) {
    if (s_ap_up) {
      s_server.send(200, "text/html", setup_page());
    } else {
      send_error(409, "enable WiFi setup on the panel");
    }
    return;
  }
  if (!authorized()) {
    send_unauthorized();
    return;
  }
  s_server.send(200, "text/html", control_page());
}

void handle_status(void) {
  if (admin_set() && !authorized()) {
    send_unauthorized();
    return;
  }
  if (!admin_set() && !s_ap_up) {
    send_error(409, "enable WiFi setup on the panel");
    return;
  }
  char net[256];
  net_status_json(net, sizeof(net), &s_link, s_service_on, s_ip);
  char out[384];
  std::snprintf(out, sizeof(out),
                "{\"panel\":\"%s\",\"build\":%lu,\"queue\":%s,\"net\":%s}",
                SS_FW_VERSION, (unsigned long)SS_BUILD_ID,
                upgrade_manager_active() ? "true" : "false", net);
  s_server.send(200, "application/json", out);
}

void handle_scan(void) {
  if (!csrf_ok()) {
    send_error(422, "csrf");
    return;
  }
  if (admin_set() && !authorized()) {
    send_unauthorized();
    return;
  }
  do_scan();
  s_server.sendHeader("Location", "/");
  s_server.send(303, "text/plain", "");
}

void handle_wifi(void) {
  if (!csrf_ok()) {
    send_error(422, "csrf");
    return;
  }
  const bool setup_open = !admin_set() && s_ap_up;
  if (!setup_open && !authorized()) {
    send_unauthorized();
    return;
  }
  const String admin = s_server.arg("admin");
  if (admin.length() > 0 && !admin_set()) {
    if (admin != s_server.arg("admin2")) {
      send_error(422, "admin password mismatch");
      return;
    }
    hash_admin(s_admin_salt, admin.c_str(), s_admin_hash);
    s_prefs.putString("adm_hash", s_admin_hash);
    SS_LOGI("net", "admin password set");
  }
  const String ssid = s_server.arg("ssid");
  const String password = s_server.arg("password");
  if (ssid.length() > 0) {
    if (password.length() == 0) {
      send_error(422, "password required");
      return;
    }
    std::snprintf(s_ssid, sizeof(s_ssid), "%s", ssid.c_str());
    std::snprintf(s_pass, sizeof(s_pass), "%s", password.c_str());
    s_have_cred = true;
    connect_sta();
  }
  s_server.sendHeader("Location", "/");
  s_server.send(303, "text/plain", "");
}

void handle_service(void) {
  if (!csrf_ok()) {
    send_error(422, "csrf");
    return;
  }
  if (!authorized()) {
    send_unauthorized();
    return;
  }
  do_service(s_server.arg("on") == "1");
  s_server.sendHeader("Location", "/");
  s_server.send(303, "text/plain", "");
}

void handle_github_check(void) {
  if (!csrf_ok()) {
    send_error(422, "csrf");
    return;
  }
  if (!authorized()) {
    send_unauthorized();
    return;
  }
  github_client_check();
  char text[96];
  github_client_status(text, sizeof(text));
  s_server.send(200, "application/json", ("{\"status\":\"" +
                 String(text) + "\"}").c_str());
}

void handle_github_install(void) {
  if (!csrf_ok()) {
    send_error(422, "csrf");
    return;
  }
  if (!authorized()) {
    send_unauthorized();
    return;
  }
  if (!github_client_update_available()) {
    send_error(409, "no update checked");
    return;
  }
  s_server.send(200, "application/json", "{\"status\":\"installing\"}");
  vTaskDelay(pdMS_TO_TICKS(100));
  github_client_install();
}

void handle_upgrade(void) {
  if (!csrf_ok()) {
    send_error(422, "csrf");
    return;
  }
  if (!authorized()) {
    send_unauthorized();
    return;
  }
  if (upgrade_manager_active()) {
    send_error(409, "queue active");
    return;
  }
  if (!upgrade_manager_start_all()) {
    send_error(409, "no outdated boards or no image");
    return;
  }
  s_server.send(200, "application/json", "{\"status\":\"started\"}");
}

void reset_upload(void) {
  if (s_up_json != nullptr) heap_caps_free(s_up_json);
  if (s_up_bin != nullptr) heap_caps_free(s_up_bin);
  s_up_json = nullptr;
  s_up_json_len = 0;
  s_up_json_cap = 0;
  s_up_bin = nullptr;
  s_up_bin_len = 0;
  s_up_bin_cap = 0;
  s_up_started = false;
  s_up_denied = false;
  s_up_too_large = false;
}

bool append(uint8_t **buf, size_t *len, size_t *cap, const uint8_t *data,
            size_t n, size_t max) {
  if (*len + n > max) return false;
  if (*len + n > *cap) {
    size_t want = *cap == 0 ? 65536 : *cap;
    while (want < *len + n) want *= 2;
    if (want > max) want = max;
    uint8_t *p = (uint8_t *)heap_caps_realloc(*buf, want, MALLOC_CAP_SPIRAM);
    if (p == nullptr) return false;
    *buf = p;
    *cap = want;
  }
  std::memcpy(*buf + *len, data, n);
  *len += n;
  return true;
}

void handle_upload(void) {
  HTTPUpload &upload = s_server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (!s_up_started) {
      reset_upload();
      s_up_started = true;
      s_up_denied = !authorized();
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    reset_upload();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (s_up_denied || s_up_too_large) return;
    const size_t max = 6u * 1024u * 1024u;
    if (upload.name == "json") {
      if (!append(&s_up_json, &s_up_json_len, &s_up_json_cap, upload.buf,
                  upload.currentSize, 4096))
        s_up_too_large = true;
    } else if (upload.name == "bin") {
      if (!append(&s_up_bin, &s_up_bin_len, &s_up_bin_cap, upload.buf,
                  upload.currentSize, max))
        s_up_too_large = true;
    }
  }
}

void handle_upload_final(void) {
  if (s_up_denied) {
    reset_upload();
    send_unauthorized();
    return;
  }
  if (!csrf_ok()) {
    reset_upload();
    send_error(422, "csrf");
    return;
  }
  if (s_up_too_large) {
    reset_upload();
    send_error(413, "body too large");
    return;
  }
  if (s_up_json == nullptr || s_up_bin == nullptr || s_up_json_len == 0 ||
      s_up_bin_len == 0) {
    reset_upload();
    send_error(422, "json and bin are required");
    return;
  }
  char *text = (char *)heap_caps_malloc(s_up_json_len + 1, MALLOC_CAP_INTERNAL);
  if (text == nullptr) {
    reset_upload();
    send_error(500, "no memory");
    return;
  }
  std::memcpy(text, s_up_json, s_up_json_len);
  text[s_up_json_len] = '\0';
  rc_manifest_t m;
  const int parse = rc_manifest_parse(text, &m);
  heap_caps_free(text);
  if (parse != 0) {
    reset_upload();
    send_error(422, "bad manifest");
    return;
  }
  const bool fly_path = s_server.uri() == "/api/images/fly";
  if ((fly_path && std::strcmp(m.target, RC_TARGET_FLY) != 0) ||
      (!fly_path && std::strcmp(m.target, RC_TARGET_PANEL) != 0)) {
    reset_upload();
    send_error(422, "wrong target");
    return;
  }
  net_upload_req_t req = {};
  req.server_on = s_service_on;
  req.password_ok = true;
  req.show_running = false;
  req.queue_active = upgrade_manager_active();
  req.max_size = fly_path ? (2u * 1024u * 1024u) : (6u * 1024u * 1024u);
  const net_upload_result_t check =
      net_upload_check(&req, &m, s_up_bin_len);
  if (check != NET_UP_OK) {
    reset_upload();
    if (check == NET_UP_SHOW_ACTIVE) send_error(409, "show or queue active");
    else if (check == NET_UP_TOO_LARGE) send_error(413, "too large");
    else send_error(422, "upload refused");
    return;
  }
  char sha[65];
  rc_sha256_hex(s_up_bin, s_up_bin_len, sha);
  if (std::strcmp(sha, m.sha256) != 0) {
    reset_upload();
    send_error(422, "SHA-256 mismatch");
    return;
  }
  char status[80] = "idle";
  bool ok = false;
  if (fly_path) {
    ok = firmware_image_store_fly(&m, s_up_bin, s_up_bin_len, status,
                                  sizeof(status));
  } else {
    ok = panel_ota_apply(&m, s_up_bin, s_up_bin_len, status, sizeof(status));
  }
  reset_upload();
  if (!ok) {
    send_error(500, status);
    return;
  }
  s_server.send(200, "application/json", "{\"status\":\"ok\"}");
  if (!fly_path) {
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
  }
}

void register_handlers(void) {
  s_server.on("/", HTTP_GET, handle_root);
  s_server.on("/api/status", HTTP_GET, handle_status);
  s_server.on("/api/scan", HTTP_POST, handle_scan);
  s_server.on("/api/wifi", HTTP_POST, handle_wifi);
  s_server.on("/api/service", HTTP_POST, handle_service);
  s_server.on("/api/upgrade/all", HTTP_POST, handle_upgrade);
  s_server.on("/api/github/check", HTTP_POST, handle_github_check);
  s_server.on("/api/github/install", HTTP_POST, handle_github_install);
  s_server.on("/api/update/panel", HTTP_POST, handle_upload_final, handle_upload);
  s_server.on("/api/images/fly", HTTP_POST, handle_upload_final, handle_upload);
}

bool apply_ap_key(const char *key) {
  const size_t n = key != nullptr ? std::strlen(key) : 0;
  if (n < 8 || n > 63) {
    lock();
    std::snprintf(s_link.reason, sizeof(s_link.reason),
                  "AP password must be 8..63 characters");
    unlock();
    return false;
  }
  std::snprintf(s_ap_pass, sizeof(s_ap_pass), "%s", key);
  s_prefs.putString("ap_pass", s_ap_pass);
  if (s_ap_up) {
    WiFi.softAPdisconnect(true);
    s_ap_up = false;
    start_ap();
  }
  return true;
}

void do_ap_new_key(void) {
  char key[16];
  random_password(key, 10);
  if (apply_ap_key(key)) SS_LOGI("net", "AP key rotated");
}

void do_ap_set_key(void) {
  char key[64];
  lock();
  std::snprintf(key, sizeof(key), "%s", s_pending_ap_key);
  s_pending_ap_key[0] = '\0';
  unlock();
  if (apply_ap_key(key)) SS_LOGI("net", "AP key set by operator");
}

void do_off(void) {
  if (!s_ap_up) WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  stop_server();
  lock();
  s_ap_up = false;
  s_service_on = false;
  s_retry_deadline = 0;
  s_retry_count = 0;
  s_link.state = NET_OFF;
  s_link.have_credentials = s_have_cred;
  std::snprintf(s_link.reason, sizeof(s_link.reason), "WiFi off");
  std::snprintf(s_ip, sizeof(s_ip), "%s", "");
  s_rssi = 0;
  unlock();
  SS_LOGI("net", "WiFi off");
}

void net_task(void *arg) {
  (void)arg;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(50));
    cmd_t cmd;
    bool service_on;
    lock();
    cmd = s_cmd;
    s_cmd = CMD_NONE;
    service_on = s_cmd_service_on;
    unlock();
    switch (cmd) {
      case CMD_SETUP: do_setup(); break;
      case CMD_FORGET: do_forget(); break;
      case CMD_SCAN: do_scan(); break;
      case CMD_SERVICE: do_service(service_on); break;
      case CMD_GITHUB_CHECK: github_client_check(); break;
      case CMD_GITHUB_INSTALL: github_client_install(); break;
      case CMD_OFF: do_off(); break;
      case CMD_AP_NEW_KEY: do_ap_new_key(); break;
      case CMD_AP_SET_KEY: do_ap_set_key(); break;
      case CMD_CONNECT: {
        lock();
        std::snprintf(s_ssid, sizeof(s_ssid), "%s", s_pending_ssid);
        std::snprintf(s_pass, sizeof(s_pass), "%s", s_pending_pass);
        s_have_cred = true;
        s_retry_count = 0;
        unlock();
        s_pending_pass[0] = '\0';
        connect_sta();
        break;
      }
      case CMD_RETRY:
        lock();
        s_retry_count = 0;
        unlock();
        if (s_have_cred && s_ssid[0] != '\0') connect_sta();
        break;
      case CMD_SET_ADMIN: {
        char pass[65];
        lock();
        std::snprintf(pass, sizeof(pass), "%s", s_pending_admin);
        s_pending_admin[0] = '\0';
        unlock();
        if (pass[0] != '\0') {
          hash_admin(s_admin_salt, pass, s_admin_hash);
          s_prefs.putString("adm_hash", s_admin_hash);
          lock();
          std::snprintf(s_link.reason, sizeof(s_link.reason),
                        "admin password set");
          unlock();
        }
        break;
      }
      default: break;
    }
    poll_link();
    if (s_server_on) s_server.handleClient();
  }
}

}  // namespace

extern "C" void net_manager_init(void) {
  if (s_mutex == nullptr) s_mutex = xSemaphoreCreateMutex();
  /* lwIP and the default event loop allocate their semaphores and queues
   * here, while internal DRAM is still contiguous, not on the first setup
   * press after LVGL has fragmented it. */
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    SS_LOGE("net", "esp_netif_init: %s", esp_err_to_name(err));
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    SS_LOGE("net", "event loop: %s", esp_err_to_name(err));
  load_nvs();
  net_link_init(&s_link);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  register_handlers();
  if (xTaskCreatePinnedToCore(net_task, "net", 8192, nullptr, 2, nullptr, 0) !=
      pdPASS) {
    SS_LOGE("net", "network task not started");
  }
  SS_LOGI("net", "state OFF; WiFi starts only on an explicit operator action");
}

extern "C" void net_manager_setup(void) {
  lock();
  s_cmd = CMD_SETUP;
  unlock();
}

extern "C" void net_manager_forget(void) {
  lock();
  s_cmd = CMD_FORGET;
  unlock();
}

extern "C" void net_manager_scan(void) {
  lock();
  if (s_scanning || s_link.state == NET_CONNECTING) {
    unlock();
    return;
  }
  std::snprintf(s_link.reason, sizeof(s_link.reason), "scanning...");
  s_cmd = CMD_SCAN;
  unlock();
}

extern "C" void net_manager_service(bool on) {
  lock();
  s_cmd_service_on = on;
  s_cmd = CMD_SERVICE;
  unlock();
}

extern "C" void net_manager_service_toggle(void) {
  lock();
  s_cmd_service_on = !s_service_on;
  s_cmd = CMD_SERVICE;
  unlock();
}

extern "C" void net_manager_retry(void) {
  lock();
  s_cmd = CMD_RETRY;
  unlock();
}

extern "C" void net_manager_ap(void) {
  lock();
  s_cmd = CMD_SETUP;
  unlock();
}



extern "C" void net_manager_ap_new_key(void) {
  lock();
  s_cmd = CMD_AP_NEW_KEY;
  unlock();
}

extern "C" bool net_manager_set_ap_key(const char *key) {
  if (key == nullptr) return false;
  lock();
  std::snprintf(s_pending_ap_key, sizeof(s_pending_ap_key), "%s", key);
  s_cmd = CMD_AP_SET_KEY;
  unlock();
  return true;
}

extern "C" void net_manager_off(void) {
  lock();
  s_cmd = CMD_OFF;
  unlock();
}

extern "C" bool net_manager_connect(const char *ssid,
                                    const char *password) {
  if (ssid == nullptr || password == nullptr || ssid[0] == '\0' ||
      password[0] == '\0')
    return false;
  lock();
  std::snprintf(s_pending_ssid, sizeof(s_pending_ssid), "%s", ssid);
  std::snprintf(s_pending_pass, sizeof(s_pending_pass), "%s", password);
  s_cmd = CMD_CONNECT;
  unlock();
  return true;
}

extern "C" bool net_manager_set_admin(const char *password) {
  if (password == nullptr || password[0] == '\0') return false;
  lock();
  std::snprintf(s_pending_admin, sizeof(s_pending_admin), "%s", password);
  s_cmd = CMD_SET_ADMIN;
  unlock();
  return true;
}

extern "C" void net_manager_github_check(void) {
  lock();
  s_cmd = CMD_GITHUB_CHECK;
  unlock();
}

extern "C" void net_manager_github_install(void) {
  lock();
  s_cmd = CMD_GITHUB_INSTALL;
  unlock();
}

extern "C" bool net_manager_service_on(void) {
  lock();
  const bool on = s_service_on;
  unlock();
  return on;
}

extern "C" void net_manager_status(char *out, unsigned len) {
  if (out == nullptr || len == 0) return;
  lock();
  std::snprintf(out, len, "%s ssid=%s ip=%s service=%s cred=%d admin=%d %s",
                net_state_text(s_link.state), s_ssid[0] ? s_ssid : s_ap_ssid,
                s_ip, s_service_on ? "on" : "off", s_have_cred ? 1 : 0,
                admin_set() ? 1 : 0, s_link.reason);
  unlock();
}

extern "C" void net_manager_ap_key(char *out, unsigned len) {
  if (out == nullptr || len == 0) return;
  lock();
  std::snprintf(out, len, "%s", s_ap_up ? s_ap_pass : "");
  unlock();
}

extern "C" int net_manager_scan_count(void) { return s_scan_count; }

extern "C" const char *net_manager_scan_ssid(int index) {
  if (index < 0 || index >= s_scan_count) return "";
  return s_scan[index];
}

extern "C" void net_manager_snapshot(ui_snapshot_t *snap) {
  if (snap == nullptr) return;
  lock();
  snap->wifi_present = true;
  snap->wifi_connected = s_link.state == NET_CONNECTED;
  snap->wifi_has_credentials = s_have_cred;
  std::snprintf(snap->wifi_state, sizeof(snap->wifi_state), "%s",
                net_state_text(s_link.state));
  std::snprintf(snap->wifi_reason, sizeof(snap->wifi_reason), "%s",
                s_link.reason);
  snap->service_on = s_service_on;
  snap->wifi_scanning = s_scanning;
  snap->wifi_scan_count = s_scan_count;
  for (int i = 0; i < s_scan_count && i < UI_MAX_WIFI_SCAN; ++i)
    std::snprintf(snap->wifi_scan_ssid[i], sizeof(snap->wifi_scan_ssid[i]),
                  "%s", s_scan[i]);
  github_client_status(snap->github_status, sizeof(snap->github_status));
  snap->github_update_available = github_client_update_available();
  snap->wifi_rssi = s_rssi;
  std::snprintf(snap->wifi_ip, sizeof(snap->wifi_ip), "%s", s_ip);
  if (s_link.state == NET_AP_SETUP) {
    std::snprintf(snap->wifi_ssid, sizeof(snap->wifi_ssid), "%s", s_ap_ssid);
    std::snprintf(snap->wifi_ap_ssid, sizeof(snap->wifi_ap_ssid), "%s",
                  s_ap_ssid);
    std::snprintf(snap->wifi_ap_password, sizeof(snap->wifi_ap_password),
                  "%s", s_ap_pass);
  } else {
    std::snprintf(snap->wifi_ssid, sizeof(snap->wifi_ssid), "%s", s_ssid);
    std::snprintf(snap->wifi_password, sizeof(snap->wifi_password), "%s",
                  s_pass);
    snap->wifi_ap_ssid[0] = '\0';
    snap->wifi_ap_password[0] = '\0';
  }
  unlock();
}
