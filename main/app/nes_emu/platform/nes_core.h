/**
 * @file nes_core.h
 * @brief NES 模拟核心集成层 — Nofrendo 核心 -> LVGL 显示管线
 *
 * API: init / load_rom / run_frame / destroy
 * 帧输出: nes_core_get_framebuffer() → nes_render_update(buffer)
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 NES 核心 (创建 CPU/PPU/APU, 分配 framebuffer)
 * @return ESP_OK 成功
 */
esp_err_t nes_core_init(void);

/**
 * @brief 加载 ROM 文件到 PSRAM, 创建 mapper, 启动模拟器
 * @param path  SD 卡上 ROM 的完整路径
 * @return ESP_OK 成功
 */
esp_err_t nes_core_load_rom(const char *path);

/**
 * @brief 运行一帧模拟 (262 扫描线, CPU + PPU)
 *
 * 渲染结果写入内部 256×240 RGB565 帧缓冲。
 * 调用后可通过 nes_core_get_framebuffer() 获取。
 */
void nes_core_run_frame(void);

/**
 * @brief 停止模拟, 释放资源
 */
void nes_core_destroy(void);

/**
 * @brief 获取 RGB565 帧缓冲指针 (256×240)
 * @return uint16_t* 或 NULL (未初始化时)
 */
uint16_t *nes_core_get_framebuffer(void);

/**
 * @brief 设置玩家1输入状态
 * @param key_mask  按键掩码 (NES_KEY_*)
 * @param pressed   true=按下, false=释放
 */
void nes_core_set_input(uint8_t key_mask, bool pressed);

#ifdef __cplusplus
}
#endif