#include "panel_ota.h"

#include "show_player.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

#include <cstdio>
#include <cstring>

#include "app_log.h"
#include "firmware_image.h"
#include "panel_core.h"
#include "upgrade_manager.h"

namespace {

constexpr uint32_t kPartitionTableOffset = 0x8000u;
constexpr size_t kPartitionTableSize = 0xC00u;

/* RTC_NOINIT lies outside the image and survives the rollback reset.  The
 * magic rejects random cold-boot contents. */
constexpr uint32_t kOtaNoteMagic = 0x4F54414Eu;  /* "OTAN" */
struct ota_note_t {
  uint32_t magic;
  char text[64];
};
RTC_NOINIT_ATTR static ota_note_t s_ota_note;

void set_note(const char *text) {
  s_ota_note.magic = kOtaNoteMagic;
  std::snprintf(s_ota_note.text, sizeof(s_ota_note.text), "%s",
                text ? text : "");
}

void set_status(char *out, unsigned len, const char *text) {
  if (out == nullptr || len == 0) return;
  std::snprintf(out, len, "%s", text != nullptr ? text : "");
  SS_LOGI("ota", "%s", out);
}

bool partition_table_sha(char out[65]) {
  uint8_t *buf = (uint8_t *)heap_caps_malloc(kPartitionTableSize,
                                             MALLOC_CAP_INTERNAL);
  if (buf == nullptr) return false;
  const esp_err_t err = esp_flash_read(esp_flash_default_chip, buf,
                                       kPartitionTableOffset,
                                       kPartitionTableSize);
  if (err != ESP_OK) {
    heap_caps_free(buf);
    return false;
  }
  rc_sha256_hex(buf, kPartitionTableSize, out);
  heap_caps_free(buf);
  return true;
}

int ota_write(void *ctx, const uint8_t *data, size_t len) {
  const esp_ota_handle_t handle = *(const esp_ota_handle_t *)ctx;
  return esp_ota_write(handle, data, len) == ESP_OK ? 0 : -1;
}

int ota_mark(void *ctx) {
  (void)ctx;
  return 0;
}

}  // namespace

extern "C" bool panel_ota_apply(const rc_manifest_t *m, const uint8_t *data,
                                size_t len, char *status,
                                unsigned status_len) {
  if (m == nullptr || data == nullptr || len == 0) {
    set_status(status, status_len, "bad panel bundle");
    return false;
  }
  const esp_partition_t *part = esp_ota_get_next_update_partition(nullptr);
  if (part == nullptr) {
    set_status(status, status_len, "no inactive OTA slot");
    return false;
  }

  char pt_sha[65];
  const bool have_pt = partition_table_sha(pt_sha);
  pc_ota_request_t req = {};
  req.show_running = show_player_running();
  req.queue_active = upgrade_manager_active();
  req.server_on = false;
  req.max_size = part->size;
  req.running_partition_sha256 = have_pt ? pt_sha : nullptr;

  const pc_ota_result_t check = pc_ota_check(&req, m, (uint32_t)len);
  if (check != PC_OTA_OK) {
    switch (check) {
      case PC_OTA_BUSY:
        set_status(status, status_len, "busy: stop the show/queue first");
        break;
      case PC_OTA_BAD_PARTITION:
        set_status(status, status_len, "partition table mismatch");
        break;
      case PC_OTA_TOO_LARGE:
        set_status(status, status_len, "image larger than OTA slot");
        break;
      case PC_OTA_BAD_MANIFEST:
        set_status(status, status_len, "bad panel manifest");
        break;
      default:
        set_status(status, status_len, "panel OTA refused");
        break;
    }
    return false;
  }

  esp_ota_handle_t handle = 0;
  esp_err_t err = esp_ota_begin(part, len, &handle);
  if (err != ESP_OK) {
    set_status(status, status_len, "esp_ota_begin failed");
    return false;
  }
  pc_ota_callbacks_t cb = {};
  cb.write = ota_write;
  cb.mark_valid = ota_mark;
  cb.ctx = &handle;
  const pc_ota_result_t r = pc_ota_run(&req, m, data, len, true, &cb);
  if (r != PC_OTA_OK) {
    esp_ota_abort(handle);
    set_status(status, status_len,
               r == PC_OTA_HASH_MISMATCH ? "SHA-256 mismatch"
                                         : "OTA write failed");
    return false;
  }
  err = esp_ota_end(handle);
  if (err != ESP_OK) {
    set_status(status, status_len, "esp_ota_end failed");
    return false;
  }
  err = esp_ota_set_boot_partition(part);
  if (err != ESP_OK) {
    set_status(status, status_len, "set boot partition failed");
    return false;
  }
  set_status(status, status_len, "panel updated; rebooting");
  return true;
}

extern "C" bool panel_ota_start(char *status, unsigned status_len) {
  const rc_manifest_t *m = firmware_image_panel();
  const char *path = firmware_image_panel_path();
  if (m == nullptr || path == nullptr) {
    set_status(status, status_len, "no panel image on SD");
    return false;
  }
  FILE *f = fopen(path, "rb");
  if (f == nullptr) {
    set_status(status, status_len, "cannot open panel image");
    return false;
  }
  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len <= 0 || (uint32_t)len != m->size) {
    fclose(f);
    set_status(status, status_len, "panel image size mismatch");
    return false;
  }
  uint8_t *data = (uint8_t *)heap_caps_malloc((size_t)len, MALLOC_CAP_SPIRAM);
  if (data == nullptr)
    data = (uint8_t *)heap_caps_malloc((size_t)len, MALLOC_CAP_INTERNAL);
  if (data == nullptr) {
    fclose(f);
    set_status(status, status_len, "no memory for panel image");
    return false;
  }
  const size_t got = fread(data, 1, (size_t)len, f);
  fclose(f);
  if (got != (size_t)len) {
    heap_caps_free(data);
    set_status(status, status_len, "short read");
    return false;
  }
  const bool ok = panel_ota_apply(m, data, (size_t)len, status, status_len);
  heap_caps_free(data);
  if (!ok) return false;
  vTaskDelay(pdMS_TO_TICKS(300));
  esp_restart();
  return true;
}

extern "C" void panel_ota_boot_check(bool display_ok, bool can_ok) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running == nullptr) return;
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) return;
  if (state != ESP_OTA_IMG_PENDING_VERIFY) return;
  if (display_ok && can_ok) {
    set_note(firmware_image_fly() ? "validated; node ready"
                                  : "validated; node missing");
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    SS_LOGI("ota", "pending image marked valid: %s", esp_err_to_name(err));
  } else {
    char note[64];
    std::snprintf(note, sizeof(note), "rollback: %s; node %s",
                  display_ok ? "CAN off" : "display",
                  firmware_image_fly() ? "ready" : "missing");
    set_note(note);
    SS_LOGE("ota", "self-test failed (display=%d can=%d); rolling back",
            (int)display_ok, (int)can_ok);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

extern "C" void panel_ota_set_note(const char *text) { set_note(text); }

extern "C" void panel_ota_last_note(char *out, unsigned len) {
  if (out == nullptr || len == 0) return;
  if (s_ota_note.magic != kOtaNoteMagic) {
    out[0] = '\0';
    return;
  }
  std::snprintf(out, len, "%s", s_ota_note.text);
}
