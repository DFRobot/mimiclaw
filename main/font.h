#pragma once

#include "font_loader.h"
#include "lvgl.h"

/** 须在首次用 dfrobot_font_* 绘制前成功调用 font_bins_init()。 */
extern lv_font_t dfrobot_font_20;
extern lv_font_t dfrobot_font_16;
