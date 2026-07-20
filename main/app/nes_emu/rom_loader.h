/**
 * @file rom_loader.h
 * @brief NES ROM 文件加载器 — 扫描 TF 卡 ROM 目录
 */

#pragma once

#include "nes_emu.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 扫描 SD 卡 ROM 目录, 构建游戏列表
 *
 * 扫描规则:
 *   /sdcard/ROM/<编号目录>/<目录名>.nes
 *   例如: /sdcard/ROM/0001/Donkey Kong (J) [!].nes
 *
 * @param out_list  输出 ROM 条目数组 (堆分配, 调用者使用 rom_loader_free_list 释放)
 * @param out_count 输出条目数量
 * @return ESP_OK 成功
 */
esp_err_t rom_loader_scan(nes_rom_entry_t **out_list, int *out_count);

/**
 * @brief 释放 rom_loader_scan 分配的 ROM 列表
 */
void rom_loader_free_list(nes_rom_entry_t *list);

/**
 * @brief 从文件加载 ROM 数据到内存
 *
 * 读取整个 .nes 文件到堆内存。
 * 调用者使用 rom_loader_free_data 释放。
 *
 * @param path      ROM 文件完整路径
 * @param out_data  输出数据指针 (堆分配)
 * @param out_size  输出数据大小
 * @return ESP_OK 成功
 */
esp_err_t rom_loader_load_file(const char *path, uint8_t **out_data, size_t *out_size);

/**
 * @brief 释放 rom_loader_load_file 分配的数据
 */
void rom_loader_free_data(uint8_t *data);

#ifdef __cplusplus
}
#endif
