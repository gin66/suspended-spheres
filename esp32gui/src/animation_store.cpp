#include "animation_store.h"

#include <Arduino.h>

#include "app_log.h"
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr const char *kAnimationDir = "/sdcard/animations";
constexpr int kMaxFiles = UI_MAX_ANIMATIONS;

ui_animation_t s_items[kMaxFiles];
int s_count = 0;
char s_status[64] = "no SD card";
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_refresh_request = false;

/* HBP2: 4-byte magic, 2-byte version, 2-byte JSON header length, then the
 * self-describing header.  Only duration and size are needed for the library;
 * playback still belongs to the master. */
bool read_meta(const char *path, uint32_t file_size, uint32_t *duration_ms) {
  *duration_ms = 0;
  FILE *f = std::fopen(path, "rb");
  if (f == nullptr) return false;
  uint8_t head[8];
  if (std::fread(head, 1, sizeof(head), f) != sizeof(head) ||
      std::memcmp(head, "HBP2", 4) != 0) {
    std::fclose(f);
    return false;
  }
  const unsigned hlen = (unsigned)head[6] | ((unsigned)head[7] << 8);
  if (hlen == 0 || hlen > 2048) {
    std::fclose(f);
    return false;
  }
  char text[2049];
  if (std::fread(text, 1, hlen, f) != hlen) {
    std::fclose(f);
    return false;
  }
  std::fclose(f);
  text[hlen] = '\0';
  const char *p = std::strstr(text, "\"duration_ms\":");
  if (p == nullptr) p = std::strstr(text, "\"playback_ms\":");
  if (p != nullptr) {
    p = std::strchr(p, ':');
    if (p != nullptr) {
      char *end = nullptr;
      const unsigned long v = std::strtoul(p + 1, &end, 10);
      if (end != p + 1) *duration_ms = (uint32_t)v;
    }
  }
  (void)file_size;
  return *duration_ms > 0;
}

int compare_name(const void *a, const void *b) {
  const ui_animation_t *aa = static_cast<const ui_animation_t *>(a);
  const ui_animation_t *bb = static_cast<const ui_animation_t *>(b);
  return strcasecmp(aa->name, bb->name);
}

}  // namespace

void animation_store_refresh(void) {
  ui_animation_t *local =
      (ui_animation_t *)std::malloc(sizeof(ui_animation_t) * kMaxFiles);
  if (local == nullptr) return;
  int count = 0;
  char status[64] = "no SD card";
  DIR *d = opendir(kAnimationDir);
  if (d == nullptr) {
    portENTER_CRITICAL(&s_mux);
    s_count = 0;
    std::snprintf(s_status, sizeof(s_status), "%s", status);
    portEXIT_CRITICAL(&s_mux);
    std::free(local);
    return;
  }
  struct dirent *ent;
  while ((ent = readdir(d)) != nullptr && count < kMaxFiles) {
    const size_t n = std::strlen(ent->d_name);
    if (n < 4 || strcasecmp(ent->d_name + n - 4, ".hbp") != 0) continue;
    char path[160];
    std::snprintf(path, sizeof(path), "%s/%s", kAnimationDir, ent->d_name);
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0) continue;
    uint32_t duration = 0;
    const bool valid = read_meta(path, (uint32_t)st.st_size, &duration);
    ui_animation_t *a = &local[count];
    std::memset(a, 0, sizeof(*a));
    std::snprintf(a->name, sizeof(a->name), "%s", ent->d_name);
    a->size_bytes = (uint32_t)st.st_size;
    a->duration_ms = duration;
    a->valid = valid;
    ++count;
  }
  closedir(d);
  if (count > 1) qsort(local, count, sizeof(local[0]), compare_name);
  std::snprintf(status, sizeof(status), "%d animation%s on SD", count,
                count == 1 ? "" : "s");
  portENTER_CRITICAL(&s_mux);
  std::memcpy(s_items, local, sizeof(s_items[0]) * (size_t)count);
  s_count = count;
  std::snprintf(s_status, sizeof(s_status), "%s", status);
  portEXIT_CRITICAL(&s_mux);
  std::free(local);
  SS_LOGI("anim", "%d animation%s on SD", count, count == 1 ? "" : "s");
}

void animation_store_request_refresh(void) { s_refresh_request = true; }

void animation_store_poll(void) {
  if (s_refresh_request) {
    s_refresh_request = false;
    animation_store_refresh();
  }
}

void animation_store_init(void) {
  mkdir(kAnimationDir, 0777);
  animation_store_refresh();
}

int animation_store_count(void) {
  portENTER_CRITICAL(&s_mux);
  const int n = s_count;
  portEXIT_CRITICAL(&s_mux);
  return n;
}

bool animation_store_get_name(int index, char *out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  portENTER_CRITICAL(&s_mux);
  const bool ok = index >= 0 && index < s_count;
  if (ok) std::snprintf(out, cap, "%s", s_items[index].name);
  portEXIT_CRITICAL(&s_mux);
  return ok;
}

void animation_store_snapshot(int *count, ui_animation_t *dst, int cap,
                              char *status, size_t status_cap) {
  portENTER_CRITICAL(&s_mux);
  const int n = s_count < cap ? s_count : cap;
  if (count) *count = n;
  if (dst) std::memcpy(dst, s_items, sizeof(s_items[0]) * (size_t)n);
  if (status && status_cap)
    std::snprintf(status, status_cap, "%s", s_status);
  portEXIT_CRITICAL(&s_mux);
}
