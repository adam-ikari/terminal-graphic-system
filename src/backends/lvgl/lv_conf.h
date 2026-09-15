/**
 * @file lv_conf.h
 * Minimal LVGL configuration for TGS (Terminal Graphic System)
 * Targets LVGL v9.6.x
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* clang-format off */

/*============================================================================
 * MEMORY & STDLIB
 *============================================================================*/
#define LV_USE_STDLIB_MALLOC   LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

#define LV_MEM_SIZE (4 * 1024 * 1024)  /* 4 MB pool */

/*============================================================================
 * OS
 *============================================================================*/
#define LV_USE_OS LV_OS_NONE

/*============================================================================
 * RENDERING
 *============================================================================*/
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_ARGB8888

#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_SUPPORT_ARGB8888 1
#define LV_DRAW_SW_SUPPORT_RGB565 0
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_RGB565A8 0
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_A8 0
#define LV_DRAW_SW_SUPPORT_I1 0

#define LV_USE_SNAPSHOT 0

/*============================================================================
 * LOGGING — disabled
 *============================================================================*/
#define LV_USE_LOG 0

/*============================================================================
 * THEMES
 *============================================================================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_SIMPLE 1
#define LV_USE_THEME_MONO 0

/*============================================================================
 * LAYOUTS
 *============================================================================*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*============================================================================
 * IMAGE — all decoders disabled (we feed pixels directly)
 *============================================================================*/
#define LV_CACHE_DEF_SIZE 0
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 0
#define LV_USE_RLE 0
#define LV_USE_LZ4 0
#define LV_BIN_DECODER_RAM_LOAD 0
#define LV_USE_LODEPNG 0
#define LV_USE_LIBPNG 0
#define LV_USE_BMP 0
#define LV_USE_TJPGD 0
#define LV_USE_LIBJPEG_TURBO 0
#define LV_USE_LIBWEBP 0
#define LV_USE_SVG 0

/*============================================================================
 * FONTS — Montserrat 14 only
 *============================================================================*/
#define LV_FONT_MONTSERRAT_8 0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SOURCE_HAN_SANS_SC_14_CJK 0
#define LV_FONT_SOURCE_HAN_SANS_SC_16_CJK 0
#define LV_FONT_UNSCII_8 1
#define LV_FONT_UNSCII_16 0
#define LV_USE_FONT_PLACEHOLDER 1

/*============================================================================
 * WIDGETS — enable only what TGS uses
 *============================================================================*/
#define LV_WIDGETS_HAS_DEFAULT_VALUE 1
#define LV_USE_3DTEXTURE 0
#define LV_USE_ANIMIMG 0
#define LV_USE_ARC 0
#define LV_USE_ARCLABEL 0
#define LV_USE_BAR 1
#define LV_USE_BARCODE 0
#define LV_USE_BUTTON 1
#define LV_USE_BUTTONMATRIX 1  /* required by LV_USE_CALENDAR (DATEPICK) */
#define LV_USE_CALENDAR 1
#define LV_USE_CANVAS 1
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 1
#define LV_USE_DROPDOWN 1
#define LV_USE_IMAGE 1
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LABEL 1
#define LV_USE_LINE 0
#define LV_USE_SCALE 0
#define LV_USE_LIST 1
#define LV_USE_MENU 1
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 1
#define LV_USE_SLIDER 1
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 1
#define LV_USE_TEXTAREA 1
#define LV_USE_TABLE 1
#define LV_USE_TABVIEW 1
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

/*============================================================================
 * INPUT DEVICES
 *============================================================================*/
#define LV_USE_GRIDNAV 1  /* arrow-key navigation between widgets */

/*============================================================================
 * CORE
 *============================================================================*/
#define LV_USE_OBJ_NAME 0
#define LV_USE_OBJ_ID 0
#define LV_USE_OBJ_PROPERTY 0
#define LV_USE_EXT_DATA 0
#define LV_USE_OBSERVER 1

/*============================================================================
 * FILESYSTEM — all disabled
 *============================================================================*/
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0
#define LV_USE_FS_LITTLEFS 0
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS 0
#define LV_USE_FS_ARDUINO_SD 0
#define LV_USE_FS_UEFI 0
#define LV_USE_FS_FROGFS 0
#define LV_USE_FS_MEMFS 0

/*============================================================================
 * DEBUGGING — disabled
 *============================================================================*/
#define LV_USE_SYSMON 0

/*============================================================================
 * OTHERS
 *============================================================================*/
#define LV_USE_SDL 0

/*============================================================================
 * DEMOS — disabled
 *============================================================================*/
#define LV_BUILD_DEMOS 0

#endif /* LV_CONF_H */
