/**
 * @file wrong_book.c
 * @brief 错题本实现 — cJSON + FatFS 文件 I/O
 */

#include "wrong_book.h"
#include "bsp/storage/sd_card.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

static const char *TAG = "WRONG_BOOK";

#define BOOK_DIR             "wrong_book"
#define INDEX_FILE           "index.json"
#define BOOK_PATH_MAX        160
#define MAX_ENTRIES           100

/* ── 构建路径 ── */
static void build_dir_path(char *buf, size_t size)
{
    snprintf(buf, size, "%s/" BOOK_DIR, sd_card_get_mount_point());
}

static void build_index_path(char *buf, size_t size)
{
    snprintf(buf, size, "%s/" BOOK_DIR "/" INDEX_FILE,
             sd_card_get_mount_point());
}

/* ── 生成时间戳字符串 ── */
static void make_ts(char *buf, size_t size)
{
    time_t now = time(NULL);
    if (now > 1000000000) {
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        snprintf(buf, size, "%04u-%02u-%02u %02u:%02u:%02u",
                 (unsigned)(tm_now.tm_year + 1900) % 10000,
                 (unsigned)(tm_now.tm_mon + 1) % 100,
                 (unsigned)tm_now.tm_mday,
                 (unsigned)tm_now.tm_hour,
                 (unsigned)tm_now.tm_min,
                 (unsigned)tm_now.tm_sec);
    } else {
        snprintf(buf, size, "boot");
    }
}

/* ── 内部: 读取 index.json 并解析为 cJSON 数组 ── */
static cJSON *load_index(void)
{
    char path[BOOK_PATH_MAX];
    build_index_path(path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return cJSON_CreateArray();  /* 新文件 */

    /* 获取文件大小 */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 256 * 1024) {
        fclose(f);
        return cJSON_CreateArray();
    }

    char *json_str = malloc(fsize + 1);
    if (!json_str) {
        fclose(f);
        return cJSON_CreateArray();
    }

    fread(json_str, 1, fsize, f);
    json_str[fsize] = '\0';
    fclose(f);

    cJSON *arr = cJSON_Parse(json_str);
    free(json_str);

    if (!arr || !cJSON_IsArray(arr)) {
        if (arr) cJSON_Delete(arr);
        return cJSON_CreateArray();
    }

    return arr;
}

/* ── 内部: 写入 index.json ── */
static esp_err_t save_index(const cJSON *arr)
{
    if (!arr) return ESP_ERR_INVALID_ARG;

    char path[BOOK_PATH_MAX];
    build_index_path(path, sizeof(path));

    char *json_str = cJSON_PrintUnformatted(arr);
    if (!json_str) return ESP_ERR_NO_MEM;

    FILE *f = fopen(path, "w");
    if (!f) {
        free(json_str);
        ESP_LOGE(TAG, "Failed to open index for write: %s", path);
        return ESP_FAIL;
    }

    fprintf(f, "%s", json_str);
    fflush(f);
    fclose(f);
    free(json_str);

    return ESP_OK;
}

/* ── 找出最大 ID ── */
static int find_max_id(const cJSON *arr)
{
    int max_id = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr) {
        cJSON *id_obj = cJSON_GetObjectItem(item, "id");
        if (cJSON_IsNumber(id_obj) && id_obj->valueint > max_id) {
            max_id = id_obj->valueint;
        }
    }
    return max_id;
}

/* ═══════════════════════════════════════════════
   公开 API
   ═══════════════════════════════════════════════ */

esp_err_t wrong_book_init(void)
{
    if (!sd_card_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted, wrong book disabled");
        return ESP_ERR_INVALID_STATE;
    }

    char dir_path[BOOK_PATH_MAX];
    build_dir_path(dir_path, sizeof(dir_path));

    struct stat st;
    if (stat(dir_path, &st) != 0) {
        if (mkdir(dir_path, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to mkdir %s (errno=%d: %s)",
                     dir_path, errno, strerror(errno));
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Wrong book directory created: %s", dir_path);
    }

    /* 若 index.json 不存在则创建空数组 */
    char idx_path[BOOK_PATH_MAX];
    build_index_path(idx_path, sizeof(idx_path));
    if (stat(idx_path, &st) != 0) {
        cJSON *arr = cJSON_CreateArray();
        if (arr) {
            save_index(arr);
            cJSON_Delete(arr);
        }
    }

    ESP_LOGI(TAG, "Wrong book ready");
    return ESP_OK;
}

esp_err_t wrong_book_add(const char *question, const char *subject,
                          const char *tags, const char *expected_answer)
{
    if (!question) return ESP_ERR_INVALID_ARG;
    if (!sd_card_is_mounted()) return ESP_ERR_INVALID_STATE;

    cJSON *arr = load_index();
    if (!arr) return ESP_ERR_NO_MEM;

    /* 限制条目数 */
    if (cJSON_GetArraySize(arr) >= MAX_ENTRIES) {
        ESP_LOGW(TAG, "Max entries (%d) reached, removing oldest", MAX_ENTRIES);
        cJSON_DeleteItemFromArray(arr, 0);
    }

    int new_id = find_max_id(arr) + 1;

    char ts[WRONG_BOOK_TS_LEN];
    make_ts(ts, sizeof(ts));

    cJSON *entry = cJSON_CreateObject();
    cJSON_AddNumberToObject(entry, "id", new_id);
    cJSON_AddStringToObject(entry, "question", question);
    cJSON_AddStringToObject(entry, "subject", subject ? subject : "");
    cJSON_AddStringToObject(entry, "tags", tags ? tags : "");
    cJSON_AddStringToObject(entry, "expected_answer",
                             expected_answer ? expected_answer : "");
    cJSON_AddStringToObject(entry, "timestamp", ts);
    cJSON_AddNumberToObject(entry, "review_count", 0);

    cJSON_AddItemToArray(arr, entry);

    esp_err_t ret = save_index(arr);
    cJSON_Delete(arr);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Added entry #%d: %.40s...", new_id, question);
    }
    return ret;
}

