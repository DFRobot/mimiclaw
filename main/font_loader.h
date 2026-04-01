#pragma once

#include <stdio.h>
#include "esp_err.h"

/* 传入的两个 FILE* 须在整段 UI 生命周期内保持打开；字模按需在文件内 fseek/fread。 */
esp_err_t font_bins_init(FILE *f_puhui_20, FILE *f_puhui_30);
