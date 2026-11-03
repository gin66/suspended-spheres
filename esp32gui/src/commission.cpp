#include "commission.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

#include <cstdio>
#include <cstring>

#include "app_log.h"
#include "motion_core.h"

namespace {

enum : int {
  kTypeMotion = 0,
  kTypeSync = 2,
  kTypeStatus = 3,
  kTypeCommand = 4,
  kTypeHome = 5,
  kTypeFault = 7,
  kTypeDisc = 8,
  kTypeAssign = 10,
  kTypeAsk = 11,
  kTypeVersionReq = MC_TYPE_VERSION_REQ,
  kTypeVersionRsp = MC_TYPE_VERSION_RSP,
};

enum : uint8_t {
  kCmdStop = 3,
  kCmdUp = 1,
  kCmdDown = 2,
};

constexpr uint32_t kAskPeriodMs = 5000u;
constexpr uint32_t kOnlineWindowMs = 10000u;

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
ss_node_t s_nodes[SS_MAX_BOARDS];
int s_count = 0;
ss_board_map_t s_known[SS_MAX_BOARDS];
bool s_persist_dirty = false;
uint32_t s_last_ask_ms = 0;
uint8_t s_cmd_seq = 0;
int s_selected = 0;
ss_commission_stats_t s_stats;

bool s_test_active = false;
int s_test_order[SS_MAX_POSITIONS];
int s_test_count = 0;
int s_test_index = 0;
uint32_t s_test_next_ms = 0;

void put_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)(v >> 8);
}

int find_node_locked(const uint8_t flash_id[SS_FLASH_ID_LEN]) {
  for (int i = 0; i < s_count; ++i) {
    if (s_nodes[i].used &&
        std::memcmp(s_nodes[i].flash_id, flash_id, SS_FLASH_ID_LEN) == 0) {
      return i;
    }
  }
  return -1;
}

int find_board_locked(int board_id) {
  for (int i = 0; i < s_count; ++i) {
    if (s_nodes[i].used && s_nodes[i].board_id == board_id) return i;
  }
  return -1;
}

int known_lookup_locked(const uint8_t flash_id[SS_FLASH_ID_LEN]) {
  for (const auto &k : s_known) {
    if (k.board_id >= 0 &&
        std::memcmp(k.flash_id, flash_id, SS_FLASH_ID_LEN) == 0) {
      return k.board_id;
    }
  }
  return -1;
}

bool board_id_used_locked(int id) {
  for (const auto &k : s_known) {
    if (k.board_id == id) return true;
  }
  for (int i = 0; i < s_count; ++i) {
    if (s_nodes[i].used && s_nodes[i].board_id == id) return true;
  }
  return false;
}

int free_board_id_locked(void) {
  for (int id = 0; id < SS_MAX_BOARDS; ++id) {
    if (!board_id_used_locked(id)) return id;
  }
  return -1;
}

void known_add_locked(const uint8_t flash_id[SS_FLASH_ID_LEN], int board_id) {
  for (auto &k : s_known) {
    if (k.board_id >= 0 &&
        std::memcmp(k.flash_id, flash_id, SS_FLASH_ID_LEN) == 0) {
      k.board_id = (int8_t)board_id;
      return;
    }
  }
  for (auto &k : s_known) {
    if (k.board_id < 0) {
      std::memcpy(k.flash_id, flash_id, SS_FLASH_ID_LEN);
      k.board_id = (int8_t)board_id;
      return;
    }
  }
}

void send_ask(void) {
  ss_can_frame_t f;
  std::memset(&f, 0, sizeof(f));
  f.id = (uint32_t)mc_id_make(0, kTypeAsk);
  f.len = 8;
  mc_pack_ask(f.data);
  if (can_transport_send(&f)) {
    s_stats.ask_count++;
  } else {
    s_stats.tx_errors++;
  }
}

void send_version_req(void) {
  ss_can_frame_t f;
  std::memset(&f, 0, sizeof(f));
  f.id = (uint32_t)mc_id_make(MC_BOARD_ALL, kTypeVersionReq);
  f.len = 8;
  mc_pack_version_req(f.data);
  if (!can_transport_send(&f)) s_stats.tx_errors++;
}

