#include "katapult_client.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "app_log.h"
#include "app_watchdog.h"
#include "board_ws7.h"
#include "motion_core.h"

namespace {

constexpr uint32_t kAdminId = 0x3F0;
/* Katapult's admin SET_NODEID carries an 8-bit node byte.  Upstream decodes
 * the data pair as (node << 1) + 0x100, so 0x3E8/0x3E9 (node 372) cannot be
 * expressed.  Node 116 gives 0x1E8/0x1E9; both are invalid show IDs
 * (type 8 with field 30 and reserved type 9), so no show node accepts them. */
constexpr uint8_t kNodeByte = 116;
constexpr uint32_t kDataTxId = (kNodeByte << 1) + 0x100;
constexpr uint32_t kDataRxId = kDataTxId + 1;
constexpr uint32_t kAppStart = 0x10004000u;
constexpr uint32_t kAppEnd = 0x10200000u;
constexpr unsigned kBlock = 64;
constexpr unsigned kRxCap = 1024;

constexpr uint8_t kCmdConnect = 0x11;
constexpr uint8_t kCmdSendBlock = 0x12;
constexpr uint8_t kCmdEof = 0x13;
constexpr uint8_t kCmdRequestBlock = 0x14;
constexpr uint8_t kCmdComplete = 0x15;
constexpr uint8_t kCmdGetCanbusId = 0x16;

constexpr uint8_t kAckSuccess = 0xA0;
constexpr uint8_t kNack = 0xF1;
constexpr uint8_t kAckError = 0xF2;
constexpr uint8_t kAckBusy = 0xF3;

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
uint8_t s_rx[kRxCap];
unsigned s_rx_len = 0;

uint16_t crc16(const uint8_t *buf, unsigned len) {
  uint16_t crc = 0xFFFF;
  for (unsigned i = 0; i < len; ++i) {
    uint8_t data = (uint8_t)(buf[i] ^ (crc & 0xFF));
    data ^= (uint8_t)((data & 0x0F) << 4);
    crc = (uint16_t)(((data << 8) | (crc >> 8)) ^ (data >> 4) ^ (data << 3));
  }
  return crc;
}

void rx_clear(void) {
  portENTER_CRITICAL(&s_lock);
  s_rx_len = 0;
  portEXIT_CRITICAL(&s_lock);
}

unsigned rx_copy(uint8_t *out, unsigned cap) {
  portENTER_CRITICAL(&s_lock);
  unsigned n = s_rx_len < cap ? s_rx_len : cap;
  memcpy(out, s_rx, n);
  portEXIT_CRITICAL(&s_lock);
  return n;
}

void rx_consume(unsigned n) {
  portENTER_CRITICAL(&s_lock);
  if (n >= s_rx_len) {
    s_rx_len = 0;
  } else {
    memmove(s_rx, s_rx + n, s_rx_len - n);
    s_rx_len -= n;
  }
  portEXIT_CRITICAL(&s_lock);
}

bool send_frame_retry(const ss_can_frame_t &frame, unsigned tries) {
  for (unsigned i = 0; i < tries; ++i) {
    if (can_transport_send(&frame)) return true;
    vTaskDelay(pdMS_TO_TICKS(1));
    app_watchdog_kick(SS_WATCH_UI | SS_WATCH_CAN);
  }
  return false;
}

bool send_stream(const uint8_t *data, unsigned len) {
  unsigned off = 0;
  do {
    ss_can_frame_t f;
    memset(&f, 0, sizeof(f));
    f.id = kDataTxId;
    f.len = (uint8_t)((len - off) > 8 ? 8 : (len - off));
    if (f.len) memcpy(f.data, data + off, f.len);
    if (!send_frame_retry(f, 50)) return false;
    off += f.len;
    app_watchdog_kick(SS_WATCH_UI | SS_WATCH_CAN);
  } while (off < len);
  return true;
}

unsigned build_command(uint8_t cmd, const uint8_t *payload, unsigned plen,
                       uint8_t *out, unsigned cap) {
  if (plen % 4 != 0 || cap < plen + 8) return 0;
  out[0] = 0x01;
  out[1] = 0x88;
  out[2] = cmd;
  out[3] = (uint8_t)(plen / 4);
  if (plen) memcpy(out + 4, payload, plen);
  uint16_t crc = crc16(out + 2, 2 + plen);
  out[4 + plen] = (uint8_t)(crc & 0xFF);
  out[5 + plen] = (uint8_t)(crc >> 8);
  out[6 + plen] = 0x99;
  out[7 + plen] = 0x03;
  return plen + 8;
}

/* Returns 0 on success, 1 on timeout/NACK, 2 on busy, -1 on protocol error. */
int wait_response(uint8_t cmd, uint8_t *ret, unsigned cap, unsigned *ret_len,
                  uint32_t timeout_ms) {
  const uint32_t start = millis();
  uint8_t buf[kRxCap];
  while ((uint32_t)(millis() - start) < timeout_ms) {
    app_watchdog_kick(SS_WATCH_UI | SS_WATCH_CAN);
    vTaskDelay(pdMS_TO_TICKS(2));
    unsigned n = rx_copy(buf, sizeof(buf));
    if (n < 4) continue;
    unsigned hdr = 0;
    while (hdr + 1 < n && !(buf[hdr] == 0x01 && buf[hdr + 1] == 0x88)) ++hdr;
    if (hdr > 0) {
      rx_consume(hdr);
      continue;
    }
    unsigned total = (unsigned)buf[3] * 4 + 8;
    if (total < 8 || total > sizeof(buf)) {
      rx_consume(1);
      continue;
    }
    if (n < total) continue;
    if (buf[total - 2] != 0x99 || buf[total - 1] != 0x03) {
      rx_consume(1);
      continue;
    }
    uint16_t got = (uint16_t)(buf[total - 4] | ((uint16_t)buf[total - 3] << 8));
    uint16_t calc = crc16(buf + 2, total - 6);
    uint8_t ack = buf[2];
    uint32_t cmd_resp = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                        ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
    rx_consume(total);
    if (got != calc) return -1;
    if (ack == kAckBusy) return 2;
    if (ack == kAckError) return -1;
    if (ack != kAckSuccess || (cmd_resp & 0xFF) != cmd) return 1;
    unsigned payload = total - 12;   /* after header, ack, words, cmd, crc */
    if (payload > cap) payload = cap;
    if (payload && ret) memcpy(ret, buf + 8, payload);
    if (ret_len) *ret_len = payload;
    return 0;
  }
  return 1;
}

bool send_command(uint8_t cmd, const uint8_t *payload, unsigned plen,
                  uint8_t *ret, unsigned cap, unsigned *ret_len,
                  uint32_t timeout_ms) {
  uint8_t out[96];
  unsigned n = build_command(cmd, payload, plen, out, sizeof(out));
  if (n == 0) return false;
  for (int attempt = 0; attempt < 3; ++attempt) {
    rx_clear();
    if (!send_stream(out, n)) continue;
    int r = wait_response(cmd, ret, cap, ret_len, timeout_ms);
    if (r == 0) return true;
    if (r == 2) vTaskDelay(pdMS_TO_TICKS(1500));
    else vTaskDelay(pdMS_TO_TICKS(300));
    SS_LOGW("katapult", "cmd 0x%02x attempt %d failed (%d)", cmd, attempt + 1,
            r);
  }
  return false;
}

bool image_ok(const uint8_t *image, uint32_t size) {
  if (image == nullptr || size < 8) return false;
  uint32_t sp = (uint32_t)image[0] | ((uint32_t)image[1] << 8) |
                ((uint32_t)image[2] << 16) | ((uint32_t)image[3] << 24);
  uint32_t reset = (uint32_t)image[4] | ((uint32_t)image[5] << 8) |
                   ((uint32_t)image[6] << 16) | ((uint32_t)image[7] << 24);
  return sp >= 0x20000000u && sp <= 0x20042000u &&
         reset >= kAppStart && reset < kAppEnd;
}

void set_status(char *status, unsigned len, const char *text) {
  if (status == nullptr || len == 0) return;
  strncpy(status, text, len - 1);
  status[len - 1] = '\0';
  SS_LOGI("katapult", "%s", text);
}

}  // namespace

