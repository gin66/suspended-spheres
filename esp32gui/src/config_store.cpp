#include "config_store.h"

#include <Arduino.h>
#include <SPIFFS.h>

#include <cstddef>
#include <cstring>

#include "app_log.h"

namespace {

ss_config_t s_cfg;
/* The v2 record is about 2 KiB.  Keep the serialise scratch off the loop
 * task stack; the panel's loop stack is tight enough without two copies. */
uint8_t s_blob[sizeof(ss_config_t)];
ss_config_t s_loaded;
bool s_dirty = false;
bool s_mounted = false;

int capacity_from(const ss_config_t *cfg) {
  return (int)cfg->grid_cols * (int)cfg->grid_rows;
}

void set_defaults(ss_config_t *cfg) {
  pc_config_v2_defaults(cfg);
}

bool write_config(const ss_config_t *cfg) {
  if (pc_config_v2_pack(cfg, s_blob, sizeof(s_blob)) != 0) return false;

  File f = SPIFFS.open(SS_CONFIG_TMP_FILE, FILE_WRITE);
  if (!f) {
    SS_LOGE("cfg", "open %s failed", SS_CONFIG_TMP_FILE);
    return false;
  }
  const size_t written = f.write(s_blob, sizeof(s_blob));
  f.close();
  if (written != sizeof(s_blob)) {
    SS_LOGE("cfg", "short write %u/%u", (unsigned)written,
            (unsigned)sizeof(s_blob));
    SPIFFS.remove(SS_CONFIG_TMP_FILE);
    return false;
  }
  SPIFFS.remove(SS_CONFIG_FILE);
  if (!SPIFFS.rename(SS_CONFIG_TMP_FILE, SS_CONFIG_FILE)) {
    SS_LOGE("cfg", "rename %s -> %s failed", SS_CONFIG_TMP_FILE,
            SS_CONFIG_FILE);
    return false;
  }
  return true;
}

/* Version 1 keys rows by the 8-byte flash id.  Keep only the header that is
 * layout-identical (grid, test parameters, brightness) and leave every UUID
 * unknown; the operator re-assigns the positions. */
bool migrate_v1(const uint8_t *blob, size_t len, ss_config_t *out) {
  if (len < offsetof(ss_config_t, positions)) return false;
  uint32_t magic = 0;
  uint16_t version = 0;
  std::memcpy(&magic, blob, sizeof(magic));
  std::memcpy(&version, blob + sizeof(magic), sizeof(version));
  if (magic != SS_CONFIG_MAGIC || version != 1u) return false;
  set_defaults(out);
  const size_t header = offsetof(ss_config_t, positions);
  std::memcpy(out, blob, header);
  out->magic = SS_CONFIG_MAGIC;
  out->version = SS_CONFIG_VERSION;
  if (out->grid_cols < 1 || out->grid_cols > SS_GRID_MAX_COLS) return false;
  if (out->grid_rows < 1 || out->grid_rows > SS_GRID_MAX_ROWS) return false;
  return true;
}

}  // namespace

bool config_store_init(void) {
  s_mounted = SPIFFS.begin(true);
  if (!s_mounted) {
    SS_LOGE("cfg", "SPIFFS mount failed; using defaults in RAM");
  }

  if (s_mounted) {
    File f = SPIFFS.open(SS_CONFIG_FILE, FILE_READ);
    if (f) {
      const size_t size = f.size();
      size_t want = size < sizeof(s_blob) ? size : sizeof(s_blob);
      const size_t got = f.read(s_blob, want);
      f.close();
      if (size == sizeof(ss_config_t) && got == sizeof(s_blob) &&
          pc_config_v2_unpack(s_blob, sizeof(s_blob), &s_loaded) == 0) {
        s_cfg = s_loaded;
        s_dirty = false;
        SS_LOGI("cfg", "loaded v2 %ux%u, %d positions assigned",
                (unsigned)s_cfg.grid_cols, (unsigned)s_cfg.grid_rows,
                config_store_assigned_count());
        return true;
      }
      if (migrate_v1(s_blob, got, &s_cfg)) {
        s_cfg.version = SS_CONFIG_VERSION;
        s_dirty = true;
        SS_LOGW("cfg", "version 1 migrated as version unknown; re-assign rows");
        config_store_save();
        return s_mounted;
      }
      SS_LOGW("cfg", "config invalid (magic/version/crc); writing defaults");
    } else {
      SS_LOGW("cfg", "no valid config; writing defaults");
    }
  }

  set_defaults(&s_cfg);
  s_dirty = true;
  if (s_mounted) config_store_save();
  return s_mounted;
}

const ss_config_t *config_store_get(void) { return &s_cfg; }

bool config_store_set_grid(uint8_t cols, uint8_t rows) {
  if (cols < 1 || cols > SS_GRID_MAX_COLS) return false;
  if (rows < 1 || rows > SS_GRID_MAX_ROWS) return false;
  if (s_cfg.grid_cols == cols && s_cfg.grid_rows == rows) return true;

  s_cfg.grid_cols = cols;
  s_cfg.grid_rows = rows;
  const int cap = capacity_from(&s_cfg);
  for (int i = cap; i < SS_MAX_POSITIONS; ++i) {
    s_cfg.positions[i].assigned = false;
  }
  s_dirty = true;
  return true;
}

