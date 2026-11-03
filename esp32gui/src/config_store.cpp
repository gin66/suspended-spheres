#include "config_store.h"

#include <Arduino.h>
#include <SPIFFS.h>

#include <cstddef>
#include <cstring>

#include "app_log.h"

namespace {

ss_config_t s_cfg;
bool s_dirty = false;
bool s_mounted = false;

uint32_t crc32_bytes(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
  }
  return ~crc;
}

uint32_t config_crc(const ss_config_t *cfg) {
  return crc32_bytes(reinterpret_cast<const uint8_t *>(cfg),
                     offsetof(ss_config_t, crc));
}

int capacity_from(const ss_config_t *cfg) {
  return (int)cfg->grid_cols * (int)cfg->grid_rows;
}

void set_defaults(ss_config_t *cfg) {
  std::memset(cfg, 0, sizeof(*cfg));
  cfg->magic = SS_CONFIG_MAGIC;
  cfg->version = SS_CONFIG_VERSION;
  cfg->grid_cols = 10;
  cfg->grid_rows = 12;
  cfg->test_drop_mm = 100;
  cfg->test_speed_mm_s = 20;
  cfg->test_step_delay_ms = 300;
  cfg->test_serpentine = 1;
  cfg->brightness = 100;
  for (auto &p : cfg->positions) {
    p.assigned = false;
    p.axis = 0;
  }
  for (auto &b : cfg->boards) b.board_id = -1;
}

bool valid_config(const ss_config_t *cfg) {
  if (cfg->magic != SS_CONFIG_MAGIC) return false;
  if (cfg->version != SS_CONFIG_VERSION) return false;
  if (cfg->grid_cols < 1 || cfg->grid_cols > SS_GRID_MAX_COLS) return false;
  if (cfg->grid_rows < 1 || cfg->grid_rows > SS_GRID_MAX_ROWS) return false;
  if (cfg->test_drop_mm == 0 || cfg->test_drop_mm > 2000) return false;
  if (cfg->test_speed_mm_s == 0 || cfg->test_speed_mm_s > 1000) return false;
  if (cfg->test_step_delay_ms == 0 || cfg->test_step_delay_ms > 60000u) return false;
  if (cfg->brightness < 10 || cfg->brightness > 100) return false;
  for (const auto &b : cfg->boards) {
    if (b.board_id < -1 || b.board_id >= SS_MAX_BOARDS) return false;
  }
  if (cfg->crc != config_crc(cfg)) return false;
  return true;
}

bool write_config(const ss_config_t *cfg) {
  ss_config_t tmp = *cfg;
  tmp.crc = config_crc(&tmp);

  File f = SPIFFS.open(SS_CONFIG_TMP_FILE, FILE_WRITE);
  if (!f) {
    SS_LOGE("cfg", "open %s failed", SS_CONFIG_TMP_FILE);
    return false;
  }
  const size_t written = f.write(reinterpret_cast<const uint8_t *>(&tmp),
                                 sizeof(tmp));
  f.close();
  if (written != sizeof(tmp)) {
    SS_LOGE("cfg", "short write %u/%u", (unsigned)written,
            (unsigned)sizeof(tmp));
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

}  // namespace

bool config_store_init(void) {
  s_mounted = SPIFFS.begin(true);
  if (!s_mounted) {
    SS_LOGE("cfg", "SPIFFS mount failed; using defaults in RAM");
  }

  if (s_mounted) {
    File f = SPIFFS.open(SS_CONFIG_FILE, FILE_READ);
    if (f && f.size() == sizeof(ss_config_t)) {
      const size_t got =
          f.read(reinterpret_cast<uint8_t *>(&s_cfg), sizeof(s_cfg));
      f.close();
      if (got == sizeof(s_cfg) && valid_config(&s_cfg)) {
        s_dirty = false;
        SS_LOGI("cfg", "loaded %ux%u, %d positions assigned",
                (unsigned)s_cfg.grid_cols, (unsigned)s_cfg.grid_rows,
                config_store_assigned_count());
        return true;
      }
      SS_LOGW("cfg", "config invalid (magic/version/crc); writing defaults");
    } else {
      if (f) f.close();
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

bool config_store_assign_position(uint8_t position, const uint8_t flash_id[8],
                                  uint8_t axis) {
  if (flash_id == nullptr) return false;
  if (axis >= SS_AXES_PER_BOARD) return false;
  if (position >= capacity_from(&s_cfg)) return false;

  for (auto &p : s_cfg.positions) {
    if (p.assigned && p.axis == axis &&
        std::memcmp(p.flash_id, flash_id, SS_FLASH_ID_LEN) == 0) {
      p.assigned = false;
    }
  }
  ss_position_map_t *slot = &s_cfg.positions[position];
  slot->assigned = true;
  slot->axis = axis;
  std::memcpy(slot->flash_id, flash_id, SS_FLASH_ID_LEN);
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

int config_store_find_position(const uint8_t flash_id[8], uint8_t axis) {
  if (flash_id == nullptr || axis >= SS_AXES_PER_BOARD) return -1;
  for (int i = 0; i < capacity_from(&s_cfg); ++i) {
    const ss_position_map_t *p = &s_cfg.positions[i];
    if (p->assigned && p->axis == axis &&
        std::memcmp(p->flash_id, flash_id, SS_FLASH_ID_LEN) == 0) {
      return i;
    }
  }
  return -1;
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

int config_store_get_board_id(const uint8_t flash_id[8]) {
  if (flash_id == nullptr) return -1;
  for (const auto &b : s_cfg.boards) {
    if (b.board_id >= 0 &&
        std::memcmp(b.flash_id, flash_id, SS_FLASH_ID_LEN) == 0) {
      return b.board_id;
    }
  }
  return -1;
}

bool config_store_set_board_id(const uint8_t flash_id[8], int board_id) {
  if (flash_id == nullptr) return false;
  if (board_id < -1 || board_id >= SS_MAX_BOARDS) return false;

  ss_board_map_t *slot = nullptr;
  for (auto &b : s_cfg.boards) {
    if (std::memcmp(b.flash_id, flash_id, SS_FLASH_ID_LEN) == 0) {
      slot = &b;
      break;
    }
  }
  if (slot == nullptr) {
    for (auto &b : s_cfg.boards) {
      if (b.board_id < 0) {
        slot = &b;
        break;
      }
    }
  }
  if (slot == nullptr) return false;

  std::memcpy(slot->flash_id, flash_id, SS_FLASH_ID_LEN);
  slot->board_id = (int8_t)board_id;
  s_dirty = true;
  return true;
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
  for (auto &b : s_cfg.boards) {
    b.board_id = -1;
  }
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
