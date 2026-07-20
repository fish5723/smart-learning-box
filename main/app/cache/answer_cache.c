/**
 * @file answer_cache.c
 * @brief AI 答题缓存实现 — MD5 + FatFS 文件 I/O
 */

#include "answer_cache.h"
#include "bsp/storage/sd_card.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "mbedtls/md5.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "ANSWER_CACHE";

#define CACHE_DIR            "cache"
#define CACHE_EXT            ".md5"
#define CACHE_PATH_MAX       128
#define MD5_HEX_LEN          32

/* 内部: 计算 MD5 hex 字符串, 返回 out_hex (长度至少 33) */
static void compute_md5(const char *input, char *out_hex)
{
    unsigned char digest[16];
    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts(&ctx);
    mbedtls_md5_update(&ctx, (const unsigned char *)input, strlen(input));
    mbedtls_md5_finish(&ctx, digest);
    mbedtls_md5_free(&ctx);

    for (int i = 0; i < 16; i++) {
        snprintf(out_hex + i * 2, 3, "%02x", digest[i]);
    }
    out_hex[MD5_HEX_LEN] = '\0';
}

/* 内部: 构建缓存文件完整路径 */
static void build_cache_path(const char *md5_hex, char *path, size_t size)
{
    snprintf(path, size, "%s/" CACHE_DIR "/%s" CACHE_EXT,
             sd_card_get_mount_point(), md5_hex);
}

esp_err_t answer_cache_init(void)
{
    if (!sd_card_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted, cache disabled");
        return ESP_ERR_INVALID_STATE;
    }

    char dir_path[CACHE_PATH_MAX];
    snprintf(dir_path, sizeof(dir_path), "%s/" CACHE_DIR,
             sd_card_get_mount_point());

    struct stat st;
    if (stat(dir_path, &st) != 0) {
        if (mkdir(dir_path, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to mkdir %s", dir_path);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Cache directory created: %s", dir_path);
    }

    ESP_LOGI(TAG, "Answer cache ready (%s)", dir_path);
    return ESP_OK;
}

esp_err_t answer_cache_lookup(const char *question, char *answer_buf, size_t buf_size)
{
    if (!question || !answer_buf || buf_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!sd_card_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    char md5_hex[MD5_HEX_LEN + 1];
    compute_md5(question, md5_hex);

    char path[CACHE_PATH_MAX];
    build_cache_path(md5_hex, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        return ESP_ERR_NOT_FOUND;   /* 缓存未见 → 未命中 */
    }

    /* 读第一行验证题目, 跳过 */
    char line[512];
    if (fgets(line, sizeof(line), f) == NULL) {
        fclose(f);
        ESP_LOGW(TAG, "Cache file empty: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    /* 读剩余行作为答案 */
    answer_buf[0] = '\0';
    size_t total = 0;
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t n = strlen(line);
        if (total + n < buf_size - 1) {
            memcpy(answer_buf + total, line, n);
            total += n;
            answer_buf[total] = '\0';
        } else {
            /* 答案超出缓冲区, 截断 */
            size_t remaining = buf_size - total - 1;
            if (remaining > 0) {
                memcpy(answer_buf + total, line, remaining);
                total += remaining;
                answer_buf[total] = '\0';
            }
            break;
        }
    }
    fclose(f);

    ESP_LOGI(TAG, "Cache HIT: %s.md5 (%u bytes)", md5_hex, (unsigned)total);
    return ESP_OK;
}

esp_err_t answer_cache_store(const char *question, const char *answer)
{
    if (!question || !answer) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!sd_card_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    char md5_hex[MD5_HEX_LEN + 1];
    compute_md5(question, md5_hex);

    char path[CACHE_PATH_MAX];
    build_cache_path(md5_hex, path, sizeof(path));

    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open cache file for write: %s", path);
        return ESP_FAIL;
    }

    /* 第一行: 题目原文 (校验), 后续: 答案 */
    fprintf(f, "%s\n", question);
    fprintf(f, "%s", answer);
    fflush(f);
    fclose(f);

    size_t answer_len = strlen(answer);
    ESP_LOGI(TAG, "Cache STORE: %s.md5 (%u bytes)", md5_hex, (unsigned)answer_len);
    return ESP_OK;
}

bool answer_cache_is_available(void)
{
    if (!sd_card_is_mounted()) {
        return false;
    }

    char dir_path[CACHE_PATH_MAX];
    snprintf(dir_path, sizeof(dir_path), "%s/" CACHE_DIR,
             sd_card_get_mount_point());

    struct stat st;
    return (stat(dir_path, &st) == 0);
}
