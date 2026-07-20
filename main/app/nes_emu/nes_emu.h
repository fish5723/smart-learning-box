/**
 * @file nes_emu.h
 * @brief NES 模拟器模块入口 — 基于 InfoNES 内核
 *
 * 功能:
 *   - NES/Famicom 模拟器核心集成
 *   - ROM 文件从 TF 卡加载
 *   - 触摸虚拟手柄
 *   - 存档/读档管理
 *
 * 架构:
 *   Core 0 — NES 模拟器主循环 (6502 + PPU + APU)
 *   Core 1 — LVGL UI (游戏画面 + 虚拟手柄 + ROM 浏览器)
 *
 * 依赖:
 *   - TF 卡 (存放 ROM 文件)
 *   - InfoNES 内核 (app/nes_emu/infones/)
 */

#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════
   NES 画面尺寸
   ═══════════════════════════════════════════════ */
#define NES_SCREEN_WIDTH     256
#define NES_SCREEN_HEIGHT    240
#define NES_FRAMEBUFFER_SIZE (NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT)

/* 缩放后的显示尺寸 (2x 最近邻 = 512×480, 居中于 1024×600) */
#define NES_DISPLAY_WIDTH    (NES_SCREEN_WIDTH * 2)
#define NES_DISPLAY_HEIGHT   (NES_SCREEN_HEIGHT * 2)

/* ═══════════════════════════════════════════════
   NES 按键定义 (与虚拟手柄对应)
   必须与 core/ppu/nesinput.h 的 INP_PAD_* 位掩码一致
   ═══════════════════════════════════════════════ */
typedef enum {
    NES_KEY_A      = 0x01,  /* INP_PAD_A */
    NES_KEY_B      = 0x02,  /* INP_PAD_B */
    NES_KEY_SELECT = 0x04,  /* INP_PAD_SELECT */
    NES_KEY_START  = 0x08,  /* INP_PAD_START */
    NES_KEY_UP     = 0x10,  /* INP_PAD_UP */
    NES_KEY_DOWN   = 0x20,  /* INP_PAD_DOWN */
    NES_KEY_LEFT   = 0x40,  /* INP_PAD_LEFT */
    NES_KEY_RIGHT  = 0x80,  /* INP_PAD_RIGHT */
} nes_key_t;

/* ═══════════════════════════════════════════════
   ROM 条目信息
   ═══════════════════════════════════════════════ */
#define NES_ROM_NAME_MAX  128

typedef struct {
    char        name[NES_ROM_NAME_MAX];
    char        path[256];
    uint32_t    size;           /* 文件大小 (bytes) */
    bool        is_nes;         /* 是否为 .nes 文件 */
} nes_rom_entry_t;

/* ═══════════════════════════════════════════════
   公开 API
   ═══════════════════════════════════════════════ */

/**
 * @brief 初始化 NES 模拟器模块
 *
 * 注册 LVGL 页面, 初始化虚拟手柄, 准备 ROM 列表。
 * 必须在 SD 卡初始化之后调用。
 *
 * @return ESP_OK 成功
 */
esp_err_t nes_emu_init(void);

/**
 * @brief 反初始化 NES 模拟器
 *
 * 停止模拟器循环, 释放内存。
 */
void nes_emu_deinit(void);

/**
 * @brief 显示 NES 模拟器主页面 (ROM 浏览器)
 */
void nes_emu_show(void);

/**
 * @brief 隐藏 NES 模拟器页面
 */
void nes_emu_hide(void);

/**
 * @brief 加载并运行 ROM
 * @param rom_path ROM 文件在 SD 卡上的完整路径
 * @return ESP_OK 加载成功并开始模拟
 */
esp_err_t nes_emu_load_rom(const char *rom_path);

/**
 * @brief 停止当前运行的 ROM
 */
void nes_emu_stop(void);

/**
 * @brief 暂停/恢复模拟
 */
void nes_emu_toggle_pause(void);

/**
 * @brief 获取 NES 帧缓冲 (RGB565 格式)
 * @return 256×240 像素的 RGB565 缓冲区指针
 */
uint16_t *nes_emu_get_framebuffer(void);

/**
 * @brief 处理 NES 按键输入 (由虚拟手柄 UI 调用)
 * @param key_mask 按键位掩码 (nes_key_t 组合)
 * @param pressed   true=按下, false=释放
 */
void nes_emu_input(uint8_t key_mask, bool pressed);

/**
 * @brief 保存当前游戏进度到 TF 卡
 * @return ESP_OK 成功
 */
esp_err_t nes_emu_save_state(void);

/**
 * @brief 从 TF 卡加载游戏进度
 * @return ESP_OK 成功
 */
esp_err_t nes_emu_load_state(void);

/**
 * @brief 获取模拟器运行状态
 * @return true=正在运行
 */
bool nes_emu_is_running(void);

/**
 * @brief 获取当前运行的 ROM 名称
 * @return ROM 名称字符串
 */
const char *nes_emu_get_current_rom_name(void);

/**
 * @brief 获取当前帧率
 * @return FPS
 */
uint32_t nes_emu_get_fps(void);

/**
 * @brief 获取已渲染帧数
 * @return 总帧数
 */
uint32_t nes_emu_get_frame_count(void);

/**
 * @brief 获取帧就绪信号量 (供LVGL定时器使用)
 * @return 信号量句柄
 */
SemaphoreHandle_t nes_emu_get_frame_ready_sem(void);

/**
 * @brief 扫描 TF 卡中的 ROM 文件
 * @param out_list  输出 ROM 列表 (调用者释放)
 * @param out_count 输出 ROM 数量
 * @return ESP_OK 成功
 */
esp_err_t nes_emu_scan_roms(nes_rom_entry_t **out_list, int *out_count);

/**
 * @brief 释放 ROM 列表内存
 */
void nes_emu_free_rom_list(nes_rom_entry_t *list);

#ifdef __cplusplus
}
#endif