void send_assign(int board_id, const uint8_t flash_id[SS_FLASH_ID_LEN]) {
  ss_can_frame_t f;
  std::memset(&f, 0, sizeof(f));
  f.id = (uint32_t)mc_id_make(board_id, kTypeAssign);
  f.len = 8;
  mc_pack_flash_id(flash_id, f.data);
  if (can_transport_send(&f)) {
    s_stats.assign_count++;
    SS_LOGI("commission", "ASSIGN board %d flash %02x%02x%02x%02x%02x%02x%02x%02x",
            board_id, flash_id[0], flash_id[1], flash_id[2], flash_id[3],
            flash_id[4], flash_id[5], flash_id[6], flash_id[7]);
  } else {
    s_stats.tx_errors++;
    SS_LOGW("commission", "ASSIGN board %d send failed", board_id);
  }
}

void handle_disc(const ss_can_frame_t *frame) {
  uint8_t flash_id[SS_FLASH_ID_LEN];
  if (mc_unpack_flash_id(frame->data, flash_id) != 0) return;

  int assign_board = -1;
  portENTER_CRITICAL(&s_lock);
  int idx = find_node_locked(flash_id);
  if (idx < 0 && s_count < SS_MAX_BOARDS) {
    idx = s_count++;
    std::memset(&s_nodes[idx], 0, sizeof(s_nodes[idx]));
    s_nodes[idx].used = true;
    s_nodes[idx].board_id = -1;
    std::memcpy(s_nodes[idx].flash_id, flash_id, SS_FLASH_ID_LEN);
  }
  if (idx < 0) {
    portEXIT_CRITICAL(&s_lock);
    SS_LOGW("commission", "node table full; DISC ignored");
    return;
  }

  ss_node_t *node = &s_nodes[idx];
  node->last_seen_ms = frame->timestamp_ms;
  node->advertised = true;
  s_stats.disc_count++;

  /* A board that answers DISC has no live board ID, so always (re)assign.
   * A known flash ID keeps the ID it had in the host map. */
  int board_id = known_lookup_locked(flash_id);
  if (board_id < 0) board_id = free_board_id_locked();
  if (board_id >= 0) {
    node->board_id = board_id;
    known_add_locked(flash_id, board_id);
    s_persist_dirty = true;
    assign_board = board_id;
  }
  portEXIT_CRITICAL(&s_lock);

  if (assign_board >= 0) {
    send_assign(assign_board, flash_id);
  } else {
    SS_LOGE("commission", "no free board ID for a discovered node");
  }
}

void handle_status(const ss_can_frame_t *frame) {
  const int board_id = mc_id_field((int)frame->id);
  if (board_id < 0 || board_id >= SS_MAX_BOARDS) return;

  mc_status_t st;
  if (mc_unpack_status(frame->data, &st) != 0) return;

  portENTER_CRITICAL(&s_lock);
  const int idx = find_board_locked(board_id);
  if (idx >= 0) {
    ss_node_t *node = &s_nodes[idx];
    node->last_seen_ms = frame->timestamp_ms;
    node->last_status_ms = frame->timestamp_ms;
    node->status_seen = true;
    node->state = st.state;
    node->fault_flags = st.fault_flags;
    std::memcpy(node->following_error, st.following_error,
                sizeof(node->following_error));
    node->buffer_depth = st.buffer_depth;
    node->last_seq = st.last_seq;
  }
  portEXIT_CRITICAL(&s_lock);
}

void handle_version(const ss_can_frame_t *frame) {
  const int board_id = mc_id_field((int)frame->id);
  if (board_id < 0 || board_id >= SS_MAX_BOARDS) return;

  mc_version_t v;
  if (mc_unpack_version_rsp(frame->data, &v) != 0) return;

  portENTER_CRITICAL(&s_lock);
  const int idx = find_board_locked(board_id);
  if (idx >= 0) {
    ss_node_t *node = &s_nodes[idx];
    node->version_seen = true;
    node->last_version_ms = frame->timestamp_ms;
    if ((v.flags & MC_VERSION_FLAG_DIRTY) != 0) {
      std::snprintf(node->sw_version, sizeof(node->sw_version),
                    "%u.%u.%u-dirty", (unsigned)v.major, (unsigned)v.minor,
                    (unsigned)v.patch);
    } else {
      std::snprintf(node->sw_version, sizeof(node->sw_version), "%u.%u.%u",
                    (unsigned)v.major, (unsigned)v.minor, (unsigned)v.patch);
    }
  }
  portEXIT_CRITICAL(&s_lock);
}

