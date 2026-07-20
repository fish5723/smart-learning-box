/**
 * @file sd_card.c
 * @brief SD 卡 BSP 驱动 — SDMMC 4-bit + FatFS 挂载实现
 */

#include "sd_card.h"
#include "sdkconfig.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"
#include "ff.h"
#include "esp_log.h"
#include "soc/soc_caps.h"
#if SOC_SDMMC_IO_POWER_EXTERNAL || SOC_SDMMC_IO_UHS_POWER_EXTERNAL
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

static const char *TAG = "SD_CARD";

#define SD_MOUNT_POINT          "/sdcard"
#define SD_MAX_FILES            16
#define SD_ALLOCATION_UNIT_SIZE (16 * 1024)
#define SD_FREQ_KHZ             40000
#define SD_SLOT                 SDMMC_HOST_SLOT_0

/* SDMMC Slot 0 固定引脚 (ESP32-P4 IOMUX) */
#define SD_PIN_CLK              43
#define SD_PIN_CMD              44
#define SD_PIN_D0               39
#define SD_PIN_D1               40
#define SD_PIN_D2               41
#define SD_PIN_D3               42

static bool s_mounted = false;
static sdmmc_card_t *s_card = NULL;
#if SOC_SDMMC_IO_POWER_EXTERNAL || SOC_SDMMC_IO_UHS_POWER_EXTERNAL
static sd_pwr_ctrl_handle_t s_pwr_ctrl = NULL;
#endif

esp_err_t sd_card_init(void)
{
    if (s_mounted) {
        ESP_LOGW(TAG, "Already mounted, skipping");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SD card (SDMMC 4-bit, Slot %d)...", SD_SLOT);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files              = SD_MAX_FILES,
        .allocation_unit_size   = SD_ALLOCATION_UNIT_SIZE,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SD_SLOT;
    host.max_freq_khz = SD_FREQ_KHZ;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    /* P4 SDMMC Slot 0 引脚由 IOMUX 固定，无需软件配置 */
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t ret;

#if SOC_SDMMC_IO_POWER_EXTERNAL || SOC_SDMMC_IO_UHS_POWER_EXTERNAL
    /* ESP32-P4 SDMMC IO 需要内部 LDO 供电，否则 IO 电压为 0 导致 0x107 超时 */
    /*
     * ESP32-P4 SDMMC IO 需要内部 LDO 供电，否则 IO 电压为 0 导致 0x107 超时。
     * LDO_VO4 是 P4 SDMMC IO 默认供电通道 (见 ESP-IDF examples/storage/sd_card/sdmmc)。
     */
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;
    ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SDMMC LDO power control");
        return ret;
    }
    host.pwr_ctrl_handle = pwr_ctrl_handle;
    s_pwr_ctrl = pwr_ctrl_handle;
#endif

    ESP_LOGI(TAG, "Mounting to %s (CLK=%d, CMD=%d, D0=%d, D1=%d, D2=%d, D3=%d)...",
             SD_MOUNT_POINT, SD_PIN_CLK, SD_PIN_CMD,
             SD_PIN_D0, SD_PIN_D1, SD_PIN_D2, SD_PIN_D3);

    ret = esp_vfs_fat_sdmmc_mount(
        SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mount failed: %s (0x%x)", esp_err_to_name(ret), ret);
        return ret;
    }

    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "SD card mounted: %s", s_card->cid.name);

    /* 挂载验证 — 只读检查挂载点可访问，不写 SD 卡 (避免磨损) */
    DIR *d = opendir(SD_MOUNT_POINT);
    if (d) {
        closedir(d);
        ESP_LOGI(TAG, "Mount point verified (read-only)");
    }

    s_mounted = true;
    return ESP_OK;
}

esp_err_t sd_card_deinit(void)
{
    if (!s_mounted) return ESP_OK;
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    s_card = NULL;
    s_mounted = false;

#if SOC_SDMMC_IO_POWER_EXTERNAL || SOC_SDMMC_IO_UHS_POWER_EXTERNAL
    if (s_pwr_ctrl) {
        sd_pwr_ctrl_del_on_chip_ldo(s_pwr_ctrl);
        s_pwr_ctrl = NULL;
    }
#endif

    return ret;
}

bool sd_card_is_mounted(void) { return s_mounted; }
const char *sd_card_get_mount_point(void) { return SD_MOUNT_POINT; }

esp_err_t sd_card_get_capacity(int *total_mb, int *free_mb)
{
    if (!s_mounted || !s_card) return ESP_ERR_INVALID_STATE;
    if (total_mb)
        *total_mb = (int)((uint64_t)s_card->csd.capacity * 512 / (1024 * 1024));
    if (free_mb) {
        FATFS *fs = NULL;
        DWORD free_clusters = 0;
        FRESULT res = f_getfree(SD_MOUNT_POINT, &free_clusters, &fs);
        if (res == FR_OK && fs)
            *free_mb = (int)((uint64_t)free_clusters * fs->csize * fs->ssize / (1024 * 1024));
        else
            *free_mb = 0;
    }
    return ESP_OK;
}

void sd_card_run_test(void)
{
    ESP_LOGI(TAG, "═══ SD Card Test ═══");
    esp_err_t ret = sd_card_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TEST FAILED");
        return;
    }
    ESP_LOGI(TAG, "═══ ALL TESTS PASSED ═══");
}
