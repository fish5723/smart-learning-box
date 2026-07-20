/**
 * @file font_loader.c
 * @brief TF 卡字库加载器实现 — 7 个字号 (12/14/16/18/20/24/26)，32+ 回退 24px
 *
 * 加载顺序:
 *   1. 尝试从 /sdcard/fonts/ 目录（LVGL FS 盘符 S:）加载 *.bin (LVGL BinFont)
 *   2. SD 卡不可用 → 使用固件内置最小字体 (仅 ASCII + 常用字)
 */

#include "font_loader.h"
#include "bsp/storage/sd_card.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "FONT_LOADER";

/* ═══════════════════════════════════════════════
   全局字体指针 — 10 个字号
   ═══════════════════════════════════════════════ */
lv_font_t *g_font_cjk_12 = NULL;
lv_font_t *g_font_cjk_14 = NULL;
lv_font_t *g_font_cjk_16 = NULL;
lv_font_t *g_font_cjk_18 = NULL;
lv_font_t *g_font_cjk_20 = NULL;
lv_font_t *g_font_cjk_24 = NULL;
lv_font_t *g_font_cjk_26 = NULL;
lv_font_t *g_font_cjk_32 = NULL;
lv_font_t *g_font_cjk_36 = NULL;
lv_font_t *g_font_cjk_48 = NULL;

/* ═══════════════════════════════════════════════
   状态
   ═══════════════════════════════════════════════ */
static bool s_using_sd = false;
static bool s_wdt_disabled = false;  /* 避免第二次加载时重复操作 WDT */

/* ═══════════════════════════════════════════════
   SD 卡字体路径映射
   ═══════════════════════════════════════════════ */
typedef struct {
    lv_font_t **font_ptr;
    const char *path;
} sd_font_entry_t;

static const sd_font_entry_t s_sd_fonts[] = {
    { &g_font_cjk_12, "/sdcard/fonts/fontcn12.bin" },
    { &g_font_cjk_14, "/sdcard/fonts/fontcn14.bin" },
    { &g_font_cjk_16, "/sdcard/fonts/fontcn16.bin" },
    { &g_font_cjk_18, "/sdcard/fonts/fontcn18.bin" },
    { &g_font_cjk_20, "/sdcard/fonts/fontcn20.bin" },
    { &g_font_cjk_24, "/sdcard/fonts/fontcn24.bin" },
    { &g_font_cjk_26, "/sdcard/fonts/fontcn26.bin" },
};

#define SD_FONT_COUNT (sizeof(s_sd_fonts) / sizeof(s_sd_fonts[0]))

/* ═══════════════════════════════════════════════
   固件内置 Fallback 字体声明 (最小字库 — 仅 4 个字号)
   ═══════════════════════════════════════════════ */
LV_FONT_DECLARE(fontcn14)
LV_FONT_DECLARE(fontcn16)
LV_FONT_DECLARE(fontcn20)
LV_FONT_DECLARE(fontcn24)

/* ═══════════════════════════════════════════════
   内部函数
   ═══════════════════════════════════════════════ */

static void load_fallback_fonts(void)
{
    ESP_LOGW(TAG, "Using fallback fonts (embedded in firmware)");

    /* 固件仅内置 14/16/20/24 */
    g_font_cjk_14 = (lv_font_t *)&fontcn14;
    g_font_cjk_16 = (lv_font_t *)&fontcn16;
    g_font_cjk_20 = (lv_font_t *)&fontcn20;
    g_font_cjk_24 = (lv_font_t *)&fontcn24;

    /* 其他字号用近似字号回退 */
    g_font_cjk_12 = (lv_font_t *)&fontcn14;  /* 12→14 */
    g_font_cjk_18 = (lv_font_t *)&fontcn20;  /* 18→20 */
    g_font_cjk_26 = (lv_font_t *)&fontcn24;  /* 26→24 */
    g_font_cjk_32 = (lv_font_t *)&fontcn24;  /* 32→24 */
    g_font_cjk_36 = (lv_font_t *)&fontcn24;  /* 36→24 */
    g_font_cjk_48 = (lv_font_t *)&fontcn24;  /* 48→24 */

    s_using_sd = false;
}

/* 把整个字体文件顺序读进 PSRAM，再从内存缓冲解析字体。
 * 相比 lv_binfont_create() 直接走 SD（解析时数千次小读，每次穿透 FS 栈），
 * 这里只做一次大块顺序读（SD 顺序读快），解析全在 RAM 上 memcpy → 快数倍。
 * lv_binfont_create_from_buffer 内部 lv_malloc 拷贝各段数据，返回后 buffer 即可释放。
 *
 * 优化点:
 *   - 读取块大小 512KB (SD 卡块对齐，减少 FS 开销)
 *   - 预分配 PSRAM 并预读取提示 (如果可用)
 *   - 解析后直接释放缓冲区，不占用额外内存
 */