esp_err_t wrong_book_list(wrong_entry_t *entries, int max_count,
                           int *out_count)
{
    if (!entries || max_count <= 0 || !out_count)
        return ESP_ERR_INVALID_ARG;
    *out_count = 0;

    if (!sd_card_is_mounted())
        return ESP_ERR_INVALID_STATE;

    cJSON *arr = load_index();
    if (!arr) return ESP_ERR_NO_MEM;

    int total = cJSON_GetArraySize(arr);
    for (int i = total - 1; i >= 0 && *out_count < max_count; i--) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (!item) continue;

        wrong_entry_t *e = &entries[*out_count];
        memset(e, 0, sizeof(*e));

        cJSON *id_obj = cJSON_GetObjectItem(item, "id");
        e->id = cJSON_IsNumber(id_obj) ? id_obj->valueint : 0;

        cJSON *q_obj = cJSON_GetObjectItem(item, "question");
        if (q_obj && q_obj->valuestring) {
            strncpy(e->question, q_obj->valuestring, WRONG_BOOK_Q_LEN - 1);
        }
        cJSON *s_obj = cJSON_GetObjectItem(item, "subject");
        if (s_obj && s_obj->valuestring) {
            strncpy(e->subject, s_obj->valuestring, WRONG_BOOK_SUBJECT_LEN - 1);
        }
        cJSON *t_obj = cJSON_GetObjectItem(item, "tags");
        if (t_obj && t_obj->valuestring) {
            strncpy(e->tags, t_obj->valuestring, WRONG_BOOK_TAGS_LEN - 1);
        }
        cJSON *ea_obj = cJSON_GetObjectItem(item, "expected_answer");
        if (ea_obj && ea_obj->valuestring) {
            strncpy(e->expected_answer, ea_obj->valuestring, WRONG_BOOK_Q_LEN - 1);
        }
        cJSON *ts_obj = cJSON_GetObjectItem(item, "timestamp");
        if (ts_obj && ts_obj->valuestring) {
            strncpy(e->timestamp, ts_obj->valuestring, WRONG_BOOK_TS_LEN - 1);
        }
        cJSON *rc_obj = cJSON_GetObjectItem(item, "review_count");
        e->review_count = cJSON_IsNumber(rc_obj) ? rc_obj->valueint : 0;

        (*out_count)++;
    }

    cJSON_Delete(arr);
    ESP_LOGI(TAG, "Listed %d entries", *out_count);
    return ESP_OK;
}

esp_err_t wrong_book_delete(int id)
{
    if (!sd_card_is_mounted()) return ESP_ERR_INVALID_STATE;

    cJSON *arr = load_index();
    if (!arr) return ESP_ERR_NO_MEM;

    bool found = false;
    int total = cJSON_GetArraySize(arr);
    for (int i = 0; i < total; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        cJSON *id_obj = cJSON_GetObjectItem(item, "id");
        if (cJSON_IsNumber(id_obj) && id_obj->valueint == id) {
            cJSON_DeleteItemFromArray(arr, i);
            found = true;
            break;
        }
    }

    if (!found) {
        cJSON_Delete(arr);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = save_index(arr);
    cJSON_Delete(arr);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Deleted entry #%d", id);
    }
    return ret;
}

esp_err_t wrong_book_mark_reviewed(int id)
{
    if (!sd_card_is_mounted()) return ESP_ERR_INVALID_STATE;

    cJSON *arr = load_index();
    if (!arr) return ESP_ERR_NO_MEM;

    bool found = false;
    int total = cJSON_GetArraySize(arr);
    for (int i = 0; i < total; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        cJSON *id_obj = cJSON_GetObjectItem(item, "id");
        if (cJSON_IsNumber(id_obj) && id_obj->valueint == id) {
            cJSON *rc_obj = cJSON_GetObjectItem(item, "review_count");
            int rc = cJSON_IsNumber(rc_obj) ? rc_obj->valueint : 0;
            cJSON_ReplaceItemInObject(item, "review_count",
                                       cJSON_CreateNumber(rc + 1));
            found = true;
            break;
        }
    }

    if (!found) {
        cJSON_Delete(arr);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = save_index(arr);
    cJSON_Delete(arr);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Marked entry #%d as reviewed", id);
    }
    return ret;
}
