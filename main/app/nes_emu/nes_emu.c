/**
 * @file nes_emu.c
 * @brief NES 模拟器模块 — 基于 Nofrendo 核心
 *
 * 架构:
 *   Core 0 — NES 模拟器任务 (nes_core_run_frame, 60fps)
 *   Core 1 — LVGL UI (refresh_frame_cb → nes_render_update)
 *
 * 数据流:
 *   nes_emu_task (Core 0):
 *     nes_core_run_frame() → xSemaphoreGive(s_frame_ready)
 *   LVGL timer (Core 1):
 *     xSemaphoreTake(s_frame_ready) → nes_core_get_framebuffer() → nes_render_update()
 */

#include "nes_emu.h"
#include "nes_emu_ui.h"
#include "rom_loader.h"
#include "platform/nes_core.h"
#include "virtual_gamepad.h"
#include "save_manager.h"
#include "app/game_center/game_db/game_db.h"
#include "bsp/storage/sd_card.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "NES_EMU";

/* ═══════════════════════════════════════════════
   模拟器状态
   ═══════════════════════════════════════════════ */
typedef enum {
    NES_STATE_IDLE,
    NES_STATE_RUNNING,
    NES_STATE_PAUSED,
    NES_STATE_STOPPING,
} nes_state_t;

static nes_state_t      s_state = NES_STATE_IDLE;
static char             s_current_rom_path[256] = {0};
static char             s_current_rom_name[NES_ROM_NAME_MAX] = {0};
static uint32_t         s_frame_count = 0;
static uint32_t         s_fps = 0;
static int64_t          s_last_fps_time = 0;

/* ═══════════════════════════════════════════════
   模拟器任务 & 同步
   ═══════════════════════════════════════════════ */
static TaskHandle_t     s_emu_task = NULL;
static SemaphoreHandle_t s_frame_ready = NULL;  /* 模拟器 → LVGL 帧就绪信号 */
static SemaphoreHandle_t s_stop_req = NULL;     /* LVGL → 模拟器 停止请求 */

/* ═══════════════════════════════════════════════
   模拟器任务 (Core 0, 60fps)
   ═══════════════════════════════════════════════ */