bool config_store_set_test_params(uint16_t drop_mm, uint16_t speed_mm_s,
                                  uint16_t step_delay_ms, bool serpentine) {
  if (drop_mm == 0 || drop_mm > 2000) return false;
  if (speed_mm_s == 0 || speed_mm_s > 1000) return false;
  if (step_delay_ms == 0 || step_delay_ms > 60000u) return false;
  s_cfg.test_drop_mm = drop_mm;
  s_cfg.test_speed_mm_s = speed_mm_s;
  s_cfg.test_step_delay_ms = step_delay_ms;
  s_cfg.test_serpentine = serpentine ? 1 : 0;
  s_dirty = true;
  return true;
}

bool config_store_set_brightness(uint8_t percent) {
  if (percent < 10 || percent > 100) return false;
  s_cfg.brightness = percent;
  s_dirty = true;
  return true;
}

bool config_store_assign_position(uint8_t position, const uint8_t uuid[6],
                                  uint8_t axis) {
  if (uuid == nullptr) return false;
  if (axis >= SS_AXES_PER_BOARD) return false;
  if (position >= capacity_from(&s_cfg)) return false;

  for (auto &p : s_cfg.positions) {
    if (p.assigned && p.axis == axis &&
        std::memcmp(p.uuid, uuid, SS_UUID_LEN) == 0) {
      p.assigned = false;
    }
  }
  ss_position_map_t *slot = &s_cfg.positions[position];
  slot->assigned = true;
  slot->axis = axis;
  std::memcpy(slot->uuid, uuid, SS_UUID_LEN);
  s_dirty = true;
  return true;
}

bool config_store_unassign_position(uint8_t position) {
  if (position >= capacity_from(&s_cfg)) return false;
  s_cfg.positions[position].assigned = false;
  s_dirty = true;
  return true;
}

void config_store_clear_positions(void) {
  for (auto &p : s_cfg.positions) p.assigned = false;
  s_dirty = true;
}

int config_store_find_position(const uint8_t uuid[6], uint8_t axis) {
  return pc_config_v2_find_position(&s_cfg, uuid, axis);
}

int config_store_assigned_count(void) {
  const int cap = capacity_from(&s_cfg);
  int n = 0;
  for (int i = 0; i < cap; ++i) {
    if (s_cfg.positions[i].assigned) ++n;
  }
  return n;
}

int config_store_capacity(void) { return capacity_from(&s_cfg); }

int config_store_get_board_id(const uint8_t uuid[6]) {
  return pc_config_v2_get_board_id(&s_cfg, uuid);
}

bool config_store_set_board_id(const uint8_t uuid[6], int board_id) {
  if (uuid == nullptr) return false;
  if (board_id < -1 || board_id >= SS_MAX_BOARDS) return false;
  const ss_board_map_t *old = config_store_get_board(uuid);
  uint32_t build = old ? old->last_build : 0;
  uint8_t flags = old ? old->version_flags : 0;
  const char *result = old ? old->last_result : "";
  if (!pc_config_v2_set_board(&s_cfg, uuid, board_id, build, flags, result))
    return false;
  s_dirty = true;
  return true;
}

bool config_store_set_board_version(const uint8_t uuid[6], int board_id,
                                    uint32_t build, uint8_t flags,
                                    const char *result) {
  if (!pc_config_v2_set_board(&s_cfg, uuid, board_id, build, flags, result))
    return false;
  s_dirty = true;
  return true;
}

const ss_board_map_t *config_store_get_board(const uint8_t uuid[6]) {
  if (uuid == nullptr) return nullptr;
  for (const auto &b : s_cfg.boards) {
    if (b.board_id >= 0 &&
        std::memcmp(b.uuid, uuid, SS_UUID_LEN) == 0) {
      return &b;
    }
  }
  return nullptr;
}

int config_store_free_board_id(void) {
  for (int id = 0; id < SS_MAX_BOARDS; ++id) {
    bool used = false;
    for (const auto &b : s_cfg.boards) {
      if (b.board_id == id) {
        used = true;
        break;
      }
    }
    if (!used) return id;
  }
  return -1;
}

void config_store_clear_boards(void) {
  std::memset(s_cfg.boards, 0, sizeof(s_cfg.boards));
  for (auto &b : s_cfg.boards) b.board_id = -1;
  s_dirty = true;
}

bool config_store_dirty(void) { return s_dirty; }

bool config_store_save(void) {
  if (!s_mounted) {
    s_dirty = false;
    return false;
  }
  if (!write_config(&s_cfg)) return false;
  s_dirty = false;
  SS_LOGI("cfg", "saved: %ux%u, %d positions, test %umm @ %umm/s, %ums",
          (unsigned)s_cfg.grid_cols, (unsigned)s_cfg.grid_rows,
          config_store_assigned_count(), (unsigned)s_cfg.test_drop_mm,
          (unsigned)s_cfg.test_speed_mm_s, (unsigned)s_cfg.test_step_delay_ms);
  return true;
}

bool config_store_reset_defaults(void) {
  set_defaults(&s_cfg);
  s_dirty = true;
  return config_store_save();
}
