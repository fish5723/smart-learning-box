/**
 * @file InfoNES.h
 * @brief InfoNES 模拟器内核 — 头文件适配层
 *
 * 本项目使用 InfoNES 作为 NES 模拟器内核。
 * 此头文件提供了与外部模块交互的接口。
 *
 * 参考: https://github.com/li2727/nesemu_esp32
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════
   InfoNES 配置
   ═══════════════════════════════════════════════ */

/* PSP 优化宏 (ESP32 不适用) */
#define InfoNES_USE_PSP    0

/* 帧缓冲大小 */
#define NES_DISP_WIDTH     256
#define NES_DISP_HEIGHT    240

/* ═══════════════════════════════════════════════
   InfoNES 全局变量 (在 InfoNES.c 中定义)
   ═══════════════════════════════════════════════ */

/* 帧缓冲: 256×240 RGB565 (在 InfoNES.c 中动态分配 PSRAM) */
extern uint16_t *InfoNES_FrameBuffer;

/* 手柄状态 (2 个控制器) */
extern uint8_t InfoNES_Pad[2];

/* 退出标志 */
extern int InfoNES_Exit;

/* 声音采样计数 */
extern int InfoNES_SampleCount;
extern int InfoNES_SampleRate;

/* ═══════════════════════════════════════════════
   InfoNES API
   ═══════════════════════════════════════════════ */

/**
 * @brief 初始化 InfoNES 内核
 */
void InfoNES_Setup(void);

/**
 * @brief 运行一帧 (6502 + PPU + APU)
 */
void InfoNES_MainLoop(void);

/**
 * @brief 从文件路径加载 ROM
 * @param path ROM 文件路径 (SD 卡上的路径)
 * @return 0 = 成功, 非 0 = 失败
 */
int InfoNES_Load(const char *path);

/**
 * @brief 释放当前 ROM 占用的资源
 */
void InfoNES_Release(void);

/**
 * @brief 检查 ROM 文件头部合法性
 */
int InfoNES_CheckRom(const uint8_t *header, uint32_t size);

/**
 * @brief 帧完成回调 — 由 InfoNES 每帧调用一次
 *
 * 需要由外部模块实现 (在 nes_emu.c 中定义)
 */
extern void InfoNES_LoadFrame(void);

/**
 * @brief 声音输出回调 — 由 InfoNES 每帧调用一次
 *
 * 需要由外部模块实现 (在 nes_emu.c 中定义)
 */
extern void InfoNES_SoundOut(int samples, void *wave[2]);

/**
 * @brief 消息框/错误提示回调
 */
extern void InfoNES_MessageBox(const char *msg);

/**
 * @brief SRAM 写入回调 (用于电池存档)
 */
extern void InfoNES_SRAMWrite(uint32_t address, uint8_t data);

#ifdef __cplusplus
}
#endif
