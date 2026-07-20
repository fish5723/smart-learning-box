/**
 * @file InfoNES_Mapper.c
 * @brief InfoNES Mapper 支持 — stub
 *
 * NES Mapper 负责处理 ROM 的地址映射和 bank 切换。
 * 完整实现需支持 Mapper 0 (NROM), 1 (MMC1), 2 (UNROM), 3 (CNROM), 4 (MMC3) 等。
 *
 * 参考: https://www.nesdev.org/wiki/Mapper
 */

#include "InfoNES.h"
#include "esp_log.h"

static const char *TAG = "Mapper";

/* Stub: 无 mapper 操作 */
void InfoNES_InitMapper(void)
{
    ESP_LOGI(TAG, "Mapper init (stub — no mapper support yet)");
}
