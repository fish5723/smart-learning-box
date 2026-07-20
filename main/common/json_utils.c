/**
 * @file json_utils.c
 * @brief 安全 JSON 工具 — cJSON 最小包装实现
 */
#include "json_utils.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "JSON_UTILS";

json_handle_t json_utils_create(void)
{
    return (json_handle_t)cJSON_CreateObject();
}

void json_utils_destroy(json_handle_t h)
{
    if (h) cJSON_Delete((cJSON *)h);
}

const char* json_get_string(json_handle_t h, const char *key)
{
    if (!h || !key) return NULL;
    cJSON *item = cJSON_GetObjectItem((cJSON *)h, key);
    if (!item) return NULL;
    if (cJSON_IsString(item)) return item->valuestring;
    return NULL;
}

int64_t json_get_int(json_handle_t h, const char *key, int64_t default_val)
{
    if (!h || !key) return default_val;
    cJSON *item = cJSON_GetObjectItem((cJSON *)h, key);
    if (!item) return default_val;
    if (cJSON_IsNumber(item)) return (int64_t)item->valuedouble;
    return default_val;
}

json_handle_t json_get_object(json_handle_t h, const char *key)
{
    if (!h || !key) return NULL;
    cJSON *item = cJSON_GetObjectItem((cJSON *)h, key);
    if (!item) return NULL;
    return (json_handle_t)item;
}

int json_array_size(json_handle_t h, const char *key)
{
    if (!h || !key) return 0;
    cJSON *item = cJSON_GetObjectItem((cJSON *)h, key);
    if (!item) return 0;
    if (cJSON_IsArray(item)) return cJSON_GetArraySize(item);
    return 0;
}

void json_set_string(json_handle_t h, const char *key, const char *val)
{
    if (!h || !key || !val) return;
    cJSON *item = cJSON_GetObjectItem((cJSON *)h, key);
    if (item) {
        cJSON_DeleteItemFromObject((cJSON *)h, key);
    }
    cJSON_AddStringToObject((cJSON *)h, key, val);
}

char* json_to_string(json_handle_t h)
{
    if (!h) return NULL;
    return cJSON_PrintUnformatted((cJSON *)h);
}

json_handle_t json_parse(const char *str)
{
    if (!str) return NULL;
    return (json_handle_t)cJSON_Parse(str);
}
