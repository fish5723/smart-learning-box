/**
 * @file photo_history.c
 * @brief 拍照历史实现 — FatFS 文件 I/O
 */

#include "photo_history.h"
#include "bsp/storage/sd_card.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>

static const char *TAG = "PHOTO_HIST";

#define PHOTOS_DIR           "photos"
#define PHOTOS_PATH_MAX      144

/* ── 生成时间戳文件名 (格式: YYYYMMDD_HHMMSS) ── */
static void make_timestamp(char *buf, size_t size)
{
    time_t now = time(NULL);
    if (now > 1000000000) {  /* 时间已同步 (2001年后) */
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        snprintf(buf, size, "%04u%02u%02u_%02u%02u%02u",
                 (unsigned)(tm_now.tm_year + 1900) % 10000,
                 (unsigned)(tm_now.tm_mon + 1) % 100,
                 (unsigned)tm_now.tm_mday,
                 (unsigned)tm_now.tm_hour,
                 (unsigned)tm_now.tm_min,
                 (unsigned)tm_now.tm_sec);
    } else {
        /* 时间未同步 — 使用启动后的毫秒计数器 */
        snprintf(buf, size, "boot_%lld", esp_timer_get_time() / 1000);
    }
}

/* ── 格式化时间戳为可读字符串 ── */
static void ts_to_readable(const char *ts, char *buf, size_t size)
{
    if (strncmp(ts, "boot_", 5) == 0) {
        snprintf(buf, size, "boot+%sms", ts + 5);
        return;
    }
    /* YYYYMMDD_HHMMSS → YYYY-MM-DD HH:MM:SS */
    snprintf(buf, size, "%.4s-%.2s-%.2s %.2s:%.2s:%.2s",
             ts, ts + 4, ts + 6, ts + 9, ts + 11, ts + 13);
}

/* ── 获取文本文件第一行作为题目预览 ── */
static void read_first_line(FILE *f, char *buf, size_t size)
{
    if (fgets(buf, (int)size, f)) {
        /* 去除换行符 */
        size_t l = strlen(buf);
        while (l > 0 && (buf[l-1] == '\n' || buf[l-1] == '\r'))
            buf[--l] = '\0';
        /* 截断过长 */
        if (l > PHOTO_HISTORY_PREVIEW_LEN - 4) {
            buf[PHOTO_HISTORY_PREVIEW_LEN - 4] = '.';
            buf[PHOTO_HISTORY_PREVIEW_LEN - 3] = '.';
            buf[PHOTO_HISTORY_PREVIEW_LEN - 2] = '.';
            buf[PHOTO_HISTORY_PREVIEW_LEN - 1] = '\0';
        }
    } else {
        buf[0] = '\0';
    }
}

esp_err_t photo_history_init(void)
{
    if (!sd_card_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted, photo history disabled");
        return ESP_ERR_INVALID_STATE;
    }

    char dir_path[PHOTOS_PATH_MAX];
    snprintf(dir_path, sizeof(dir_path), "%s/" PHOTOS_DIR,
             sd_card_get_mount_point());

    struct stat st;
    if (stat(dir_path, &st) != 0) {
        if (mkdir(dir_path, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to mkdir %s (errno=%d)", dir_path, errno);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Photos directory created: %s", dir_path);
    }

    ESP_LOGI(TAG, "Photo history ready");
    return ESP_OK;
}

esp_err_t photo_history_save(const uint8_t *jpeg_data, size_t jpeg_len,
                              const char *question, const char *answer)
{
    if (!jpeg_data || jpeg_len == 0) return ESP_ERR_INVALID_ARG;
    if (!sd_card_is_mounted()) return ESP_ERR_INVALID_STATE;

    char ts[32];
    make_timestamp(ts, sizeof(ts));

    char jpg_path[PHOTOS_PATH_MAX];
    char txt_path[PHOTOS_PATH_MAX];
    snprintf(jpg_path, sizeof(jpg_path), "%s/" PHOTOS_DIR "/%s.jpg",
             sd_card_get_mount_point(), ts);
    snprintf(txt_path, sizeof(txt_path), "%s/" PHOTOS_DIR "/%s.txt",
             sd_card_get_mount_point(), ts);

    /* 写 JPEG */
    FILE *fj = fopen(jpg_path, "wb");
    if (!fj) {
        ESP_LOGE(TAG, "Failed to write JPEG: %s", jpg_path);
        return ESP_FAIL;
    }
    fwrite(jpeg_data, 1, jpeg_len, fj);
    fflush(fj);
    fclose(fj);

    /* 写文本 */
    FILE *ft = fopen(txt_path, "w");
    if (!ft) {
        ESP_LOGE(TAG, "Failed to write TXT: %s", txt_path);
        return ESP_FAIL;
    }
    if (question && question[0]) fprintf(ft, "%s\n", question);
    if (answer && answer[0])     fprintf(ft, "%s", answer);
    fflush(ft);
    fclose(ft);

    ESP_LOGI(TAG, "Saved: %s (JPEG=%uB)", ts, (unsigned)jpeg_len);
    return ESP_OK;
}

esp_err_t photo_history_list(photo_entry_t *entries, int max_count,
                              int *out_count)
{
    if (!entries || max_count <= 0 || !out_count)
        return ESP_ERR_INVALID_ARG;
    *out_count = 0;

    if (!sd_card_is_mounted())
        return ESP_ERR_INVALID_STATE;

    char dir_path[PHOTOS_PATH_MAX];
    snprintf(dir_path, sizeof(dir_path), "%s/" PHOTOS_DIR,
             sd_card_get_mount_point());

    DIR *dir = opendir(dir_path);
    if (!dir) return ESP_ERR_NOT_FOUND;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && *out_count < max_count) {
        /* 只处理 .txt 文件 (跳过 .jpg, 通过 .txt 定位) */
        const char *name = entry->d_name;
        size_t name_len = strlen(name);
        if (name_len < 5) continue;
        if (strcmp(name + name_len - 4, ".txt") != 0) continue;

        /* 提取基础文件名 */
        photo_entry_t *e = &entries[*out_count];
        size_t base_len = name_len - 4;
        if (base_len >= sizeof(e->filename)) base_len = sizeof(e->filename) - 1;
        memcpy(e->filename, name, base_len);
        e->filename[base_len] = '\0';

        /* 时间戳 */
        ts_to_readable(e->filename, e->timestamp, sizeof(e->timestamp));

        /* JPEG 大小 */
        char jpg_path[PHOTOS_PATH_MAX];
        snprintf(jpg_path, sizeof(jpg_path), "%s/" PHOTOS_DIR "/%s.jpg",
                 sd_card_get_mount_point(), e->filename);
        struct stat jst;
        e->jpeg_size = (stat(jpg_path, &jst) == 0) ? (size_t)jst.st_size : 0;

        /* 题目预览 (读 txt 第一行) */
        char txt_path[PHOTOS_PATH_MAX];
        snprintf(txt_path, sizeof(txt_path), "%s/" PHOTOS_DIR "/%s.txt",
                 sd_card_get_mount_point(), e->filename);
        FILE *ft = fopen(txt_path, "r");
        if (ft) {
            read_first_line(ft, e->question, sizeof(e->question));
            fclose(ft);
        } else {
            e->question[0] = '\0';
        }

        (*out_count)++;
    }
    closedir(dir);

    ESP_LOGI(TAG, "Listed %d photos", *out_count);
    return ESP_OK;
}

