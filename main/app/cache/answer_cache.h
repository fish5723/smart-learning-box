/**
 * @file answer_cache.h
 * @brief AI 答题缓存 — MD5(题目) → SD 卡缓存，重复题秒出
 *
 * 设计:
 *   - 对 OCR 识别出的题目文本计算 MD5
 *   - 缓存文件: /sdcard/cache/<md5_hex>.md5
 *   - 文件内容: 第 1 行为题目原文 (校验), 后续行为答案
 *   - Phase 1 完成后先查缓存, 命中则跳过 Phase 2 API 调用
 *
 * 依赖:
 *   - SD 卡已挂载 (sd_card_is_mounted())
 *   - mbedtls MD5
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化答题缓存子系统
 *
 * 创建 /sdcard/cache/ 目录。需在 SD 卡挂载后调用。
 *
 * @return ESP_OK 成功; ESP_ERR_INVALID_STATE SD 卡未挂载
 */
esp_err_t answer_cache_init(void);

/**
 * @brief 按题目文本查找缓存
 *
 * 内部对 question 做 MD5, 定位缓存文件并读取答案内容。
 *
 * @param[in]  question   题目原文 (OCR 识别结果)
 * @param[out] answer_buf 答案内容输出缓冲区 (调用方分配)
 * @param[in]  buf_size   answer_buf 大小
 * @return ESP_OK 命中 (answer_buf 已填充)
 * @return ESP_ERR_NOT_FOUND 未命中
 * @return ESP_ERR_INVALID_STATE SD 卡未挂载
 */
esp_err_t answer_cache_lookup(const char *question, char *answer_buf, size_t buf_size);

/**
 * @brief 缓存答案到 SD 卡
 *
 * 若同一题目已有缓存则覆盖。文件名 = MD5(question).md5。
 *
 * @param question 题目原文
 * @param answer   完整答案 (Phase 2 LLM 输出)
 * @return ESP_OK 成功
 * @return ESP_ERR_INVALID_STATE SD 卡未挂载
 */
esp_err_t answer_cache_store(const char *question, const char *answer);

/**
 * @brief 查询缓存子系统是否就绪
 * @return true SD 卡已挂载且缓存目录存在
 */
bool answer_cache_is_available(void);

#ifdef __cplusplus
}
#endif
