/**
 * @file InfoNES.c
 * @brief InfoNES 模拟器内核 — ESP32-P4 移植适配层
 *
 * 此文件提供 InfoNES 核心仿真循环的最小实现骨架。
 * 完整的 InfoNES 源代码 (~3000 行) 包含完整的:
 *   - 6502 CPU 仿真
 *   - PPU (图像处理单元)
 *   - APU (音频处理单元)
 *   - Mapper/MMC 支持
 *
 * 本项目集成方式:
 *   1. 将此 stub 替换为完整的 InfoNES 源码 (从
 *      https://github.com/li2727/nesemu_esp32 获取)
 *   2. InfoNES.c + InfoNES_Mapper.c + InfoNES_pAPU.c
 *   3. 文件 I/O 适配为 fopen/fread (ESP-IDF FatFS)
 */

#include "InfoNES.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "InfoNES";

/* ═══════════════════════════════════════════════
   全局变量定义
   ═══════════════════════════════════════════════ */

/* 帧缓冲: 256×240 RGB565 — PSRAM 动态分配
   声明为指针而非数组以节省 BSS (BSS 约减 ~120KB) */
uint16_t *InfoNES_FrameBuffer = NULL;
uint8_t  InfoNES_Pad[2];
int      InfoNES_Exit = 0;
int      InfoNES_SampleCount = 0;
int      InfoNES_SampleRate = 44100;

/* ═══════════════════════════════════════════════
   ROM 数据
   ═══════════════════════════════════════════════ */
static uint8_t  *s_rom_data = NULL;
static size_t    s_rom_size = 0;

/* ═══════════════════════════════════════════════
   InfoNES API 实现 (stub — 待替换为完整内核)
   ═══════════════════════════════════════════════ */

void InfoNES_Setup(void)
{
    ESP_LOGI(TAG, "InfoNES_Setup() — allocating framebuffer in PSRAM");

    /* 从 PSRAM 分配帧缓冲 (BSS 减 120 KB) */
    if (!InfoNES_FrameBuffer) {
        InfoNES_FrameBuffer = heap_caps_malloc(
            NES_DISP_WIDTH * NES_DISP_HEIGHT * sizeof(uint16_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }

    if (InfoNES_FrameBuffer) {
        memset(InfoNES_FrameBuffer, 0,
               NES_DISP_WIDTH * NES_DISP_HEIGHT * sizeof(uint16_t));
    } else {
        ESP_LOGE(TAG, "Failed to allocate framebuffer in PSRAM");
    }

    memset(InfoNES_Pad, 0, sizeof(InfoNES_Pad));
    InfoNES_Exit = 0;
}

int InfoNES_Load(const char *path)
{
    ESP_LOGI(TAG, "InfoNES_Load(%s)", path);

    /* 打开 ROM 文件 */
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open ROM: %s", path);
        return -1;
    }

    /* 获取文件大小 */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 16 || fsize > (4 * 1024 * 1024)) {
        ESP_LOGE(TAG, "Invalid ROM size: %ld", fsize);
        fclose(f);
        return -2;
    }

    /* 分配内存并读取 */
    free(s_rom_data);
    s_rom_data = malloc(fsize);
    if (!s_rom_data) {
        ESP_LOGE(TAG, "Out of memory for ROM (%ld bytes)", fsize);
        fclose(f);
        return -3;
    }

    size_t read = fread(s_rom_data, 1, fsize, f);
    fclose(f);

    if (read != (size_t)fsize) {
        ESP_LOGE(TAG, "Short read: %zu/%ld", read, fsize);
        free(s_rom_data);
        s_rom_data = NULL;
        return -4;
    }

    s_rom_size = fsize;

    /* 验证 NES ROM 头部 (NES\x1A) */
    if (s_rom_size < 16 || s_rom_data[0] != 'N' || s_rom_data[1] != 'E'
        || s_rom_data[2] != 'S' || s_rom_data[3] != 0x1A) {
        ESP_LOGW(TAG, "Invalid NES ROM header: %02X %02X %02X %02X",
                 s_rom_data[0], s_rom_data[1], s_rom_data[2], s_rom_data[3]);
        /* 不返回错误，允许尝试运行 */
    }

    ESP_LOGI(TAG, "ROM loaded: %zu bytes, mapper=%d, PRG=%d×16KB, CHR=%d×8KB",
             s_rom_size,
             ((s_rom_data[6] >> 4) | (s_rom_data[7] & 0xF0)),
             s_rom_data[4],
             s_rom_data[5]);

    return 0;
}

