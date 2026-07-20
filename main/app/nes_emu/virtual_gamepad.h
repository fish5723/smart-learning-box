/**
 * @file virtual_gamepad.h
 * @brief NES 触摸虚拟手柄 — LVGL 触摸屏 → NES 按键映射
 */

#pragma once

#include "nes_emu.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建虚拟手柄叠加层
 *
 * 在半透明层上绘制方向键 (左下) 和 AB 按键 (右下)。
 * 使用 lv_obj 的 CLICKABLE + PRESSED/RELEASED 事件映射到 NES 按键。
 *
 * @param parent  父对象 (游戏画面 screen)
 */
void virtual_gamepad_create(lv_obj_t *parent);

/**
 * @brief 显示/隐藏虚拟手柄
 */
void virtual_gamepad_set_visible(bool visible);

#ifdef __cplusplus
}
#endif
