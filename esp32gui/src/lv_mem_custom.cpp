/* LVGL object memory from PSRAM, with an internal-RAM fallback.  The RGB
 * framebuffer and the DMA draw buffers stay in internal RAM; LVGL objects do
 * not need internal access.  Keeping them out of DRAM leaves room for lwIP
 * and the WiFi driver (firmware_release_whitepaper.md §3.7). */

#include <lvgl.h>
#include <esp_heap_caps.h>

#include <cstdlib>

extern "C" void lv_mem_init(void) {}
extern "C" void lv_mem_deinit(void) {}

extern "C" lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) {
  (void)mem;
  (void)bytes;
  return nullptr;
}

extern "C" void lv_mem_remove_pool(lv_mem_pool_t pool) { (void)pool; }

extern "C" void *lv_malloc_core(size_t size) {
  void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (p == nullptr) p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
  return p;
}

extern "C" void *lv_realloc_core(void *p, size_t new_size) {
  void *q = heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM);
  if (q == nullptr) q = heap_caps_realloc(p, new_size, MALLOC_CAP_INTERNAL);
  return q;
}

extern "C" void lv_free_core(void *p) {
  if (p != nullptr) heap_caps_free(p);
}

extern "C" void lv_mem_monitor_core(lv_mem_monitor_t *mon_p) {
  (void)mon_p;
}

extern "C" lv_result_t lv_mem_test_core(void) {
  return LV_RESULT_OK;
}
