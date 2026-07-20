/**
 * @file nes_game_list_ui.h
 * @brief NES 游戏列表页面 — lv_list 分页浏览 (每页最多 30 条)
 */

#pragma once

#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void nes_game_list_ui_init(void);
void nes_game_list_ui_show(uint8_t type_id);
void nes_game_list_ui_hide(void);

#ifdef __cplusplus
}
#endif