#ifndef FONT_LOAD_CHUNK_SIZE
#define FONT_LOAD_CHUNK_SIZE  (512 * 1024)   /* 512KB 块读取 */
#endif

static lv_font_t *load_one_font_via_psram(const char *posix_path)
{
    FILE *fp = fopen(posix_path, "rb");
    if (!fp) { ESP_LOGW(TAG, "fopen failed: %s", posix_path); return NULL; }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize <= 0) { fclose(fp); return NULL; }

    uint8_t *buf = heap_caps_malloc((size_t)fsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "PSRAM alloc %ld B failed for %s", fsize, posix_path);
        fclose(fp);
        return NULL;
    }

#if defined(_POSIX_ADVISORY_INFO) || defined(__linux__)
    /* 预读取提示: 告诉内核这是顺序读取，可提前缓存 */
    posix_fadvise(fileno(fp), 0, fsize, POSIX_FADV_SEQUENTIAL);
#endif

    /* 大块顺序读，512KB 一次 → 减少 fread/FS 调用次数 */
    size_t total = 0;
    while (total < (size_t)fsize) {
        size_t chunk = (size_t)fsize - total;
        if (chunk > FONT_LOAD_CHUNK_SIZE) chunk = FONT_LOAD_CHUNK_SIZE;
        size_t rd = fread(buf + total, 1, chunk, fp);
        if (rd == 0) break;
        total += rd;
    }
    fclose(fp);

    if (total != (size_t)fsize) {
        ESP_LOGW(TAG, "short read %u/%ld: %s", (unsigned)total, fsize, posix_path);
        heap_caps_free(buf);
        return NULL;
    }

    lv_font_t *font = lv_binfont_create_from_buffer(buf, (uint32_t)fsize);
    heap_caps_free(buf);   /* 解析已把数据拷进 LVGL 结构，源 buffer 可释放 */
    return font;
}

