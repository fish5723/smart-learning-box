/**
 * @file rom_loader.c
 * @brief NES ROM 文件加载器 — 扫描 TF 卡 /ROM/ 目录
 *
 * ROM 目录结构:
 *   /sdcard/ROM/
 *     0001/ Donkey Kong (J) [!].nes
 *     0002/ Super Mario Bros. (JU) [!].nes
 *     ...
 *
 * 每个编号子目录包含 .nes 和 .fds 文件, 我们只收集 .nes 文件。
 */

#include "rom_loader.h"
#include "bsp/storage/sd_card.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "ROM_LOADER";

/* ROM 根目录 */
#define ROM_ROOT_PATH   "/sdcard/ROM"

esp_err_t rom_loader_scan(nes_rom_entry_t **out_list, int *out_count)
{
    if (!out_list || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_list = NULL;
    *out_count = 0;

    if (!sd_card_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted");
        return ESP_ERR_NOT_FOUND;
    }

    /* 打开 ROM 根目录 */
    DIR *root = opendir(ROM_ROOT_PATH);
    if (!root) {
        ESP_LOGW(TAG, "ROM directory not found: %s", ROM_ROOT_PATH);
        return ESP_ERR_NOT_FOUND;
    }

    /* 第一遍: 统计 .nes 文件数量 */
    int capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(root)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) continue;

        /* 打开子目录 */
        char sub_path[512];
        snprintf(sub_path, sizeof(sub_path), "%s/%s", ROM_ROOT_PATH, entry->d_name);

        DIR *sub = opendir(sub_path);
        if (!sub) continue;

        struct dirent *rom;
        while ((rom = readdir(sub)) != NULL) {
            if (rom->d_name[0] == '.') continue;

            /* 检查是否为 .nes 文件 */
            const char *ext = strrchr(rom->d_name, '.');
            if (ext && strcasecmp(ext, ".nes") == 0) {
                capacity++;
            }
        }
        closedir(sub);
    }
    rewinddir(root);

    if (capacity == 0) {
        ESP_LOGW(TAG, "No .nes files found in %s", ROM_ROOT_PATH);
        closedir(root);
        return ESP_ERR_NOT_FOUND;
    }

    /* 分配列表 */
    nes_rom_entry_t *list = calloc(capacity, sizeof(nes_rom_entry_t));
    if (!list) {
        ESP_LOGE(TAG, "Failed to allocate ROM list (%d entries)", capacity);
        closedir(root);
        return ESP_ERR_NO_MEM;
    }

    /* 第二遍: 填充列表 */
    int count = 0;
    while ((entry = readdir(root)) != NULL && count < capacity) {
        if (entry->d_name[0] == '.') continue;

        char sub_path[512];
        snprintf(sub_path, sizeof(sub_path), "%s/%s", ROM_ROOT_PATH, entry->d_name);

        DIR *sub = opendir(sub_path);
        if (!sub) continue;

        struct dirent *rom;
        while ((rom = readdir(sub)) != NULL && count < capacity) {
            if (rom->d_name[0] == '.') continue;

            const char *ext = strrchr(rom->d_name, '.');
            if (!ext || strcasecmp(ext, ".nes") != 0) continue;

            /* 填充条目 */
            strncpy(list[count].name, rom->d_name, NES_ROM_NAME_MAX - 1);
            list[count].name[NES_ROM_NAME_MAX - 1] = '\0';

            int written = snprintf(list[count].path, sizeof(list[count].path),
                     "%s/%s", sub_path, rom->d_name);
            if (written < 0 || (size_t)written >= sizeof(list[count].path)) {
                list[count].path[sizeof(list[count].path) - 1] = '\0';
            }

            /* 获取文件大小 */
            struct stat st;
            if (stat(list[count].path, &st) == 0) {
                list[count].size = st.st_size;
            }

            list[count].is_nes = true;
            count++;
        }
        closedir(sub);
    }
    closedir(root);

    *out_list = list;
    *out_count = count;

    ESP_LOGI(TAG, "Found %d NES ROMs in %s", count, ROM_ROOT_PATH);
    return ESP_OK;
}

void rom_loader_free_list(nes_rom_entry_t *list)
{
    if (list) {
        free(list);
    }
}

esp_err_t rom_loader_load_file(const char *path, uint8_t **out_data, size_t *out_size)
{
    if (!path || !out_data || !out_size) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_data = NULL;
    *out_size = 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    /* 获取文件大小 */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > (4 * 1024 * 1024)) {  /* 最大 4MB */
        ESP_LOGE(TAG, "Invalid ROM size: %ld", fsize);
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    /* 分配内存 */
    uint8_t *data = malloc(fsize);
    if (!data) {
        ESP_LOGE(TAG, "Failed to allocate %ld bytes for ROM", fsize);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    /* 读取 */
    size_t read = fread(data, 1, fsize, f);
    fclose(f);

    if (read != (size_t)fsize) {
        ESP_LOGE(TAG, "Short read: %zu/%ld", read, fsize);
        free(data);
        return ESP_FAIL;
    }

    *out_data = data;
    *out_size = fsize;

    ESP_LOGI(TAG, "Loaded ROM: %s (%zu bytes)", path, fsize);
    return ESP_OK;
}

void rom_loader_free_data(uint8_t *data)
{
    if (data) {
        free(data);
    }
}