esp_err_t photo_history_get_entry(const char *filename,
                                   uint8_t **jpeg_data, size_t *jpeg_len,
                                   char *question_buf, size_t q_buf_size)
{
    if (!filename || !jpeg_data || !jpeg_len || !question_buf)
        return ESP_ERR_INVALID_ARG;
    if (!sd_card_is_mounted())
        return ESP_ERR_INVALID_STATE;

    /* 读 JPEG */
    char jpg_path[PHOTOS_PATH_MAX];
    snprintf(jpg_path, sizeof(jpg_path), "%s/" PHOTOS_DIR "/%s.jpg",
             sd_card_get_mount_point(), filename);

    struct stat jst;
    if (stat(jpg_path, &jst) != 0)
        return ESP_ERR_NOT_FOUND;

    *jpeg_len = (size_t)jst.st_size;
    *jpeg_data = malloc(*jpeg_len);
    if (!*jpeg_data) return ESP_ERR_NO_MEM;

    FILE *fj = fopen(jpg_path, "rb");
    if (!fj) {
        free(*jpeg_data);
        *jpeg_data = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    fread(*jpeg_data, 1, *jpeg_len, fj);
    fclose(fj);

    /* 读题目 (txt 第一行) */
    question_buf[0] = '\0';
    char txt_path[PHOTOS_PATH_MAX];
    snprintf(txt_path, sizeof(txt_path), "%s/" PHOTOS_DIR "/%s.txt",
             sd_card_get_mount_point(), filename);
    FILE *ft = fopen(txt_path, "r");
    if (ft) {
        read_first_line(ft, question_buf, q_buf_size);
        fclose(ft);
    }

    return ESP_OK;
}

esp_err_t photo_history_get_full_text(const char *filename,
                                       char *buf, size_t buf_size)
{
    if (!filename || !buf || buf_size == 0)
        return ESP_ERR_INVALID_ARG;
    if (!sd_card_is_mounted())
        return ESP_ERR_INVALID_STATE;

    char txt_path[PHOTOS_PATH_MAX];
    snprintf(txt_path, sizeof(txt_path), "%s/" PHOTOS_DIR "/%s.txt",
             sd_card_get_mount_point(), filename);

    FILE *f = fopen(txt_path, "r");
    if (!f) return ESP_ERR_NOT_FOUND;

    /* Read entire file */
    size_t total = 0;
    while (total < buf_size - 1) {
        size_t remain = buf_size - 1 - total;
        size_t n = fread(buf + total, 1, remain, f);
        if (n == 0) break;
        total += n;
    }
    buf[total] = '\0';

    /* Strip trailing whitespace */
    while (total > 0 && (buf[total - 1] == '\n' || buf[total - 1] == '\r'
           || buf[total - 1] == ' ')) {
        buf[--total] = '\0';
    }

    fclose(f);
    return ESP_OK;
}

/* ── 扫描 JPEG marker, 提取 SOF 尺寸/分量, 校验 SOI/EOI ──
 * 只读文件头 (最多前 64KB) + 文件尾 2 字节, 不整体解码。 */
esp_err_t photo_history_probe_jpeg(const char *jpg_path, jpeg_probe_t *out)
{
    if (!jpg_path || !out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(jpg_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "probe: cannot open %s", jpg_path);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz < 4) {
        ESP_LOGW(TAG, "probe: %s too small (%ld B)", jpg_path, fsz);
        fclose(f);
        return ESP_OK;  /* out.valid=false */
    }
    out->file_size = (size_t)fsz;

    /* 读取文件头 (最多 64KB, 足够覆盖到 SOF) */
    size_t hdr_cap = (fsz > 65536) ? 65536 : (size_t)fsz;
    uint8_t *buf = malloc(hdr_cap);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t rd = fread(buf, 1, hdr_cap, f);

    /* SOI = FF D8 */
    if (rd >= 2 && buf[0] == 0xFF && buf[1] == 0xD8) {
        out->has_soi = true;
    }

    /* 扫描 marker 段, 查找 SOF0(C0)/SOF1(C1)/SOF2(C2) 等帧头 */
    size_t i = 2;
    while (out->has_soi && i + 1 < rd) {
        if (buf[i] != 0xFF) { i++; continue; }          /* 对齐到 marker */
        uint8_t m = buf[i + 1];
        if (m == 0xFF) { i++; continue; }                /* 填充字节 */
        /* 无长度字段的 marker: SOI/EOI/RSTn/TEM */
        if (m == 0xD8 || m == 0xD9 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) {
            i += 2; continue;
        }
        if (i + 3 >= rd) break;
        uint16_t seg_len = ((uint16_t)buf[i + 2] << 8) | buf[i + 3];
        /* SOF markers: C0-C3, C5-C7, C9-CB, CD-CF (排除 C4=DHT, C8=JPG, CC=DAC) */
        bool is_sof = (m >= 0xC0 && m <= 0xCF) &&
                      (m != 0xC4 && m != 0xC8 && m != 0xCC);
        if (is_sof && i + 9 < rd) {
            out->has_sof    = true;
            out->height     = ((int)buf[i + 5] << 8) | buf[i + 6];
            out->width      = ((int)buf[i + 7] << 8) | buf[i + 8];
            out->components = buf[i + 9];
            break;  /* 拿到尺寸即可停止 */
        }
        if (m == 0xDA) break;  /* SOS: 图像数据开始, 之后无 SOF */
        if (seg_len < 2) break;
        i += 2 + seg_len;
    }
    free(buf);

    /* EOI = FF D9 (读文件尾 2 字节, 校验未截断) */
    if (fsz >= 2) {
        uint8_t tail[2] = {0};
        fseek(f, -2, SEEK_END);
        if (fread(tail, 1, 2, f) == 2 && tail[0] == 0xFF && tail[1] == 0xD9) {
            out->has_eoi = true;
        }
    }
    fclose(f);

    /* P4 硬件 JPEG 解码器要求: 宽高均 16 的倍数且 >=64 (与 esp_lv_decoder use_hw 判据一致) */
    out->hw_decodable = out->has_sof &&
                        (out->width  % 16 == 0) && (out->height % 16 == 0) &&
                        (out->width  >= 64)      && (out->height >= 64);

    out->valid = out->has_soi && out->has_sof && out->has_eoi &&
                 out->width > 0 && out->height > 0;

    ESP_LOGI(TAG, "JPEG probe: path=%s size=%u SOI=%d SOF=%d %dx%d comp=%d "
             "EOI=%d hw_decodable=%d valid=%d",
             jpg_path, (unsigned)out->file_size, out->has_soi, out->has_sof,
             out->width, out->height, out->components,
             out->has_eoi, out->hw_decodable, out->valid);

    return ESP_OK;
}

esp_err_t photo_history_delete(const char *filename)
{
    if (!filename) return ESP_ERR_INVALID_ARG;
    if (!sd_card_is_mounted()) return ESP_ERR_INVALID_STATE;

    char jpg_path[PHOTOS_PATH_MAX];
    char txt_path[PHOTOS_PATH_MAX];
    snprintf(jpg_path, sizeof(jpg_path), "%s/" PHOTOS_DIR "/%s.jpg",
             sd_card_get_mount_point(), filename);
    snprintf(txt_path, sizeof(txt_path), "%s/" PHOTOS_DIR "/%s.txt",
             sd_card_get_mount_point(), filename);

    bool ok = true;
    if (unlink(jpg_path) != 0) {
        ESP_LOGW(TAG, "Failed to unlink %s", jpg_path);
        ok = false;
    }
    if (unlink(txt_path) != 0) {
        ESP_LOGW(TAG, "Failed to unlink %s", txt_path);
        ok = false;
    }

    if (ok) ESP_LOGI(TAG, "Deleted: %s", filename);
    return ok ? ESP_OK : ESP_FAIL;
}
