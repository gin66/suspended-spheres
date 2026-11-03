/**
 * Minimal lv_conf.h for the esp32gui macOS host harness.
 * Matches the 800x480 panel and the fonts used by ui/ui.cpp.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_USE_STDLIB_MALLOC 1
#define LV_USE_CLIB_STRING  1
#define LV_USE_CLIB_SPRINTF 1

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_ARGB8888
#define LV_COLOR_DEPTH 32
#define LV_DEF_REFR_PERIOD 16

#define LV_USE_THORVG         0
#define LV_USE_VECTOR_GRAPHIC 0
#define LV_USE_MATRIX         0
#define LV_USE_FLOAT          1
#define LV_DRAW_SW_DRAW_UNIT_CNT 1
#define LV_DRAW_THREAD_STACK_SIZE 0
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE 0

#define LV_USE_OBJ_NAME 0
#define LV_USE_OBJ_STYLE_CACHE 0
#define LV_USE_LOG 0
#define LV_USE_LOG_PRINTF 0
#define LV_USE_TRANSLATION 0
#define LV_USE_SYSMON 0
#define LV_USE_FS_STDIO 0
#define LV_USE_QRCODE 0
#define LV_USE_BARCODE 0
#define LV_USE_GIF 0
#define LV_USE_GSTREAMER 0
#define LV_USE_LOTTIE 0
#define LV_USE_TINY_TTF 0
#define LV_USE_FREETYPE 0
#define LV_USE_LIBPNG 0
#define LV_USE_LIBJPEG_TURBO 0
#define LV_USE_LIBWEBP 0
#define LV_USE_LZ4 0
#define LV_USE_BMP 0
#define LV_USE_TJPGD 0
#define LV_USE_LODEPNG 1
#define LV_USE_RLE 0
#define LV_USE_SVG_ANIMATION 0
#define LV_USE_CANVAS 1
#define LV_USE_CANVAS_FORMAT_ARGB8888 1

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_UNSCII_8 1
#define LV_FONT_UNSCII_16 1

#define LV_USE_SDL 0
#define LV_SDL_BUF_COUNT 1

#endif /* LV_CONF_H */