static esp_err_t load_sd_fonts(void)
{
    int loaded = 0;
    int failed = 0;

    ESP_LOGI(TAG, "Loading %d fonts from SD card...", (int)SD_FONT_COUNT);

    /* 关闭任务看门狗：CJK 字体文件需解析 ~20000 字形，
     * 12~26px 共 7 个文件约 44 秒，远超 WDT 超时。
     * 字体加载是启动时的一次性操作，完成后立即恢复 WDT 保护。
     * s_wdt_disabled 标记避免第二次调用时重复操作（WDT reinit 后 main 已订阅，
     * 二次 deinit 导致未定义行为）。 */
#if CONFIG_SMARTBOX_TASK_WDT_ENABLED || CONFIG_ESP_TASK_WDT_EN
    if (!s_wdt_disabled) {
        esp_task_wdt_deinit();
        s_wdt_disabled = true;
    }
#endif

    /* 逐个加载 SD 卡字体 */
    int static const sizes[] = {12,14,16,18,20,24,26};
    for (int i = 0; i < SD_FONT_COUNT; i++) {
        int64_t t0 = esp_timer_get_time();
        lv_font_t *font = load_one_font_via_psram(s_sd_fonts[i].path);
        int ms = (int)((esp_timer_get_time() - t0) / 1000);
        if (font) {
            *s_sd_fonts[i].font_ptr = font;
            loaded++;
            ESP_LOGI(TAG, "  [%d/%d] %dpx -> OK (%d ms)", i+1, SD_FONT_COUNT, sizes[i], ms);
        } else {
            ESP_LOGW(TAG, "  [%d/%d] %dpx -> FAILED (%s)",
                     i+1, SD_FONT_COUNT, sizes[i], s_sd_fonts[i].path);
            failed++;
        }
        /* 让出 CPU，允许 LVGL 刷屏 */
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    /* 恢复 WDT 监督 */
#if CONFIG_SMARTBOX_TASK_WDT_ENABLED || CONFIG_ESP_TASK_WDT_EN
    {
        esp_task_wdt_config_t wdt_cfg = {
            .timeout_ms      = 5000,
            .idle_core_mask  = (1 << 0) | (1 << 1),
            .trigger_panic   = true,
        };
        esp_task_wdt_init(&wdt_cfg);
    }
#endif

    /* 至少加载一个字体才算成功 */
    if (loaded == 0) {
        ESP_LOGE(TAG, "Failed to load any fonts from SD");
        return ESP_FAIL;
    }

    /* 字号映射表 — 与 s_sd_fonts[] 的下标一一对应 */
    static const int s_font_sizes[] = {12, 14, 16, 18, 20, 24, 26, 32, 36, 48};

    /* 未从 SD 加载成功的字号使用 fallback 回退 */
    for (int i = 0; i < SD_FONT_COUNT; i++) {
        if (*s_sd_fonts[i].font_ptr == NULL) {
            /* 按近似大小选择 fallback 字体 */
            /* 优先选已从 SD 加载的最接近的字号，否则用固件内置 */
            lv_font_t **candidates[] = {
                &g_font_cjk_14, &g_font_cjk_16, &g_font_cjk_20, &g_font_cjk_24,
                &g_font_cjk_18, &g_font_cjk_26, &g_font_cjk_32, &g_font_cjk_36,
                &g_font_cjk_48, &g_font_cjk_12,
            };
            lv_font_t *fallback = NULL;
            for (int j = 0; j < sizeof(candidates) / sizeof(candidates[0]); j++) {
                if (*candidates[j] != NULL && *candidates[j] != (lv_font_t *)&fontcn14
                    && *candidates[j] != (lv_font_t *)&fontcn16
                    && *candidates[j] != (lv_font_t *)&fontcn20
                    && *candidates[j] != (lv_font_t *)&fontcn24) {
                    fallback = *candidates[j];
                    break;
                }
            }
            /* 没有已加载的 SD 字体，使用内置 fallback */
            if (!fallback) {
                fallback = (lv_font_t *)&fontcn16;
            }
            *s_sd_fonts[i].font_ptr = fallback;
            ESP_LOGI(TAG, "Font %dpx → fallback (%dpx)",
                     s_font_sizes[i], 16);
        }
    }

    s_using_sd = true;
    ESP_LOGI(TAG, "SD fonts: %d loaded, %d fallback (%d total)",
             loaded, failed, (int)SD_FONT_COUNT);

    return ESP_OK;
}

/* ═══════════════════════════════════════════════
   公开接口
   ═══════════════════════════════════════════════ */

esp_err_t font_loader_init(void)
{
    /* 已从 SD 加载成功则跳过（home_ui_update_fonts 可能在 SD 就绪后再次触发） */
    if (s_using_sd) {
        ESP_LOGI(TAG, "Already loaded from SD, skipping re-init");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "font_loader_init() starting — 7 sizes (12/14/16/18/20/24/26)");

    /* 重置指针 */
    g_font_cjk_12 = NULL;
    g_font_cjk_14 = NULL;
    g_font_cjk_16 = NULL;
    g_font_cjk_18 = NULL;
    g_font_cjk_20 = NULL;
    g_font_cjk_24 = NULL;
    g_font_cjk_26 = NULL;
    g_font_cjk_32 = NULL;
    g_font_cjk_36 = NULL;
    g_font_cjk_48 = NULL;

    /* 优先从 SD 卡加载 */
    if (sd_card_is_mounted()) {
        if (load_sd_fonts() == ESP_OK) {
            ESP_LOGI(TAG, "font_loader_init() OK (SD card, %d fonts)", (int)SD_FONT_COUNT);
            return ESP_OK;
        }
    } else {
        ESP_LOGW(TAG, "SD card not mounted");
    }

    /* 回退到固件内置字体 */
    load_fallback_fonts();

    ESP_LOGI(TAG, "font_loader_init() OK (fallback — 4 embedded fonts)");
    return ESP_OK;
}

void font_loader_deinit(void)
{
    ESP_LOGI(TAG, "font_loader_deinit()");

    /* 释放所有从 SD 卡加载的字体 (非固件内置) */
    const lv_font_t *embedded[] = {
        (lv_font_t *)&fontcn14, (lv_font_t *)&fontcn16,
        (lv_font_t *)&fontcn20, (lv_font_t *)&fontcn24
    };

    lv_font_t **all_fonts[] = {
        &g_font_cjk_12, &g_font_cjk_14, &g_font_cjk_16, &g_font_cjk_18,
        &g_font_cjk_20, &g_font_cjk_24, &g_font_cjk_26, &g_font_cjk_32,
        &g_font_cjk_36, &g_font_cjk_48
    };

    for (int i = 0; i < sizeof(all_fonts) / sizeof(all_fonts[0]); i++) {
        if (*all_fonts[i] == NULL) continue;

        bool is_embedded = false;
        for (int j = 0; j < 4; j++) {
            if (*all_fonts[i] == embedded[j]) {
                is_embedded = true;
                break;
            }
        }
        if (!is_embedded) {
            lv_binfont_destroy(*all_fonts[i]);
        }
        *all_fonts[i] = NULL;
    }

    s_using_sd = false;
}

bool font_loader_using_sd(void)
{
    return s_using_sd;
}