void katapult_on_frame(const ss_can_frame_t *frame) {
  if (frame == nullptr || frame->id != kDataRxId) return;
  portENTER_CRITICAL(&s_lock);
  unsigned room = kRxCap - s_rx_len;
  unsigned n = frame->len < room ? frame->len : room;
  if (n) memcpy(s_rx + s_rx_len, frame->data, n);
  s_rx_len += n;
  portEXIT_CRITICAL(&s_lock);
}

bool katapult_flash(int board_id, const uint8_t uuid[6],
                    const uint8_t *image, uint32_t size,
                    char *status, unsigned status_len) {
  if (board_id < 0 || board_id >= MC_BOARD_COUNT || uuid == nullptr) {
    set_status(status, status_len, "bad board or UUID");
    return false;
  }
  if (!image_ok(image, size)) {
    set_status(status, status_len, "image is not a Katapult payload");
    return false;
  }

  /* The Katapult session is CAN only. Make sure the CH422G mux is on the
   * CAN side even if a previous action left it on USB. */
  board_ws7_set_usb_mode(false);
  vTaskDelay(pdMS_TO_TICKS(5));

  set_status(status, status_len, "enter bootloader");
  mc_command_t enter = {};
  enter.action = MC_CMD_ENTER_BOOTLOADER;
  uint8_t payload[8];
  mc_pack_command(&enter, payload);
  ss_can_frame_t f;
  memset(&f, 0, sizeof(f));
  f.id = (uint32_t)mc_id_make(board_id, 4);   /* type 4 is COMMAND */
  f.len = 8;
  memcpy(f.data, payload, 8);
  send_frame_retry(f, 50);
  vTaskDelay(pdMS_TO_TICKS(1000));

  set_status(status, status_len, "SET_NODEID");
  uint8_t admin[8] = {0};
  admin[0] = 0x11;
  memcpy(admin + 1, uuid, 6);
  admin[7] = kNodeByte;
  for (int i = 0; i < 3; ++i) {
    ss_can_frame_t a;
    memset(&a, 0, sizeof(a));
    a.id = kAdminId;
    a.len = 8;
    memcpy(a.data, admin, 8);
    send_frame_retry(a, 20);
    vTaskDelay(pdMS_TO_TICKS(30));
  }

  set_status(status, status_len, "CONNECT");
  uint8_t ret[128];
  unsigned ret_len = 0;
  if (!send_command(kCmdConnect, nullptr, 0, ret, sizeof(ret), &ret_len,
                    3000) ||
      ret_len < 12) {
    set_status(status, status_len, "CONNECT failed");
    return false;
  }
  uint32_t start = (uint32_t)ret[4] | ((uint32_t)ret[5] << 8) |
                   ((uint32_t)ret[6] << 16) | ((uint32_t)ret[7] << 24);
  uint32_t block = (uint32_t)ret[8] | ((uint32_t)ret[9] << 8) |
                   ((uint32_t)ret[10] << 16) | ((uint32_t)ret[11] << 24);
  char mcu[32];
  unsigned mcu_len = ret_len > 12 ? ret_len - 12 : 0;
  if (mcu_len >= sizeof(mcu)) mcu_len = sizeof(mcu) - 1;
  memcpy(mcu, ret + 12, mcu_len);
  mcu[mcu_len] = '\0';
  if (start != kAppStart || block != kBlock || strncmp(mcu, "rp2040", 6) != 0) {
    SS_LOGE("katapult", "bad CONNECT: start=0x%lx block=%lu mcu='%s'",
            (unsigned long)start, (unsigned long)block, mcu);
    set_status(status, status_len, "wrong bootloader or target");
    return false;
  }

  set_status(status, status_len, "GET_CANBUS_ID");
  if (!send_command(kCmdGetCanbusId, nullptr, 0, ret, sizeof(ret), &ret_len,
                    2000) ||
      ret_len < 6 || memcmp(ret, uuid, 6) != 0) {
    set_status(status, status_len, "UUID mismatch; not writing");
    return false;
  }

  const uint32_t blocks = (size + kBlock - 1) / kBlock;
  set_status(status, status_len, "WRITING");
  for (uint32_t i = 0; i < blocks; ++i) {
    uint8_t sb[4 + kBlock];
    uint32_t addr = kAppStart + i * kBlock;
    sb[0] = (uint8_t)addr;
    sb[1] = (uint8_t)(addr >> 8);
    sb[2] = (uint8_t)(addr >> 16);
    sb[3] = (uint8_t)(addr >> 24);
    uint32_t remaining = size - i * kBlock;
    uint32_t take = remaining < kBlock ? remaining : kBlock;
    memcpy(sb + 4, image + i * kBlock, take);
    if (take < kBlock) memset(sb + 4 + take, 0xFF, kBlock - take);
    if (!send_command(kCmdSendBlock, sb, sizeof(sb), ret, sizeof(ret), &ret_len,
                      6000) ||
        ret_len < 4) {
      set_status(status, status_len, "block write failed");
      return false;
    }
    uint32_t got = (uint32_t)ret[0] | ((uint32_t)ret[1] << 8) |
                   ((uint32_t)ret[2] << 16) | ((uint32_t)ret[3] << 24);
    if (got != addr) {
      set_status(status, status_len, "block address mismatch");
      return false;
    }
    if ((i & 0x1F) == 0) {
      SS_LOGI("katapult", "write %lu/%lu", (unsigned long)(i + 1),
              (unsigned long)blocks);
      app_watchdog_kick(SS_WATCH_UI | SS_WATCH_CAN);
    }
  }

  set_status(status, status_len, "SEND_EOF");
  if (!send_command(kCmdEof, nullptr, 0, ret, sizeof(ret), &ret_len, 4000)) {
    set_status(status, status_len, "EOF failed");
    return false;
  }

  set_status(status, status_len, "VERIFYING");
  for (uint32_t i = 0; i < blocks; ++i) {
    uint8_t rb[4];
    uint32_t addr = kAppStart + i * kBlock;
    rb[0] = (uint8_t)addr;
    rb[1] = (uint8_t)(addr >> 8);
    rb[2] = (uint8_t)(addr >> 16);
    rb[3] = (uint8_t)(addr >> 24);
    if (!send_command(kCmdRequestBlock, rb, 4, ret, sizeof(ret), &ret_len,
                      3000) ||
        ret_len < 4 + kBlock) {
      set_status(status, status_len, "read-back failed");
      return false;
    }
    uint32_t got = (uint32_t)ret[0] | ((uint32_t)ret[1] << 8) |
                   ((uint32_t)ret[2] << 16) | ((uint32_t)ret[3] << 24);
    if (got != addr) {
      set_status(status, status_len, "read-back address mismatch");
      return false;
    }
    uint32_t remaining = size - i * kBlock;
    uint32_t take = remaining < kBlock ? remaining : kBlock;
    if (memcmp(ret + 4, image + i * kBlock, take) != 0) {
      set_status(status, status_len, "read-back data mismatch");
      return false;
    }
    for (uint32_t b = take; b < kBlock; ++b) {
      if (ret[4 + b] != 0xFF) {
        set_status(status, status_len, "read-back padding mismatch");
        return false;
      }
    }
    if ((i & 0x1F) == 0)
      app_watchdog_kick(SS_WATCH_UI | SS_WATCH_CAN);
  }

  set_status(status, status_len, "COMPLETE");
  send_command(kCmdComplete, nullptr, 0, ret, sizeof(ret), &ret_len, 3000);
  vTaskDelay(pdMS_TO_TICKS(1500));
  set_status(status, status_len, "flash complete");
  return true;
}