void recompute_stats(void) {
  portENTER_CRITICAL(&s_lock);
  const uint32_t now = millis();
  uint32_t discovered = 0;
  uint32_t assigned_boards = 0;
  uint32_t online = 0;
  for (int i = 0; i < s_count; ++i) {
    if (!s_nodes[i].used) continue;
    discovered++;
    if (s_nodes[i].board_id >= 0) assigned_boards++;
    if ((uint32_t)(now - s_nodes[i].last_seen_ms) <= kOnlineWindowMs) online++;
  }
  s_stats.discovered_boards = discovered;
  s_stats.assigned_boards = assigned_boards;
  s_stats.discovered_steppers = discovered * SS_AXES_PER_BOARD;
  s_stats.online_boards = online;
  portEXIT_CRITICAL(&s_lock);

  s_stats.assigned_positions = (uint32_t)config_store_assigned_count();
  s_stats.capacity = (uint32_t)config_store_capacity();
  s_stats.unassigned_positions =
      s_stats.capacity > s_stats.assigned_positions
          ? s_stats.capacity - s_stats.assigned_positions
          : 0;
  s_stats.unassigned_steppers =
      s_stats.discovered_steppers > s_stats.assigned_positions
          ? s_stats.discovered_steppers - s_stats.assigned_positions
          : 0;
}

}  // namespace

void commission_init(void) {
  portENTER_CRITICAL(&s_lock);
  std::memset(s_nodes, 0, sizeof(s_nodes));
  s_count = 0;
  for (auto &k : s_known) k.board_id = -1;
  portEXIT_CRITICAL(&s_lock);

  commission_reload_config();
  s_last_ask_ms = millis() - kAskPeriodMs;  /* first ASK immediately */
  s_selected = 0;
  s_test_active = false;
  s_test_count = 0;
  s_test_index = 0;
  SS_LOGI("commission", "ready; ASK every %u ms", (unsigned)kAskPeriodMs);
}

void commission_reload_config(void) {
  const ss_config_t *cfg = config_store_get();
  portENTER_CRITICAL(&s_lock);
  for (auto &k : s_known) k.board_id = -1;
  for (const auto &b : cfg->boards) {
    if (b.board_id < 0) continue;
    known_add_locked(b.flash_id, b.board_id);
  }
  portEXIT_CRITICAL(&s_lock);
}

void commission_on_frame(const ss_can_frame_t *frame, void *ctx) {
  (void)ctx;
  if (frame == nullptr) return;
  if (!mc_id_valid((int)frame->id)) return;

  switch (mc_id_type((int)frame->id)) {
    case kTypeDisc:
      handle_disc(frame);
      break;
    case kTypeStatus:
      handle_status(frame);
      break;
    case kTypeVersionRsp:
      handle_version(frame);
      break;
    case kTypeFault:
      SS_LOGW("commission", "fault frame id=0x%03lx board=%d",
              (unsigned long)frame->id, mc_id_field((int)frame->id));
      break;
    default:
      break;
  }
}

void commission_poll(void) {
  const uint32_t now = millis();

  if ((uint32_t)(now - s_last_ask_ms) >= kAskPeriodMs) {
    s_last_ask_ms = now;
    if (can_transport_running()) send_ask();
  }

  /* Board IDs are assigned in the CAN callback but persisted here, on the
   * main loop, so the config store is only mutated from one task. */
  if (s_persist_dirty) {
    bool still_dirty = false;
    portENTER_CRITICAL(&s_lock);
    for (int i = 0; i < s_count; ++i) {
      if (!s_nodes[i].used || s_nodes[i].board_id < 0) continue;
      if (config_store_get_board_id(s_nodes[i].flash_id) ==
          s_nodes[i].board_id) {
        continue;
      }
      if (!config_store_set_board_id(s_nodes[i].flash_id,
                                     s_nodes[i].board_id)) {
        still_dirty = true;
      }
    }
    portEXIT_CRITICAL(&s_lock);
    s_persist_dirty = still_dirty;
  }

  commission_test_run_poll();
  recompute_stats();
}

