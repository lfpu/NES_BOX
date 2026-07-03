#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* 基本配置 */
#define LV_COLOR_DEPTH          16
#define LV_COLOR_16_SWAP        1
#define LV_MEM_CUSTOM           1
#define LV_MEM_CUSTOM_INCLUDE   <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC     malloc
#define LV_MEM_CUSTOM_FREE      free
#define LV_MEM_CUSTOM_REALLOC   realloc

/* 显示 */
#define LV_HOR_RES_MAX         600
#define LV_VER_RES_MAX         450
#define LV_DPI_DEF             130

/* 字体 */
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_28  1
#define LV_FONT_MONTSERRAT_32  1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_26 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT        &lv_font_montserrat_16

/* 控件 */
#define LV_USE_LABEL           1
#define LV_USE_BTN             1
#define LV_USE_BTNMATRIX       1
#define LV_USE_SLIDER          1
#define LV_USE_ARC             1
#define LV_USE_BAR             1
#define LV_USE_SWITCH          1
#define LV_USE_TEXTAREA        1
#define LV_USE_TABLE           1
#define LV_USE_CHECKBOX        1
#define LV_USE_DROPDOWN        1
#define LV_USE_ROLLER          1
#define LV_USE_IMG             1
#define LV_USE_LINE            1
#define LV_USE_CANVAS          0
#define LV_USE_CHART           1
#define LV_USE_METER           1
#define LV_USE_MSGBOX          1
#define LV_USE_SPINBOX         1
#define LV_USE_SPINNER         1
#define LV_USE_TABVIEW         1
#define LV_USE_TILEVIEW        1
#define LV_USE_WIN             1
#define LV_USE_SPAN            1
#define LV_USE_LED             1
#define LV_USE_LIST            1
#define LV_USE_MENU            1
#define LV_USE_COLORWHEEL      1
#define LV_USE_IMGBTN          1
#define LV_USE_KEYBOARD        1
#define LV_USE_CALENDAR        1

/* 主题 */
#define LV_USE_THEME_DEFAULT   1
#define LV_THEME_DEFAULT_DARK  1

/* 动画 */
#define LV_USE_ANIMATION       1
#define LV_USE_SHADOW          1
#define LV_USE_BLEND_MODES     1
#define LV_USE_OPA_SCALE       1
#define LV_USE_IMG_TRANSFORM   1

/* 日志（调试时开启） */
#define LV_USE_LOG             0
#define LV_LOG_LEVEL           LV_LOG_LEVEL_WARN

/* Tick */
#define LV_TICK_CUSTOM         1
#define LV_TICK_CUSTOM_INCLUDE <Arduino.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/* 文件系统 — SD 卡通过 stdio（VFS /sd mount）访问 */
#define LV_USE_FS_STDIO  1
#define LV_FS_STDIO_LETTER 'S'
#define LV_FS_STDIO_PATH "/sd"

/* 图片缓存 — 预取前后各一张，缓存 20 张解码图避免反复读 SD */
#define LV_IMG_CACHE_DEF_SIZE  20

/* 图片解码器 */
#define LV_USE_PNG          1

#endif /* LV_CONF_H */

/* 确保你的 lv_conf.h 中包含以下配置 */


/* 内存 - 使用PSRAM */

#define LV_MEM_SIZE (128 * 1024)

/* 字体 - 确保启用以下字体 */


/* 默认字体 */
#define LV_FONT_DEFAULT &lv_font_montserrat_16

/* 动画 */
#define LV_USE_FLEX 1



/* 符号字体 */
#define LV_USE_FONT_SYMBOL 1