void InfoNES_Release(void)
{
    ESP_LOGI(TAG, "InfoNES_Release()");

    /* 只释放 ROM 数据。帧缓冲在 InfoNES_Setup() 中一次性分配、为持久资源，
       不能在此释放：nes_emu_load_rom() 会在运行前调用 Release()，若把
       FrameBuffer 置 NULL，InfoNES_MainLoop() 写入 NULL[0] 会触发 Store fault。 */
    if (s_rom_data) {
        free(s_rom_data);
        s_rom_data = NULL;
        s_rom_size = 0;
    }
}

void InfoNES_MainLoop(void)
{
    /* 帧缓冲回退保护: 若为 NULL 则尝试重分配 (发生在线程安全窗口内) */
    if (!InfoNES_FrameBuffer) {
        ESP_LOGW(TAG, "MainLoop: FrameBuffer is NULL — re-allocating...");
        InfoNES_Setup();
        if (!InfoNES_FrameBuffer) {
            ESP_LOGE(TAG, "MainLoop: FrameBuffer re-allocation failed, skipped");
            return;
        }
        ESP_LOGI(TAG, "MainLoop: FrameBuffer re-allocated OK");
    }

    /*
     * Stub 实现: 绘制测试图案到帧缓冲
     *
     * 完整实现应包含:
     *   1. 6502 CPU 执行 ~29780 个周期 (一帧)
     *   2. PPU 渲染 240 行扫描线
     *   3. APU 生成音频采样
     *   4. 每帧结束时调用 InfoNES_LoadFrame()
     */

    /* 渐变测试图案: 用于验证帧缓冲显示正常 */
    static int frame = 0;
    frame++;

    for (int y = 0; y < NES_DISP_HEIGHT; y++) {
        for (int x = 0; x < NES_DISP_WIDTH; x++) {
            /* 彩色条纹图案 */
            uint8_t r = (uint8_t)((x + frame) & 0xFF);
            uint8_t g = (uint8_t)((y + frame) & 0xFF);
            uint8_t b = (uint8_t)(((x ^ y) + frame) & 0xFF);

            /* RGB565 格式 */
            uint16_t rgb565 = (uint16_t)(((r >> 3) << 11)
                                       | ((g >> 2) << 5)
                                       | (b >> 3));
            InfoNES_FrameBuffer[y * NES_DISP_WIDTH + x] = rgb565;
        }
    }

    /* 绘制边框 */
    for (int x = 0; x < NES_DISP_WIDTH; x++) {
        InfoNES_FrameBuffer[0 * NES_DISP_WIDTH + x] = 0xFFFF;
        InfoNES_FrameBuffer[(NES_DISP_HEIGHT - 1) * NES_DISP_WIDTH + x] = 0xFFFF;
    }
    for (int y = 0; y < NES_DISP_HEIGHT; y++) {
        InfoNES_FrameBuffer[y * NES_DISP_WIDTH + 0] = 0xFFFF;
        InfoNES_FrameBuffer[y * NES_DISP_WIDTH + (NES_DISP_WIDTH - 1)] = 0xFFFF;
    }

    /* 通知外部模块帧已完成 */
    InfoNES_LoadFrame();
}

int InfoNES_CheckRom(const uint8_t *header, uint32_t size)
{
    if (size < 16) return -1;
    if (header[0] != 'N' || header[1] != 'E' || header[2] != 'S' || header[3] != 0x1A) {
        return -2;
    }
    return 0;
}
