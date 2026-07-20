/**
 * @file wrong_book.h
 * @brief 错题本 — 拍照识别后标记错题, 存入 SD 卡供复习
 *
 * 数据格式: /sdcard/wrong_book/index.json (cJSON 数组)
 * 示例:
 *   [{"id":1, "question":"...", "subject":"数学",
 *     "tags":"二次函数,顶点", "expected_answer":"",
 *     "timestamp":"2026-07-04 15:30", "review_count":0}]
 *
 * 依赖:
 *   - SD 卡已挂载
 *   - cJSON (已链接)
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WRONG_BOOK_Q_LEN        512
#define WRONG_BOOK_SUBJECT_LEN  32
#define WRONG_BOOK_TAGS_LEN     128
#define WRONG_BOOK_TS_LEN       20

/** @brief 错题条目 */
typedef struct {
    int    id;
    char   question[WRONG_BOOK_Q_LEN];
    char   subject[WRONG_BOOK_SUBJECT_LEN];
    char   tags[WRONG_BOOK_TAGS_LEN];
    char   expected_answer[WRONG_BOOK_Q_LEN];
    char   timestamp[WRONG_BOOK_TS_LEN];
    int    review_count;
} wrong_entry_t;

/**
 * @brief 初始化错题本子系统
 *
 * 创建 /sdcard/wrong_book/ 目录和 index.json (如不存在)。
 *
 * @return ESP_OK 成功
 */
esp_err_t wrong_book_init(void);

/**
 * @brief 添加一条错题
 *
 * 自动分配递增 ID, 时间戳为当前时间。
 *
 * @param question        题目文本
 * @param subject         学科 (如 "数学", 可为 NULL)
 * @param tags            知识点标签 (逗号分隔, 可为 NULL)
 * @param expected_answer 正确答案 (可为空或 NULL)
 * @return ESP_OK 成功; ESP_ERR_INVALID_STATE SD 未挂载
 */
esp_err_t wrong_book_add(const char *question, const char *subject,
                          const char *tags, const char *expected_answer);

/**
 * @brief 列出所有错题, 最新在前
 *
 * @param[out] entries    输出数组 (调用方分配)
 * @param[in]  max_count  数组容量
 * @param[out] out_count  实际条目数
 * @return ESP_OK 成功
 */
esp_err_t wrong_book_list(wrong_entry_t *entries, int max_count,
                           int *out_count);

/**
 * @brief 删除指定 ID 的错题
 *
 * @param id 条目 ID
 * @return ESP_OK 成功; ESP_ERR_NOT_FOUND ID 不存在
 */
esp_err_t wrong_book_delete(int id);

/**
 * @brief 标记为已复习 (review_count++)
 *
 * @param id 条目 ID
 * @return ESP_OK 成功
 */
esp_err_t wrong_book_mark_reviewed(int id);

#ifdef __cplusplus
}
#endif
