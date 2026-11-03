#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef IS_WAVESHARE_LCD7

#include <driver/spi_master.h>
#include <lvgl.h>

#define BOARD_WS7_SCREEN_WIDTH 800
#define BOARD_WS7_SCREEN_HEIGHT 480

// Logical orientation (see whitepaper §6.6). This is NOT the LVGL enum:
//   0 = portrait 480x800 (LVGL rotation 90)   1 = landscape 800x480 (LVGL
//   rotation 0) 2 = portrait 480x800 (LVGL rotation 270)  3 = landscape 800x480
//   (LVGL rotation 180)
#ifndef BOARD_WS7_ROTATION
#define BOARD_WS7_ROTATION 0
#endif

#if (BOARD_WS7_ROTATION == 0) || (BOARD_WS7_ROTATION == 2)
#define BOARD_WS7_WIDTH 480
#define BOARD_WS7_HEIGHT 800
#else
#define BOARD_WS7_WIDTH 800
#define BOARD_WS7_HEIGHT 480
#endif

// I2C bus for CH422G
#define BOARD_WS7_I2C_SDA 8
#define BOARD_WS7_I2C_SCL 9
#define BOARD_WS7_I2C_ADDR 0x24

// CH422G IO assignments
#define BOARD_WS7_IO_TP_RST 1
#define BOARD_WS7_IO_LCD_BL 2
#define BOARD_WS7_IO_LCD_RST 3
#define BOARD_WS7_IO_SD_CS 4
#define BOARD_WS7_IO_USB_SEL 5

// RGB panel pins
#define BOARD_WS7_PIN_VSYNC 3
#define BOARD_WS7_PIN_HSYNC 46
#define BOARD_WS7_PIN_DE 5
#define BOARD_WS7_PIN_PCLK 7
#define BOARD_WS7_PIN_DATA0 14
#define BOARD_WS7_PIN_DATA1 38
#define BOARD_WS7_PIN_DATA2 18
#define BOARD_WS7_PIN_DATA3 17
#define BOARD_WS7_PIN_DATA4 10
#define BOARD_WS7_PIN_DATA5 39
#define BOARD_WS7_PIN_DATA6 0
#define BOARD_WS7_PIN_DATA7 45
#define BOARD_WS7_PIN_DATA8 48
#define BOARD_WS7_PIN_DATA9 47
#define BOARD_WS7_PIN_DATA10 21
#define BOARD_WS7_PIN_DATA11 1
#define BOARD_WS7_PIN_DATA12 2
#define BOARD_WS7_PIN_DATA13 42
#define BOARD_WS7_PIN_DATA14 41
#define BOARD_WS7_PIN_DATA15 40

// SD SPI pins
#define BOARD_WS7_SD_MOSI 11
#define BOARD_WS7_SD_SCLK 12
#define BOARD_WS7_SD_MISO 13

esp_err_t board_ws7_expander_init(void);

esp_err_t board_ws7_backlight(bool on);
esp_err_t board_ws7_touch_reset_assert(void);
esp_err_t board_ws7_touch_reset_release(void);
esp_err_t board_ws7_panel_reset(void);
esp_err_t board_ws7_sd_select(bool select);
esp_err_t board_ws7_set_usb_mode(bool usb);
bool board_ws7_can_available(void);

/* Read the CH422G RD-IO register and report the actual IO5 level (S23.02).
 * true = the mux currently routes GPIO 19/20 to CAN. This is a hardware
 * readback, not the cached output shadow. */
esp_err_t board_ws7_read_can_available(bool* can);

// I2C mutex with bounded timeout (N13)
esp_err_t board_ws7_i2c_lock(uint32_t timeout_ms);
void board_ws7_i2c_unlock(void);

// SD card: enable open-collector push-pull mode for CS bit (S3.15)
esp_err_t board_ws7_sd_cs_enable_oc_pp(void);

// SD SPI bus handle (S3.16)
spi_host_device_t board_ws7_sd_bus(void);

// RGB panel + LVGL display (S4)
lv_display_t* board_ws7_display_init(uint8_t rotation, uint32_t* buf,
                                   size_t buf_size);

/* Recreate the RGB panel from the compile-time BOARD_WS7_* timing macros and
 * pulse the ST7262 reset through CH422G IO3 (S4.06). This resets every timing
 * register and restarts DMA without destroying the LVGL display. Used by
 * Settings -> PANEL RESET. */
esp_err_t board_ws7_reset_rgb_timing(void);

// GT911 touch (S5)
esp_err_t board_ws7_touch_init(void);
void board_ws7_touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data);

/* Number of touch points in the last GT911 frame (for three-finger Home). */
int board_ws7_touch_points(void);

/* Last raw native GT911 point, without offsets or clamping. Returns false
 * when the last frame had no touch point. Used by the touch calibration test.
 */
bool board_ws7_touch_last_raw(int16_t* x, int16_t* y);

#endif  // IS_WAVESHARE_LCD7
