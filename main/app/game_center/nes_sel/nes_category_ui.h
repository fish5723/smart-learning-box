/**
 * @file nes_category_ui.h
 * @brief NES 游戏分类选择页面 — 10 个分类按钮, 不包含图片
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void nes_category_ui_init(void);
void nes_category_ui_show(void);
void nes_category_ui_hide(void);

#ifdef __cplusplus
}
#endif
