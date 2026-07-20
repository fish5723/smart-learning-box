/**
 * @file nes_emu_ui.h
 * @brief NES 模拟器 UI 层 — ROM 浏览器 + 游戏画面 + 虚拟手柄
 */

#pragma once

#include "nes_emu.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 NES 模拟器 UI
 */
void nes_emu_ui_init(void);

/**
 * @brief 显示 ROM 浏览器页面
 */
void nes_emu_ui_show(void);

/**
 * @brief 隐藏 NES UI 页面
 */
void nes_emu_ui_hide(void);

/**
 * @brief 切换到游戏画面 (加载 ROM 后调用)
 * @param rom_name ROM 名称 (显示用)
 */
void nes_emu_ui_show_game(const char *rom_name);

/**
 * @brief 获取 NES 画面区域对象 (用于放置帧缓冲)
 * @return lv_obj_t*
 */
lv_obj_t *nes_emu_ui_get_screen_area(void);

#ifdef __cplusplus
}
#endif
