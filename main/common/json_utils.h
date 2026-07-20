/**
 * @file json_utils.h
 * @brief 安全 JSON 工具 — cJSON 的最小包装
 *
 * 只提供 8 个核心函数，内部全部 NULL guard。
 * 不包装所有 cJSON API，避免重复造轮子。
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* json_handle_t;

/* ── 创建/销毁 ── */
json_handle_t json_utils_create(void);
void          json_utils_destroy(json_handle_t h);

/* ── 读取 (全 NULL guard) ── */
const char*   json_get_string(json_handle_t h, const char *key);
int64_t       json_get_int(json_handle_t h, const char *key, int64_t default_val);
json_handle_t json_get_object(json_handle_t h, const char *key);
int           json_array_size(json_handle_t h, const char *key);

/* ── 写入 ── */
void          json_set_string(json_handle_t h, const char *key, const char *val);

/* ── 序列化 ── */
char*         json_to_string(json_handle_t h);     /* caller 必须 free() */
json_handle_t json_parse(const char *str);          /* caller 必须 json_utils_destroy() */

#ifdef __cplusplus
}
#endif
