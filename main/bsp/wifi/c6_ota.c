/**
 * @file c6_ota.c
 * @brief C6 协处理器固件 OTA 实现
 *
 * 参考: esp_hosted/examples/host_performs_slave_ota/components/ota_partition/ota_partition.c
 *
 * 流程:
 *   1. 查找 "slave_fw" 分区
 *   2. 解析 ESP 固件镜像头 → 获取新固件版本号和大小
 *   3. 获取 C6 当前运行版本
 *   4. 版本不同 → esp_hosted_slave_ota_begin/write/end/activate
 *   5. 版本相同 → 跳过
 */

#include "c6_ota.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "esp_hosted_ota.h"
#include "esp_hosted.h"
#include "esp_hosted_api_types.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "C6_OTA";

#define OTA_CHUNK_SIZE     1500
#define NVS_NS             "c6_ota"

/* ── 检查分区是否包含有效固件 ── */
static esp_err_t check_partition_not_empty(const esp_partition_t *partition)
{
    uint8_t buf[64];
    esp_err_t ret = esp_partition_read(partition, 0, buf, sizeof(buf));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Partition read failed: %s", esp_err_to_name(ret));
        return ret;
    }
    for (int i = 0; i < (int)sizeof(buf); i++) {
        if (buf[i] != 0xFF) return ESP_OK;   /* 非空 */
    }
    ESP_LOGW(TAG, "Partition '%s' is empty (all 0xFF)", partition->label);
    return ESP_ERR_NOT_FOUND;
}

/* ── 解析 ESP 固件镜像头, 获取大小和版本 ── */
static esp_err_t parse_image_info(const esp_partition_t *partition,
                                   size_t *out_size, char *out_version, size_t ver_len)
{
    esp_image_header_t img_hdr;
    esp_err_t ret = esp_partition_read(partition, 0, &img_hdr, sizeof(img_hdr));
    if (ret != ESP_OK) return ret;

    if (img_hdr.magic != ESP_IMAGE_HEADER_MAGIC) {
        ESP_LOGE(TAG, "Bad magic: 0x%02x (expected 0x%02x)", img_hdr.magic, ESP_IMAGE_HEADER_MAGIC);
        return ESP_ERR_INVALID_ARG;
    }

    /* 遍历 segment 计算总大小 */
    size_t offset = sizeof(img_hdr);
    size_t total   = sizeof(img_hdr);

    for (int i = 0; i < img_hdr.segment_count; i++) {
        esp_image_segment_header_t seg;
        ret = esp_partition_read(partition, offset, &seg, sizeof(seg));
        if (ret != ESP_OK) return ret;
        total   += sizeof(seg) + seg.data_len;
        offset  += sizeof(seg) + seg.data_len;

        /* 第 0 段包含 app_desc */
        if (i == 0) {
            esp_app_desc_t desc;
            size_t desc_off = sizeof(img_hdr) + sizeof(seg);
            if (esp_partition_read(partition, desc_off, &desc, sizeof(desc)) == ESP_OK) {
                snprintf(out_version, ver_len, "%s", desc.version);
            }
        }
    }

    /* 16 字节对齐 + 校验和 + SHA256(如果有) */
    total = (total + 15) & ~15;          /* align 16 */
    total += 1;                          /* checksum */
    if (img_hdr.hash_appended) {
        total = (total + 15) & ~15;      /* hash padding */
        total += 32;                     /* SHA256 */
    }

    *out_size = total;
    ESP_LOGI(TAG, "Image: %u bytes, version=%s, segments=%d",
             (unsigned)total, out_version, img_hdr.segment_count);
    return ESP_OK;
}

/* ── 公开 API ── */
esp_err_t c6_ota_check_and_update(void)
{
    esp_err_t ret;

    /* 1. 查找 slave_fw 分区 */
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "slave_fw");
    if (!part) {
        ESP_LOGW(TAG, "No 'slave_fw' partition found — OTA disabled");
        return ESP_ERR_NOT_FOUND;
    }

    /* 2. 检查是否为空 */
    if (check_partition_not_empty(part) != ESP_OK) {
        ESP_LOGI(TAG, "slave_fw partition is empty, skipping OTA");
        return ESP_ERR_NOT_FOUND;
    }

    /* 3. 解析新固件版本 */
    size_t fw_size = 0;
    char new_ver[32] = {0};
    if (parse_image_info(part, &fw_size, new_ver, sizeof(new_ver)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse slave_fw partition");
        return ESP_ERR_INVALID_ARG;
    }
    if (fw_size > part->size) {
        ESP_LOGE(TAG, "Firmware %uB > partition %luB", (unsigned)fw_size, (unsigned long)part->size);
        return ESP_ERR_INVALID_ARG;
    }

    /* 4. 检查 NVS: 上次推送的版本号 → 相同则跳过 */
    {
        char stored_ver[32] = {0};
        nvs_handle_t h;
        if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
            size_t len = sizeof(stored_ver);
            nvs_get_str(h, "version", stored_ver, &len);
            nvs_close(h);
        }
        if (strlen(stored_ver) > 0 && strcmp(stored_ver, new_ver) == 0) {
            ESP_LOGI(TAG, "C6 firmware version %s unchanged, skipping OTA", new_ver);
            return ESP_OK;
        }
        ESP_LOGI(TAG, "C6 firmware changed: %s -> %s, starting OTA...",
                 stored_ver[0] ? stored_ver : "(none)", new_ver);
    }

    ESP_LOGI(TAG, "C6 OTA: pushing version %s (%u bytes)",
             new_ver, (unsigned)fw_size);

    /* 5. 开始 OTA */
    ESP_LOGI(TAG, "*** Starting C6 OTA (%u bytes) ***", (unsigned)fw_size);
    ret = esp_hosted_slave_ota_begin();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 6. 分块传输 */
    size_t offset = 0;
    int chunk_nr  = 0;
    while (offset < fw_size) {
        uint8_t chunk[OTA_CHUNK_SIZE];
        size_t len = fw_size - offset;
        if (len > sizeof(chunk)) len = sizeof(chunk);

        ret = esp_partition_read(part, offset, chunk, len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Partition read @%u: %s", (unsigned)offset, esp_err_to_name(ret));
            esp_hosted_slave_ota_end();
            return ret;
        }

        ret = esp_hosted_slave_ota_write(chunk, len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA write chunk %d: %s", chunk_nr, esp_err_to_name(ret));
            esp_hosted_slave_ota_end();
            return ret;
        }
        offset += len;
        chunk_nr++;
        if (chunk_nr % 50 == 0) {
            ESP_LOGI(TAG, "  OTA progress: %u/%u bytes (%.0f%%)",
                     (unsigned)offset, (unsigned)fw_size,
                     (double)offset * 100 / fw_size);
        }
    }

    /* 7. 结束 OTA */
    ret = esp_hosted_slave_ota_end();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "OTA transfer complete (%u bytes, %d chunks)", (unsigned)offset, chunk_nr);

    /* 8. 激活新固件 */
    ret = esp_hosted_slave_ota_activate();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "New firmware activated — C6 will reboot");
    } else {
        ESP_LOGW(TAG, "Activate not supported or failed (%s) — C6 may need manual reboot",
                 esp_err_to_name(ret));
    }

    /* 9. 记录已推送的版本号(下次 boot 版本相同则跳过) + 重启 P4 */
    {
        nvs_handle_t h;
        if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_str(h, "version", new_ver);
            nvs_commit(h);
            nvs_close(h);
        }
    }
    ESP_LOGI(TAG, "C6 OTA version %s recorded, restarting P4...", new_ver);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();

    return ESP_OK;  /* unreachable */
}