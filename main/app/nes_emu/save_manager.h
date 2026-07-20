/**
 * @file save_manager.h
 * @brief NES 游戏存档管理 — 保存/读取游戏进度到 TF 卡
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 保存当前游戏进度
 *
 * 将模拟器状态 (CPU 寄存器、内存、SRAM) 序列化到 /sdcard/saves/<rom_name>.sav
 *
 * @param rom_name ROM 名称 (不含路径)
 * @return ESP_OK 成功
 */
esp_err_t save_manager_save(const char *rom_name);

/**
 * @brief 加载游戏进度
 *
 * 从 /sdcard/saves/<rom_name>.sav 反序列化模拟器状态
 *
 * @param rom_name ROM 名称 (不含路径)
 * @return ESP_OK 成功, ESP_ERR_NOT_FOUND 存档不存在
 */
esp_err_t save_manager_load(const char *rom_name);

/**
 * @brief 检查存档是否存在
 * @return true 存档文件存在
 */
bool save_manager_exists(const char *rom_name);

#ifdef __cplusplus
}
#endif