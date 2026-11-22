#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_MEM_CUSTOM 0
#if defined(DESKTOP_SIM)
// The companion test lab shares this heap with the 240x320 panel.
#define LV_MEM_SIZE (96U * 1024U)
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#define LV_ASSERT_HANDLER abort();
#else
#define LV_MEM_SIZE (48U * 1024U)
#endif
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(panel_mono_16) LV_FONT_DECLARE(panel_mono_40)
#define LV_FONT_DEFAULT &panel_mono_16
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_THEME_DEFAULT 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_48 1

#endif