int commission_node_count(void) {
  portENTER_CRITICAL(&s_lock);
  const int n = s_count;
  portEXIT_CRITICAL(&s_lock);
  return n;
}

bool commission_get_node(int index, ss_node_t *out) {
  if (out == nullptr) return false;
  bool ok = false;
  portENTER_CRITICAL(&s_lock);
  if (index >= 0 && index < s_count && s_nodes[index].used) {
    *out = s_nodes[index];
    ok = true;
  }
  portEXIT_CRITICAL(&s_lock);
  if (ok) {
    out->assigned_positions = 0;
    const ss_config_t *cfg = config_store_get();
    for (int i = 0; i < config_store_capacity(); ++i) {
      const ss_position_map_t *p = &cfg->positions[i];
      if (p->assigned &&
          std::memcmp(p->flash_id, out->flash_id, SS_FLASH_ID_LEN) == 0) {
        out->assigned_positions++;
      }
    }
  }
  return ok;
}

const ss_commission_stats_t *commission_stats(void) {
  recompute_stats();
  return &s_stats;
}

int commission_find_node(const uint8_t flash_id[8]) {
  if (flash_id == nullptr) return -1;
  portENTER_CRITICAL(&s_lock);
  const int idx = find_node_locked(flash_id);
  portEXIT_CRITICAL(&s_lock);
  return idx;
}

bool commission_query_versions(void) {
  if (!can_transport_running()) return false;
  send_version_req();
  SS_LOGI("commission", "VERSION query sent to all boards");
  return true;
}

int commission_selected_index(void) { return s_selected; }

void commission_select(int index) {
  const int n = commission_node_count();
  if (n <= 0) {
    s_selected = 0;
    return;
  }
  if (index < 0) index = 0;
  if (index >= n) index = n - 1;
  s_selected = index;
}

bool commission_test_motor(const uint8_t flash_id[8], uint8_t axis,
                           ss_motor_action_t action, uint16_t distance_mm,
                           uint16_t speed_mm_s) {
  if (flash_id == nullptr || axis >= SS_AXES_PER_BOARD) return false;
  const int idx = commission_find_node(flash_id);
  if (idx < 0) return false;

  ss_node_t node;
  if (!commission_get_node(idx, &node) || node.board_id < 0) return false;

  ss_can_frame_t f;
  std::memset(&f, 0, sizeof(f));
  f.len = 8;

  if (action == SS_MOTOR_HOME) {
    f.id = (uint32_t)mc_id_make(node.board_id, kTypeHome);
    f.data[0] = (uint8_t)(1u << axis);
  } else {
    f.id = (uint32_t)mc_id_make(node.board_id, kTypeCommand);
    f.data[0] = (action == SS_MOTOR_STOP) ? kCmdStop
                 : (action == SS_MOTOR_UP) ? kCmdUp
                                           : kCmdDown;
    f.data[1] = (uint8_t)(1u << axis);
    put_u16(&f.data[2], distance_mm);
    put_u16(&f.data[4], speed_mm_s);
    f.data[6] = s_cmd_seq++;
  }

  const bool ok = can_transport_send(&f);
  if (!ok) {
    s_stats.tx_errors++;
    SS_LOGW("commission", "motor %s board %d axis %u send failed",
            action == SS_MOTOR_HOME ? "HOME"
            : action == SS_MOTOR_UP ? "UP"
            : action == SS_MOTOR_DOWN ? "DOWN"
                                      : "STOP",
            node.board_id, (unsigned)axis);
  }
  return ok;
}

