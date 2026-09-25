/**
 * @file lv_conf.h
 * @brief LVGL v9.5.0 configuration for ESP32-S3 Nas-assistant project
 * 
 * Display: ILI9488 横屏 480x320 RGB565
 * Touch: XPT2046
 * OS: FreeRTOS
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING   LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF  LV_STDLIB_CLIB

#define LV_STDINT_INCLUDE       <stdint.h>
#define LV_STDDEF_INCLUDE       <stddef.h>
#define LV_STDBOOL_INCLUDE      <stdbool.h>
#define LV_INTTYPES_INCLUDE     <inttypes.h>
#define LV_LIMITS_INCLUDE       <limits.h>
#define LV_STDARG_INCLUDE       <stdarg.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    #define LV_MEM_SIZE (64 * 1024U)
    #define LV_MEM_POOL_EXPAND_SIZE 0
    #define LV_MEM_ADR 0
    #if LV_MEM_ADR == 0
        #undef LV_MEM_POOL_INCLUDE
        #undef LV_MEM_POOL_ALLOC
    #endif
#endif

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DEF_REFR_PERIOD  10
#define LV_DPI_DEF  130

/*=================
 * OPERATING SYSTEM
 *=================*/
#define LV_USE_OS   LV_OS_FREERTOS

#if LV_USE_OS == LV_OS_FREERTOS
    #define LV_USE_FREERTOS_TASK_NOTIFY 1
#endif

/*========================
 * RENDERING CONFIGURATION
 *========================*/
#define LV_DRAW_BUF_STRIDE_ALIGN                1
#define LV_DRAW_BUF_ALIGN                       4
#define LV_DRAW_TRANSFORM_USE_MATRIX            0
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE    (24 * 1024)
#define LV_DRAW_LAYER_MAX_MEMORY 0
#define LV_DRAW_THREAD_STACK_SIZE    (8 * 1024)
#define LV_DRAW_THREAD_PRIO LV_THREAD_PRIO_MID

#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW == 1
    #define LV_DRAW_SW_SUPPORT_RGB565       1
    #define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED       0
    #define LV_DRAW_SW_SUPPORT_RGB565A8     0
    #define LV_DRAW_SW_SUPPORT_RGB888       0
    #define LV_DRAW_SW_SUPPORT_XRGB8888     0
    #define LV_DRAW_SW_SUPPORT_ARGB8888     0
    #define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
    #define LV_DRAW_SW_SUPPORT_L8           0
    #define LV_DRAW_SW_SUPPORT_AL88         0
    #define LV_DRAW_SW_SUPPORT_A8           0
    #define LV_DRAW_SW_SUPPORT_I1           1

    #define LV_DRAW_SW_I1_LUM_THRESHOLD 127

    #define LV_DRAW_SW_DRAW_UNIT_CNT    1

    #define LV_USE_DRAW_ARM2D_SYNC      0
    #define LV_USE_NATIVE_HELIUM_ASM    0
    #define LV_DRAW_SW_COMPLEX          1

    #if LV_DRAW_SW_COMPLEX == 1
        #define LV_DRAW_SW_SHADOW_CACHE_SIZE 0
        #define LV_DRAW_SW_CIRCLE_CACHE_SIZE 4
    #endif

    #define  LV_USE_DRAW_SW_ASM     LV_DRAW_SW_ASM_NONE
    #if LV_USE_DRAW_SW_ASM == LV_DRAW_SW_ASM_CUSTOM
        #define  LV_DRAW_SW_ASM_CUSTOM_INCLUDE ""
    #endif

    #define LV_USE_DRAW_SW_COMPLEX_GRADIENTS    0
#endif

#define LV_USE_NEMA_GFX 0
#define LV_USE_PXP 0
#define LV_USE_G2D 0
#define LV_USE_DRAW_VGLITE 0
#define LV_USE_DRAW_DAVE2D 0
#define LV_USE_DRAW_SDL 0

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 0
    #define LV_LOG_USE_TIMESTAMP 1
    #define LV_LOG_TRACE_MEMCPY 0
    #define LV_LOG_TRACE_MALLOC 0
    #define LV_LOG_TRACE_TIMER 0
    #define LV_LOG_TRACE_USER_DATA 0
#endif

/*-------------
 * Asserts
 *-----------*/
#define LV_ASSERT_HANDLER_INCLUDE <stdio.h>
#define LV_ASSERT_HANDLER while(1);

/*------------------
 * ASYNC
 *-----------------*/
#define LV_USE_ASYNC 0

/*------------------
 * ANIMATIONS
 *-----------------*/
#define LV_USE_ANIMATION 1
#if LV_USE_ANIMATION
    #define LV_ANIM_RESIZE_WINDOW 0
#endif

/*-------------------
 * EXTERNAL PERIPHERAL
 *-------------------*/
#define LV_USE_SNAPSHOT 0
#define LV_USE_SYSMON 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_PROFILER 0
#define LV_USE_VG_LITE 0
#define LV_USE_THORVG 0
#define LV_USE_LZ4 0
#define LV_USE_RLE 0
#define LV_USE_FS_FATFS 0
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_LITTLEFS 0
#define LV_USE_IME_PINYIN 0
#define LV_USE_IME_BOPOMOFO 0
#define LV_USE_FILE_EXPLORER 0

/*==================
 * WIDGET USAGE
 *===================*/
#define LV_USE_ANIMIMG    1
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     1
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMAGE      1
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD   1
#define LV_USE_LABEL      1
#define LV_USE_LED        1
#define LV_USE_LINE       1
#define LV_USE_LIST       1
#define LV_USE_MENU       0
#define LV_USE_MSGBOX     0
#define LV_USE_ROLLER     1
#define LV_USE_SCALE      0
#define LV_USE_SLIDER     1
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    1
#define LV_USE_SWITCH     1
#define LV_USE_TABVIEW    1
#define LV_USE_TABLE      0
#define LV_USE_TEXTAREA   1
#define LV_USE_TILEVIEW   0
#define LV_USE_WINDOW     0
#define LV_USE_CALENDAR   0

/*==================
 * THEMES
 *===================*/
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 0
    #define LV_THEME_DEFAULT_GROW 1
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif
#define LV_USE_THEME_SIMPLE 1

/*==================
 * LAYOUTS
 *===================*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*==================
 * 3RD PARTS LIBRARIES
 *===================*/
#define LV_USE_UTF8 1
#define LV_USE_ARABIC_PERSIAN_CHARS 0
#define LV_USE_BIN_DECODER 1
#define LV_USE_TJPGD 0
#define LV_USE_PNG 0
#define LV_USE_BMP 0
#define LV_USE_GIF 0
#define LV_USE_QRCODE 1
#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 0
#define LV_USE_BARCODE 0

/*==================
 * EXAMPLES & DEMOS
 *===================*/
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_MUSIC 0
#define LV_USE_DEMO_FLEX_LAYOUT 0
#define LV_USE_DEMO_MULTILANG 0

/*==================
 * FONTS
 *===================*/
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1

#endif /* LV_CONF_H */