static void nes_emu_task(void *arg)
{
    ESP_LOGI(TAG, "NES emulator task started (Core %d)", xPortGetCoreID());

    while (1) {
        /* 等待停止请求 */
        if (s_stop_req && xSemaphoreTake(s_stop_req, 0) == pdTRUE) {
            ESP_LOGI(TAG, "Stop request received, exiting task");
            s_state = NES_STATE_IDLE;
            s_emu_task = NULL;
            vTaskDelete(NULL);
            return;
        }

        /* 仅在运行状态下执行模拟器 */
        if (s_state == NES_STATE_RUNNING) {
            nes_core_run_frame();
            /* 注意：s_frame_count 由 LVGL 任务统计，避免跨核竞争 */

            /* 通知LVGL帧已就绪 */
            if (s_frame_ready) {
                xSemaphoreGive(s_frame_ready);
            }
            
            /* 让出CPU时间片，避免饿死低优先级任务 */
            taskYIELD();
        } else {
            /* 暂停或空闲时短暂休眠，降低CPU占用 */
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

/* ═══════════════════════════════════════════════
   FPS 统计
   ═══════════════════════════════════════════════ */

uint32_t nes_emu_get_fps(void)
{
    int64_t now = esp_timer_get_time();
    int64_t elapsed = now - s_last_fps_time;

    if (elapsed >= 1000000) {
        s_fps = s_frame_count;
        s_frame_count = 0;
        s_last_fps_time = now;
    }
    return s_fps;
}

/* ═══════════════════════════════════════════════
   公开 API 实现
   ═══════════════════════════════════════════════ */

esp_err_t nes_emu_init(void)
{
    ESP_LOGI(TAG, "nes_emu_init()");

    if (!sd_card_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted — NES emulator disabled");
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Phase 1: 加载游戏数据库 (JSON → PSRAM 索引, 1251 款) */
    esp_err_t db_err = game_db_init();
    if (db_err != ESP_OK) {
        ESP_LOGW(TAG, "game_db_init() failed: %s — NES disabled", esp_err_to_name(db_err));
        return db_err;
    }

    /* Phase 4: 初始化 Nofrendo 核心 (CPU/PPU/APU, 视频缓冲) */
    esp_err_t core_err = nes_core_init();
    if (core_err != ESP_OK) {
        ESP_LOGW(TAG, "nes_core_init() failed: %s", esp_err_to_name(core_err));
        return core_err;
    }

    /* 初始化 UI (LVGL 页面 + 虚拟手柄) */
    nes_emu_ui_init();

    ESP_LOGI(TAG, "nes_emu_init() OK");
    return ESP_OK;
}

void nes_emu_deinit(void)
{
    ESP_LOGI(TAG, "nes_emu_deinit()");
    nes_emu_stop();
    nes_core_destroy();
}

void nes_emu_show(void)
{
    /* 旧 ROM 浏览器已废弃 — 由 nes_category_ui_show 替代 */
    nes_emu_ui_show();
}

void nes_emu_hide(void)
{
    nes_emu_ui_hide();
}

esp_err_t nes_emu_load_rom(const char *rom_path)
{
    if (s_state != NES_STATE_IDLE) {
        ESP_LOGW(TAG, "ROM already running, stop first");
        nes_emu_stop();
    }

    if (!rom_path || strlen(rom_path) == 0) {
        ESP_LOGE(TAG, "Invalid ROM path");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Loading ROM: %s", rom_path);

    /* ── 加载 ROM 到 NES 核心 ── */
    esp_err_t ret = nes_core_load_rom(rom_path);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nes_core_load_rom failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 保存 ROM 信息 */
    strncpy(s_current_rom_path, rom_path, sizeof(s_current_rom_path) - 1);
    const char *basename = strrchr(rom_path, '/');
    if (basename) {
        strncpy(s_current_rom_name, basename + 1, sizeof(s_current_rom_name) - 1);
    } else {
        strncpy(s_current_rom_name, rom_path, sizeof(s_current_rom_name) - 1);
    }
    char *dot = strrchr(s_current_rom_name, '.');
    if (dot) *dot = '\0';

    /* 重置帧计数 */
    s_frame_count = 0;
    s_fps = 0;
    s_last_fps_time = esp_timer_get_time();

    /* 创建同步信号量 */
    if (!s_frame_ready) {
        s_frame_ready = xSemaphoreCreateCounting(1, 0);
    }
    if (!s_stop_req) {
        s_stop_req = xSemaphoreCreateBinary();
    }

    /* 设置运行状态 */
    s_state = NES_STATE_RUNNING;

    /* 创建模拟器任务 (Core 0, 优先级3, 8KB栈) */
    if (!s_emu_task) {
        BaseType_t result = xTaskCreatePinnedToCore(
            nes_emu_task,
            "nes_emu",
            8192,  /* 8KB栈：Nofrendo核心需要较深栈空间 */
            NULL,
            3,     /* 优先级3：低于LVGL(5)和I2C任务(4) */
            &s_emu_task,
            0  /* Core 0 */
        );
        if (result != pdPASS) {
            ESP_LOGE(TAG, "Failed to create emulator task");
            s_state = NES_STATE_IDLE;
            return ESP_FAIL;
        }
    }

    /* 显示游戏画面 */
    nes_emu_ui_show_game(s_current_rom_name);

    ESP_LOGI(TAG, "ROM loaded: %s, task created", s_current_rom_name);
    return ESP_OK;
}

void nes_emu_stop(void)
{
    if (s_state == NES_STATE_IDLE) return;

    ESP_LOGI(TAG, "Stopping emulator...");
    s_state = NES_STATE_STOPPING;

    /* 发送停止请求到模拟器任务 */
    if (s_stop_req) {
        xSemaphoreGive(s_stop_req);
    }

    /* 等待任务退出 (最多500ms) */
    int timeout = 50;
    while (s_emu_task != NULL && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (s_emu_task) {
        ESP_LOGW(TAG, "Emulator task did not exit, force deleting");
        vTaskDelete(s_emu_task);
        s_emu_task = NULL;
    }

    /* 清理信号量 */
    if (s_frame_ready) {
        vSemaphoreDelete(s_frame_ready);
        s_frame_ready = NULL;
    }
    if (s_stop_req) {
        vSemaphoreDelete(s_stop_req);
        s_stop_req = NULL;
    }

    /* 清理NES核心状态（释放ROM数据、重建核心） */
    nes_core_destroy();

    /* 重新初始化核心以便加载下一个ROM */
    nes_core_init();

    s_current_rom_path[0] = '\0';
    s_current_rom_name[0] = '\0';
    s_state = NES_STATE_IDLE;
    ESP_LOGI(TAG, "Emulator stopped");
}

void nes_emu_toggle_pause(void)
{
    if (s_state == NES_STATE_RUNNING) {
        s_state = NES_STATE_PAUSED;
        ESP_LOGI(TAG, "Paused");
    } else if (s_state == NES_STATE_PAUSED) {
        s_state = NES_STATE_RUNNING;
        ESP_LOGI(TAG, "Resumed");
    }
}

uint16_t *nes_emu_get_framebuffer(void)
{
    return nes_core_get_framebuffer();
}

void nes_emu_input(uint8_t key_mask, bool pressed)
{
    if (s_state != NES_STATE_RUNNING) return;
    
    /* 将虚拟手柄按键传递到NES核心 */
    nes_core_set_input(key_mask, pressed);
}

esp_err_t nes_emu_save_state(void)
{
    if (s_current_rom_name[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    return save_manager_save(s_current_rom_name);
}

esp_err_t nes_emu_load_state(void)
{
    if (s_current_rom_name[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    return save_manager_load(s_current_rom_name);
}

bool nes_emu_is_running(void)
{
    return (s_state == NES_STATE_RUNNING || s_state == NES_STATE_PAUSED);
}

esp_err_t nes_emu_scan_roms(nes_rom_entry_t **out_list, int *out_count)
{
    return rom_loader_scan(out_list, out_count);
}

void nes_emu_free_rom_list(nes_rom_entry_t *list)
{
    rom_loader_free_list(list);
}

const char *nes_emu_get_current_rom_name(void)
{
    return s_current_rom_name;
}

uint32_t nes_emu_get_frame_count(void)
{
    return s_frame_count;
}

SemaphoreHandle_t nes_emu_get_frame_ready_sem(void)
{
    return s_frame_ready;
}