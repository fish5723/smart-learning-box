/**
 * @file save_manager.c
 * @brief NES 游戏存档管理实现
 *
 * 存档格式 (简化):
 *   - 文件头: "NESSAVE" (8 bytes)
 *   - ROM 名称: NES_ROM_NAME_MAX bytes
 *   - 模拟器状态: CPU 寄存器 + PPU 状态 + RAM + VRAM
 *   - 文件尾: CRC32
 *
 * 存档位置: /sdcard/saves/<rom_name>.sav
 */

#include "save_manager.h"
#include "nes_emu.h"
#include "bsp/storage/sd_card.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static const char *TAG = "SAVE_MGR";

/* 存档目录 */
#define SAVE_DIR  "/sdcard/saves"

/* TODO: Phase 5 — 接入 Nofrendo 存档系统 (nesstate.c) */

esp_err_t save_manager_save(const char *rom_name)
{
    if (!rom_name || !sd_card_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 确保 saves 目录存在 */
    struct stat st;
    if (stat(SAVE_DIR, &st) != 0) {
        mkdir(SAVE_DIR, 0755);
    }

    char save_path[512];
    snprintf(save_path, sizeof(save_path), "%s/%s.sav", SAVE_DIR, rom_name);

    FILE *f = fopen(save_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to create save file: %s", save_path);
        return ESP_FAIL;
    }

    /* 写入头部标识 */
    const char header[] = "NESSAVE";
    if (fwrite(header, 1, 8, f) != 8) {
        ESP_LOGE(TAG, "Failed to write header");
        fclose(f);
        return ESP_FAIL;
    }

    /* 写入 ROM 名称 (定长) */
    char name_buf[NES_ROM_NAME_MAX] = {0};
    strncpy(name_buf, rom_name, NES_ROM_NAME_MAX - 1);
    if (fwrite(name_buf, 1, NES_ROM_NAME_MAX, f) != NES_ROM_NAME_MAX) {
        ESP_LOGE(TAG, "Failed to write ROM name");
        fclose(f);
        return ESP_FAIL;
    }

    /* TODO: 写入完整的模拟器状态 (CPU/PPU/APU/RAM/SRAM) */
    /* 当前为简化实现，仅保存存档标记 */

    fclose(f);
    ESP_LOGI(TAG, "Saved: %s", save_path);
    return ESP_OK;
}

esp_err_t save_manager_load(const char *rom_name)
{
    if (!rom_name || !sd_card_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    char save_path[512];
    snprintf(save_path, sizeof(save_path), "%s/%s.sav", SAVE_DIR, rom_name);

    FILE *f = fopen(save_path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Save file not found: %s", save_path);
        return ESP_ERR_NOT_FOUND;
    }

    /* 验证头部 */
    char header[9] = {0};
    if (fread(header, 1, 8, f) != 8 || strncmp(header, "NESSAVE", 7) != 0) {
        ESP_LOGE(TAG, "Invalid save file header");
        fclose(f);
        return ESP_FAIL;
    }

    /* 跳过 ROM 名称域 */
    fseek(f, NES_ROM_NAME_MAX, SEEK_CUR);

    /* TODO: 反序列化完整模拟器状态 */

    fclose(f);
    ESP_LOGI(TAG, "Loaded: %s", save_path);
    return ESP_OK;
}

bool save_manager_exists(const char *rom_name)
{
    if (!rom_name) return false;

    char save_path[512];
    snprintf(save_path, sizeof(save_path), "%s/%s.sav", SAVE_DIR, rom_name);

    struct stat st;
    return (stat(save_path, &st) == 0);
}
