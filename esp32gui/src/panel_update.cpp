#include "panel_update.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

#include <cstring>

namespace {

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
panel_update_state_t s_state = PANEL_UPDATE_IDLE;
int s_percent = -1;
char s_stage[40] = "";
char s_result[64] = "";
uint32_t s_write_ms = 0;
bool s_blanked = false;

void set_text(char *dst, size_t cap, const char *src) {
  if (dst == nullptr || cap == 0) return;
  if (src == nullptr) src = "";
  const size_t n = std::strlen(src);
  const size_t take = n < cap - 1 ? n : cap - 1;
  std::memcpy(dst, src, take);
  dst[take] = '\0';
}

}  // namespace

void panel_update_clear(void) {
  portENTER_CRITICAL(&s_mux);
  s_state = PANEL_UPDATE_IDLE;
  s_percent = -1;
  s_stage[0] = '\0';
  s_result[0] = '\0';
  s_write_ms = 0;
  s_blanked = false;
  portEXIT_CRITICAL(&s_mux);
}

void panel_update_begin(const char *stage) {
  portENTER_CRITICAL(&s_mux);
  s_state = PANEL_UPDATE_DOWNLOAD;
  s_percent = 0;
  s_result[0] = '\0';
  s_write_ms = 0;
  s_blanked = false;
  set_text(s_stage, sizeof(s_stage), stage ? stage : "downloading");
  portEXIT_CRITICAL(&s_mux);
}

void panel_update_stage(const char *stage) {
  portENTER_CRITICAL(&s_mux);
  set_text(s_stage, sizeof(s_stage), stage ? stage : "");
  portEXIT_CRITICAL(&s_mux);
}

void panel_update_progress(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  portENTER_CRITICAL(&s_mux);
  s_percent = percent;
  portEXIT_CRITICAL(&s_mux);
}

void panel_update_begin_write(void) {
  portENTER_CRITICAL(&s_mux);
  s_state = PANEL_UPDATE_WRITING;
  s_percent = -1;
  set_text(s_stage, sizeof(s_stage), "writing flash");
  s_write_ms = millis();
  s_blanked = false;
  portEXIT_CRITICAL(&s_mux);
}

void panel_update_error(const char *text) {
  portENTER_CRITICAL(&s_mux);
  s_state = PANEL_UPDATE_ERROR;
  s_percent = -1;
  set_text(s_result, sizeof(s_result), text ? text : "update failed");
  set_text(s_stage, sizeof(s_stage), "update failed");
  portEXIT_CRITICAL(&s_mux);
}

void panel_update_done(const char *text) {
  portENTER_CRITICAL(&s_mux);
  s_state = PANEL_UPDATE_DONE;
  s_percent = 100;
  set_text(s_result, sizeof(s_result), text ? text : "updated");
  set_text(s_stage, sizeof(s_stage), "done");
  portEXIT_CRITICAL(&s_mux);
}

bool panel_update_writing(void) {
  portENTER_CRITICAL(&s_mux);
  const bool writing = (s_state == PANEL_UPDATE_WRITING);
  portEXIT_CRITICAL(&s_mux);
  return writing;
}

bool panel_update_blank_due(uint32_t now_ms, uint32_t delay_ms) {
  portENTER_CRITICAL(&s_mux);
  const bool due = (s_state == PANEL_UPDATE_WRITING) && !s_blanked &&
                   (uint32_t)(now_ms - s_write_ms) >= delay_ms;
  portEXIT_CRITICAL(&s_mux);
  return due;
}

void panel_update_mark_blanked(bool blanked) {
  portENTER_CRITICAL(&s_mux);
  s_blanked = blanked;
  portEXIT_CRITICAL(&s_mux);
}

bool panel_update_blanked(void) {
  portENTER_CRITICAL(&s_mux);
  const bool blanked = s_blanked;
  portEXIT_CRITICAL(&s_mux);
  return blanked;
}

void panel_update_get(bool *active, bool *error, int *progress, char *stage,
                      size_t stage_cap, char *result, size_t result_cap) {
  portENTER_CRITICAL(&s_mux);
  const bool is_active = (s_state != PANEL_UPDATE_IDLE);
  const bool is_error = (s_state == PANEL_UPDATE_ERROR);
  const int pct = s_percent;
  char stage_copy[sizeof(s_stage)];
  char result_copy[sizeof(s_result)];
  set_text(stage_copy, sizeof(stage_copy), s_stage);
  set_text(result_copy, sizeof(result_copy), s_result);
  portEXIT_CRITICAL(&s_mux);

  if (active) *active = is_active;
  if (error) *error = is_error;
  if (progress) *progress = pct;
  if (stage && stage_cap) set_text(stage, stage_cap, stage_copy);
  if (result && result_cap) set_text(result, result_cap, result_copy);
}