bool commission_stop_all(void) {
  ss_can_frame_t f;
  std::memset(&f, 0, sizeof(f));
  f.id = (uint32_t)mc_id_make(MC_BOARD_ALL, kTypeCommand);
  f.len = 8;
  f.data[0] = kCmdStop;
  f.data[1] = 0x0Fu;
  const bool ok = can_transport_send(&f);
  if (!ok) s_stats.tx_errors++;
  return ok;
}

bool commission_test_run_start(void) {
  const ss_config_t *cfg = config_store_get();
  const int capacity = config_store_capacity();
  if (config_store_assigned_count() != capacity) {
    SS_LOGW("testrun", "refused: %d of %d positions assigned",
            config_store_assigned_count(), capacity);
    return false;
  }

  /* Every assigned position must have a live node with a board ID before a
   * cascade starts; otherwise a sphere silently never moves. */
  for (int i = 0; i < capacity; ++i) {
    const ss_position_map_t *p = &cfg->positions[i];
    if (!p->assigned) continue;
    const int idx = commission_find_node(p->flash_id);
    ss_node_t node;
    if (idx < 0 || !commission_get_node(idx, &node) || node.board_id < 0 ||
        (uint32_t)(millis() - node.last_seen_ms) > kOnlineWindowMs) {
      SS_LOGW("testrun", "refused: position %d has no live board", i);
      return false;
    }
  }

  s_test_count = 0;
  for (int row = 0; row < cfg->grid_rows; ++row) {
    const bool reverse = cfg->test_serpentine && (row % 2 == 1);
    for (int k = 0; k < cfg->grid_cols; ++k) {
      const int col = reverse ? (cfg->grid_cols - 1 - k) : k;
      const int pos = row * cfg->grid_cols + col;
      if (pos < SS_MAX_POSITIONS && cfg->positions[pos].assigned) {
        s_test_order[s_test_count++] = pos;
      }
    }
  }

  s_test_index = 0;
  s_test_next_ms = millis();
  s_test_active = true;
  SS_LOGI("testrun", "start: %d spheres, %umm down at %umm/s, gap %ums",
          s_test_count, (unsigned)cfg->test_drop_mm,
          (unsigned)cfg->test_speed_mm_s,
          (unsigned)cfg->test_step_delay_ms);
  return true;
}

void commission_test_run_stop(void) {
  if (s_test_active) SS_LOGW("testrun", "stopped by operator");
  s_test_active = false;
  commission_stop_all();
}

void commission_test_run_poll(void) {
  if (!s_test_active) return;
  const uint32_t now = millis();
  if ((int32_t)(now - s_test_next_ms) < 0) return;

  if (s_test_index >= s_test_count) {
    s_test_active = false;
    SS_LOGI("testrun", "complete");
    return;
  }

  const ss_config_t *cfg = config_store_get();
  const int pos = s_test_order[s_test_index++];
  const ss_position_map_t *p = &cfg->positions[pos];
  const int idx = commission_find_node(p->flash_id);
  ss_node_t node;
  if (idx < 0 || !commission_get_node(idx, &node) || node.board_id < 0 ||
      (uint32_t)(millis() - node.last_seen_ms) > kOnlineWindowMs) {
    SS_LOGE("testrun", "position %d board vanished; stopping", pos);
    s_test_active = false;
    commission_stop_all();
    return;
  }

  if (!commission_test_motor(p->flash_id, p->axis, SS_MOTOR_DOWN,
                             cfg->test_drop_mm, cfg->test_speed_mm_s)) {
    SS_LOGE("testrun", "position %d command failed; stopping", pos);
    s_test_active = false;
    commission_stop_all();
    return;
  }

  const uint32_t move_ms =
      (uint32_t)cfg->test_drop_mm * 1000u / (uint32_t)cfg->test_speed_mm_s;
  s_test_next_ms = now + move_ms + cfg->test_step_delay_ms;

  if (s_test_index >= s_test_count) {
    s_test_active = false;
    SS_LOGI("testrun", "last sphere commanded; cascade done");
  }
}

bool commission_test_run_active(void) { return s_test_active; }

int commission_test_run_remaining(void) {
  if (!s_test_active) return 0;
  const int left = s_test_count - s_test_index;
  return left > 0 ? left : 0;
}
