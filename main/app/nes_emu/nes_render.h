/**
 * @file nes_render.h
 * @brief NES 帧缓冲渲染模块 — RGB565 → LVGL canvas → RGB888 LCD
 *
 * 负责:
 *   - 初始化 LVGL canvas (256×240 → 2x 缩放至 512×480)
 *   - InfoNES RGB565 framebuffer → RGB888 canvas 转换
 *   - 测试彩条 (Phase 3 验证用, 不依赖真实 ROM)
 */

#pragma once

#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NES 原生分辨率 */
#define NES_RENDER_W  256
#define NES_RENDER_H  240

/**
 * @brief 初始化渲染器 — 分配 PSRAM canvas buffer, 创建 LVGL canvas
 * @param parent  canvas 父对象 (通常为游戏画面的背景容器)
 */
void nes_render_init(lv_obj_t *parent);

/**
 * @brief 更新 canvas 内容 (RGB565 → RGB888 转换 + 写入 canvas + invalidate)
 * @param buffer  256×240 RGB565 帧缓冲 (InfoNES_FrameBuffer)
 *
 * @note 必须在 LVGL 任务上下文中调用 (或持 esp_lv_adapter_lock)。
 */
void nes_render_update(const uint16_t *buffer);

/**
 * @brief 显示 SMPTE 彩条测试图案 (不依赖 ROM / InfoNES_FrameBuffer)
 */
void nes_render_show_test_pattern(void);

/**
 * @brief 获取 canvas 对象 (供 UI 层对齐/定位)
 */
lv_obj_t *nes_render_get_canvas(void);

#ifdef __cplusplus
}
#endif
