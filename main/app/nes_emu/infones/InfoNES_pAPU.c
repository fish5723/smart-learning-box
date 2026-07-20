/**
 * @file InfoNES_pAPU.c
 * @brief InfoNES 伪音频处理单元 — stub
 *
 * NES APU 有 5 个声道: 2×矩形波, 三角波, 噪声, DMC。
 * 完整实现生成 44.1kHz 16-bit mono PCM 采样。
 *
 * 参考: https://www.nesdev.org/wiki/APU
 */

#include "InfoNES.h"
#include "esp_log.h"

static const char *TAG = "pAPU";

/* Stub: 无音频输出 */
void InfoNES_pAPU_Init(void)
{
    ESP_LOGI(TAG, "pAPU init (stub — audio disabled)");
}

void InfoNES_pAPU_Run(int cycles)
{
    (void)cycles;
    InfoNES_SampleCount = 0;
}
