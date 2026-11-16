#include "panel_console.h"

#include <Arduino.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board_ws7.h"
#include "can_transport.h"
#include "commission.h"
#include "config_store.h"
#include "firmware_image.h"
#include "katapult_client.h"
#include "motion_core.h"
#include "panel_ota.h"
#include "upgrade_manager.h"

#ifdef SS_BENCH_CONSOLE

namespace {

char s_line[160];
unsigned s_len = 0;

int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parse_uuid(const char *s, uint8_t uuid[6]) {
  if (s == nullptr || strlen(s) != 12) return false;
  for (int i = 0; i < 6; ++i) {
    int hi = hex_nibble(s[i * 2]);
    int lo = hex_nibble(s[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    uuid[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

int node_index_for_board(int board_id) {
  ss_node_t n;
  for (int i = 0; i < commission_node_count(); ++i) {
    if (commission_get_node(i, &n) && n.used && n.board_id == board_id)
      return i;
  }
  return -1;
}

void print_help(void) {
  Serial.println("panel commands:");
  Serial.println("  status                      nodes, build and image size");
  Serial.println("  query                       VERSION_REQ to every board");
  Serial.println("  jump <board>                COMMAND action 4 only");
  Serial.println("  flash <board> <uuidhex>     Katapult the embedded Fly image");
  Serial.println("  upgrade all|sel             Run the fleet queue from the embedded image");
  Serial.println("  upstatus                    Queue state and last result");
  Serial.println("  panelota                    Write the SD panel image and reboot");
  Serial.println("  image                       embedded Fly image size");
  Serial.println("  can                         USB_SEL/CAN state and counters");
  Serial.println("  canon                       force USB_SEL to CAN and print state");
  Serial.println("  canstop                     stop TWAI and route USB_SEL to USB");
}

void print_status(void) {
  const ss_fw_image_t *img = firmware_image_fly();
  Serial.printf("nodes=%d image=%u bytes can=%s\n", commission_node_count(),
                img ? (unsigned)img->size : 0u,
                can_transport_running() ? "on" : "off");
  ss_node_t n;
  for (int i = 0; i < commission_node_count(); ++i) {
    if (!commission_get_node(i, &n) || !n.used) continue;
    char uuid[16];
    snprintf(uuid, sizeof(uuid), "%02x%02x%02x%02x%02x%02x",
             n.uuid[0], n.uuid[1], n.uuid[2], n.uuid[3], n.uuid[4],
             n.uuid[5]);
    Serial.printf("  board=%d uuid=%s online=%d version=%s build=%lu dirty=%d\n",
                  n.board_id, uuid,
                  (uint32_t)(millis() - n.last_seen_ms) <= 10000u ? 1 : 0,
                  n.version_seen ? n.sw_version : "--",
                  (unsigned long)n.current_build, (n.version_flags & 1) ? 1 : 0);
  }
  const ss_config_t *cfg = config_store_get();
  for (int i = 0; i < SS_MAX_BOARDS; ++i) {
    if (cfg->boards[i].board_id < 0) continue;
    char uuid[16];
    snprintf(uuid, sizeof(uuid), "%02x%02x%02x%02x%02x%02x",
             cfg->boards[i].uuid[0], cfg->boards[i].uuid[1],
             cfg->boards[i].uuid[2], cfg->boards[i].uuid[3],
             cfg->boards[i].uuid[4], cfg->boards[i].uuid[5]);
    Serial.printf("  config board=%d uuid=%s build=%lu result=%s\n",
                  cfg->boards[i].board_id, uuid,
                  (unsigned long)cfg->boards[i].last_build,
                  cfg->boards[i].last_result);
  }
}

bool send_enter(int board) {
  if (board < 0 || board >= MC_BOARD_COUNT) return false;
  mc_command_t enter;
  memset(&enter, 0, sizeof(enter));
  enter.action = MC_CMD_ENTER_BOOTLOADER;
  uint8_t payload[8];
  if (mc_pack_command(&enter, payload) != 0) return false;
  ss_can_frame_t f;
  memset(&f, 0, sizeof(f));
  f.id = (uint32_t)mc_id_make(board, 4);
  f.len = 8;
  memcpy(f.data, payload, 8);
  return can_transport_send(&f);
}

void command_flash(char *args) {
  if (upgrade_manager_active()) {
    Serial.println("upgrade queue is running; wait or stop it");
    return;
  }
  char *board_s = strtok(args, " ");
  char *uuid_s = strtok(nullptr, " ");
  if (board_s == nullptr) {
    Serial.println("usage: flash <board> <uuidhex>");
    return;
  }
  const int board = atoi(board_s);
  uint8_t uuid[6];
  if (uuid_s != nullptr) {
    if (!parse_uuid(uuid_s, uuid)) {
      Serial.println("bad uuid; expected 12 hex characters");
      return;
    }
  } else {
    int idx = node_index_for_board(board);
    ss_node_t n;
    const ss_board_map_t *b = nullptr;
    const ss_config_t *cfg = config_store_get();
    for (int i = 0; i < SS_MAX_BOARDS; ++i)
      if (cfg->boards[i].board_id == board) b = &cfg->boards[i];
    if (idx >= 0 && commission_get_node(idx, &n)) {
      memcpy(uuid, n.uuid, 6);
    } else if (b != nullptr) {
      memcpy(uuid, b->uuid, 6);
    } else {
      Serial.println("unknown UUID; pass it explicitly");
      return;
    }
  }
  const ss_fw_image_t *img = firmware_image_fly();
  if (img == nullptr || img->data == nullptr || img->size == 0) {
    Serial.println("no Fly image; insert SD or rebuild with SS_FLY_IMAGE=...");
    return;
  }
  char status[80] = "idle";
  Serial.printf("flashing board %d, %u bytes from %s...\n", board,
                (unsigned)img->size, img->path);
  const bool ok = katapult_flash(board, uuid, img->data, img->size,
                                 status, sizeof(status));
  Serial.printf("flash %s: %s\n", ok ? "OK" : "FAILED", status);
}

void execute(char *line) {
  char *cmd = strtok(line, " ");
  if (cmd == nullptr) return;
  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
    print_help();
  } else if (strcmp(cmd, "status") == 0) {
    print_status();
  } else if (strcmp(cmd, "query") == 0) {
    commission_query_versions();
    Serial.println("version query sent");
  } else if (strcmp(cmd, "can") == 0) {
    bool can = false;
    const esp_err_t e = board_ws7_read_can_available(&can);
    ss_can_stats_t st;
    can_transport_get_stats(&st);
    Serial.printf("can_mux=%d (read %s) shadow=%d running=%d st=%d tec=%lu rec=%lu rx=%lu tx=%lu err=%lu\n",
                  can ? 1 : 0, esp_err_to_name(e),
                  board_ws7_can_available() ? 1 : 0, can_transport_running() ? 1 : 0,
                  st.state, (unsigned long)st.tec, (unsigned long)st.rec,
                  (unsigned long)st.rx_frames, (unsigned long)st.tx_frames,
                  (unsigned long)st.bus_errors);
  } else if (strcmp(cmd, "canon") == 0) {
    const esp_err_t e = board_ws7_set_usb_mode(false);
    vTaskDelay(pdMS_TO_TICKS(5));
    bool can = false;
    board_ws7_read_can_available(&can);
    Serial.printf("USB_SEL->CAN: write %s, read can_mux=%d shadow=%d\n",
                  esp_err_to_name(e), can ? 1 : 0,
                  board_ws7_can_available() ? 1 : 0);
  } else if (strcmp(cmd, "canstop") == 0) {
    can_transport_stop();
    Serial.println("CAN stopped");
  } else if (strcmp(cmd, "upgrade") == 0) {
    char *args = strtok(nullptr, " ");
    if (args != nullptr && strcmp(args, "all") == 0) {
      Serial.println(upgrade_manager_start_all() ? "upgrade started"
                                                 : "upgrade refused");
    } else if (args != nullptr && (strcmp(args, "sel") == 0 ||
                                   strcmp(args, "selected") == 0)) {
      Serial.println(upgrade_manager_start_selected() ? "upgrade started"
                                                      : "upgrade refused");
    } else {
      Serial.println("usage: upgrade all|sel");
    }
  } else if (strcmp(cmd, "panelota") == 0) {
    char text[80] = "idle";
    if (!panel_ota_start(text, sizeof(text))) Serial.println(text);
  } else if (strcmp(cmd, "upstatus") == 0) {
    char text[96];
    upgrade_manager_status(text, sizeof(text));
    Serial.println(text);
  } else if (strcmp(cmd, "image") == 0) {
    const ss_fw_image_t *img = firmware_image_fly();
    if (img != nullptr)
      Serial.printf("fly image: %s %s build %lu, %u bytes\n", img->source,
                    img->version, (unsigned long)img->build,
                    (unsigned)img->size);
    else
      Serial.println("no fly image");
  } else if (strcmp(cmd, "flash") == 0) {
    char *args = strtok(nullptr, "");
    command_flash(args ? args : (char *)"");
  } else if (strcmp(cmd, "jump") == 0) {
    char *board_s = strtok(nullptr, " ");
    if (board_s == nullptr) {
      Serial.println("usage: jump <board>");
      return;
    }
    Serial.println(send_enter(atoi(board_s)) ? "enter sent"
                                              : "enter failed");
  } else {
    Serial.println("unknown command; try help");
  }
}

}  // namespace

extern "C" void panel_console_begin(void) { s_len = 0; }

extern "C" void panel_console_poll(void) {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
      s_line[s_len] = '\0';
      if (s_len > 0) execute(s_line);
      s_len = 0;
      continue;
    }
    if (s_len + 1 < sizeof(s_line)) s_line[s_len++] = (char)c;
  }
}

#else

extern "C" void panel_console_begin(void) {}
extern "C" void panel_console_poll(void) {}

#endif /* SS_BENCH_CONSOLE */
