/**
 * @brief 系统初始化 — esp_lvgl_adapter API 驱动的初始化流程
 *
 * 适配器接管:
 *  - LVGL 初始化 (lv_init)
 *  - Display 注册 (创建 lv_display + 绘制缓冲 + flush/ready 回调)
 *  - Touch 注册   (创建 lv_indev + 读取回调)
 *  - LVGL tick 定时器
 *  - LVGL task (lv_timer_handler 循环)
 *  - 线程安全锁 (esp_lv_adapter_lock / unlock)
 *
 * 参考: esp_lvgl_adapter test_apps/lvgl9/main/test_esp_lvgl_adapter_features.c
 */

#include "system_init.h"

#include "bsp/lcd/lcd.h"
#include "bsp/touch/touch.h"
#include "bsp/camera/camera.h"
#include "bsp/wifi/wifi.h"
#include "bsp/time/sys_time.h"
#include "bsp/battery/battery.h"
#include "bsp/storage/storage.h"
#include "bsp/storage/sd_card.h"
#include "bsp/storage/crash_log.h"
#include "bsp/web_server/web_server.h"
#include "manager/camera_manager.h"
#include "vision/vision_core.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "app/boot/boot_ui.h"
#include "manager/ui_manager.h"
#include "app/ai/ai.h"
#include "app/ocr/ocr.h"
#include "app/ocr/ocr_album_ui.h"
#include "app/cache/answer_cache.h"
#include "app/photos/photo_history.h"
#include "app/photos/photo_history_ui.h"
#include "app/wrong_book/wrong_book.h"
#include "app/wrong_book/wrong_book_ui.h"
#include "app/game_center/game_center.h"
#include "app/achievement/achievement.h"

#include "app/font_loader/font_loader.h"
#include "app/icon_loader/icon_loader.h"
#include "app/nes_emu/nes_emu.h"

#include "bsp/lvgl/lvgl_port.h"
#include "esp_lv_adapter.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "lvgl.h"

static const char *TAG = "SYSTEM_INIT";

/* ── 学习时长后台计时 ──
 *   study_timer_cb (ESP_TIMER_TASK) 写入，main_task 读出并清零。
 *   用 spinlock 防止竞态条件（定时器任务可能在 main_task 读-清零之间抢占）。 */
static volatile int s_study_minutes_pending = 0;
static volatile int s_study_minutes_total   = 0;   /* 累计总分钟数（含已计入成就的） */
static portMUX_TYPE s_study_spinlock = portMUX_INITIALIZER_UNLOCKED;

/* ── HTTPS 最小化测试任务（不访问 SD 卡）── */
static void https_test_task(void *arg)
{
    (void)arg;

    /* 等待 WiFi 连接 */
    ESP_LOGI(TAG, "[HTTPS_TEST] Waiting for WiFi connection...");
    int retry = 0;
    while (!wifi_is_connected() && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    if (!wifi_is_connected()) {
        ESP_LOGE(TAG, "[HTTPS_TEST] WiFi not connected, aborting test");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "[HTTPS_TEST] WiFi connected, waiting 10s for network stable...");
    vTaskDelay(pdMS_TO_TICKS(10000));

    /* 测试 1: HTTPS GET 百度（最小化） */
    ESP_LOGI(TAG, "[HTTPS_TEST] Test 1: https://www.baidu.com");
    esp_http_client_config_t config1 = {
        .url = "https://www.baidu.com",
        .method = HTTP_METHOD_GET,
        .timeout_ms = 30000,
        .buffer_size = 1024,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config1);
    if (client) {
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "[HTTPS_TEST] Baidu: HTTP %d", status);
        } else {
            ESP_LOGE(TAG, "[HTTPS_TEST] Baidu failed: %s", esp_err_to_name(err));
        }
        esp_http_client_cleanup(client);
    }

    /* 测试 2: HTTPS GET 豆包 API（最小化） */
    ESP_LOGI(TAG, "[HTTPS_TEST] Test 2: https://ark.cn-beijing.volces.com");
    esp_http_client_config_t config2 = {
        .url = "https://ark.cn-beijing.volces.com",
        .method = HTTP_METHOD_GET,
        .timeout_ms = 30000,
        .buffer_size = 1024,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    client = esp_http_client_init(&config2);
    if (client) {
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "[HTTPS_TEST] Doubao: HTTP %d", status);
        } else {
            ESP_LOGE(TAG, "[HTTPS_TEST] Doubao failed: %s", esp_err_to_name(err));
        }
        esp_http_client_cleanup(client);
    }

    ESP_LOGI(TAG, "[HTTPS_TEST] All tests completed");
    vTaskDelete(NULL);
}

/* ── 后台定时器句柄（全局化防泄漏） ── */
static esp_timer_handle_t s_study_timer  = NULL;
static esp_timer_handle_t s_battery_timer = NULL;

/* ── 学习时长查询接口（供 UI 层调用） ── */
int system_get_study_minutes(void)
{
    return s_study_minutes_total + s_study_minutes_pending;
}

static void study_timer_cb(void *arg)
{
    (void)arg;
    portENTER_CRITICAL(&s_study_spinlock);
    s_study_minutes_pending++;
    portEXIT_CRITICAL(&s_study_spinlock);
}

/* ── 电池定时器回调 ── */
static void battery_timer_cb(void *arg)
{
    (void)arg;
    battery_read(NULL, NULL);  /* 更新缓存值 */
}

/* ── WiFi 事件回调 — 获取 IP 后启动家长端网页服务 ── */
static void on_wifi_event(wifi_cb_event_t event, void *arg)
{
    (void)arg;
    if (event == WIFI_CB_GOT_IP) {
        ESP_LOGI(TAG, "WiFi connected, starting parent web server...");
        web_server_start();
    }
}

/* ── Boot Splash 退出回调 (由 LVGL timer 在 LVGL task 上下文调用) ── */
static void boot_exit_to_home(void)
{
    ui_manager_show_page(PAGE_HOME);
}

/* ── 主逻辑任务 ── */
static void main_task(void *arg)
{
    ESP_LOGI(TAG, "Main task started");

    /* ── Boot Splash: 等 LVGL task 稳定后异步创建 ── */
    vTaskDelay(pdMS_TO_TICKS(200));
    boot_ui_start();
    boot_ui_set_progress(0, "SYSTEM STARTING...");

    /* 非 UI 页面延迟加载 (初始化仅为空操作，无需加锁) */
    ai_init();
    ocr_init();
    game_center_init();
    achievement_init();
    wrong_book_ui_init();
    photo_history_ui_init();
    ocr_album_ui_init();

    boot_ui_set_progress(20, "LVGL READY");

    /* 时间模块初始化 (时区 + NTP 服务器配置) */
    time_init();

    /* 崩溃日志初始化 — 通过串口输出复位原因/状态快照，不写 SD 卡。
     * 不依赖任何外设，可在 SD 卡之前调用。 */
#if CONFIG_SMARTBOX_CRASH_LOG_ENABLED
    crash_log_init();
#endif

    boot_ui_set_progress(40, "LOADING FONTS...");

    /* SD 卡初始化 — 必须在 WiFi 之前，因为 SDMMC 外设共享时钟分频器。
     * 若 WiFi SDIO (Slot 1) 先运行再初始化 SD 卡 (Slot 0)，
     * sdmmc_card_init 的 400kHz 探测频率会中断 Slot 1 通信 → C6 复位 → netif_add crash。 */
#if CONFIG_SMARTBOX_SDCARD_ENABLED
    {
        esp_err_t sd_ret = sd_card_init();
        if (sd_ret == ESP_OK) {
            int total_mb = 0, free_mb = 0;
            sd_card_get_capacity(&total_mb, &free_mb);
            ESP_LOGI(TAG, "SD card ready: %d MB total, %d MB free",
                     total_mb, free_mb);

        #if CONFIG_SMARTBOX_ANSWER_CACHE_ENABLED
            answer_cache_init();
        #endif
        #if CONFIG_SMARTBOX_PHOTO_HISTORY_ENABLED
            photo_history_init();
        #endif
        #if CONFIG_SMARTBOX_WRONG_BOOK_ENABLED
            wrong_book_init();
        #endif

            font_loader_init();
            icon_loader_init();
            /* home_ui_update_fonts() 不需要 — Home 将在 boot_ui_finish 之后创建，此时 SD 字体已就绪 */
            ocr_recover_pending();
        #if CONFIG_SMARTBOX_NES_EMU_ENABLED
            nes_emu_init();
        #endif
        } else {
            ESP_LOGW(TAG, "SD card not available (ret=0x%x), "
                     "history/cache features disabled", sd_ret);
        }
    }
#endif

    boot_ui_set_progress(60, "STORAGE READY");

    /* 电池 ADC 初始化 */
    battery_init();
    battery_read(NULL, NULL);

    /* WiFi 初始化 — SDMMC Slot 1 (SDIO to ESP32-C6) */
    wifi_app_init();
    wifi_register_callback(on_wifi_event, NULL);
    wifi_auto_connect();

    boot_ui_set_progress(80, "NETWORK READY");

    /* 学习时长后台计时器 — 每 60 秒计入 1 分钟 */
    const esp_timer_create_args_t study_timer_args = {
        .callback = study_timer_cb,
        .arg = NULL,
        .name = "study_timer",
        .dispatch_method = ESP_TIMER_TASK,
    };
    if (esp_timer_create(&study_timer_args, &s_study_timer) == ESP_OK) {
        esp_timer_start_periodic(s_study_timer, 60 * 1000 * 1000);  /* 60 秒 */
    } else {
        ESP_LOGE(TAG, "Failed to create study timer");
    }

    /* 电池定时器 — 每 5 秒读取一次电池电量并更新缓存 */
    const esp_timer_create_args_t battery_timer_args = {
        .callback = battery_timer_cb,
        .arg = NULL,
        .name = "battery",
        .dispatch_method = ESP_TIMER_TASK,
    };
    if (esp_timer_create(&battery_timer_args, &s_battery_timer) == ESP_OK) {
        esp_timer_start_periodic(s_battery_timer, 5 * 1000 * 1000);  /* 5 秒 */
    } else {
        ESP_LOGE(TAG, "Failed to create battery timer");
    }

    /* LLM API Key 首次自动配置（如 NVS 中不存在则提示用户） */
    {
        char key_buf[64];
        size_t len = sizeof(key_buf);
        if (storage_load_llm_key(key_buf, &len) != ESP_OK) {
            /* 首次运行，无默认 Key — 用户在 WiFi 配置界面或设置中自行输入 */
            ESP_LOGW(TAG, "LLM API Key not configured — "
                     "please set it in the settings page or via "
                     "'idf.py menuconfig → SmartBox → LLM API Key'");
        }
    }

    /* HTTPS 自检任务已禁用。
     * 原用途: 开机验证 TLS/SSL 配置(百度稳定 HTTP 200, 已证实配置正确)。
     * 现禁用原因: Test 2 打 ark.cn-beijing.volces.com 会近乎必现触发 SDIO 258
     * → 已启用 CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE=y → esp_restart(),
     * 若仍无条件跑在启动路径上会导致 boot loop(每 ~30s 重启)。
     * 需要临时诊断时手动取消注释即可。 */
    /* xTaskCreate(https_test_task, "https_test", 4096, NULL, 4, NULL); */
    (void)https_test_task;  /* 保留函数, 抑制 unused 警告 */

#if CONFIG_SMARTBOX_AI_TEST_MODE
    /* AI 测试模式已移除 — 后端/LLM/API 功能已清理 */
    ESP_LOGW(TAG, "AI_TEST_MODE is enabled but AI backend has been removed");
#endif

    /* ── 完成启动: 100% → "SYSTEM READY" → 500ms → exit callback → Home ── */
    boot_ui_set_progress(95, "FINALIZING...");

    esp_lv_adapter_lock(-1);
    boot_ui_finish(boot_exit_to_home);
    esp_lv_adapter_unlock();

    while (1) {
        /* 处理学习时长累积（spinlock 防止定时器任务并发写入） */
        portENTER_CRITICAL(&s_study_spinlock);
        int pending = s_study_minutes_pending;
        s_study_minutes_pending = 0;
        portEXIT_CRITICAL(&s_study_spinlock);
        if (pending > 0) {
            for (int i = 0; i < pending; i++) {
                achievement_complete_task(ACHV_TASK_STUDY_MINUTE, 1);
                s_study_minutes_total++;
            }
        }

        /* 喂狗 — 防止 HTTP 等同步阻塞触发 TASK_WDT */
#if CONFIG_SMARTBOX_TASK_WDT_ENABLED
        esp_task_wdt_reset();
#endif

        /* TODO: 系统健康检查、低功耗调度 */
        vTaskDelay(pdMS_TO_TICKS(1000));  /* 1 秒周期，减少无效唤醒 */
    }
}

/* ── 系统初始化入口 ── */
void system_init(void)
{
    ESP_LOGI(TAG, "system_init() starting");

    /* 0. 系统级基础初始化 (NVS → event loop) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());

    /* 0.5 Storage 初始化 (NVS 已就绪) */
    ESP_ERROR_CHECK(storage_init());

    /* 1. BSP 硬件初始化 */
    ESP_ERROR_CHECK(lcd_init());
    ESP_ERROR_CHECK(touch_init());

    /* Camera 为非关键模块：初始化失败不阻塞系统启动 */
#if CONFIG_SMARTBOX_CAMERA_ENABLED
    {
        esp_err_t cam_ret = camera_init();
        if (cam_ret != ESP_OK) {
            ESP_LOGW(TAG, "Camera init failed (0x%x) — OCR disabled", cam_ret);
        } else {
            camera_manager_init();
        }
    }
#endif

    /* Vision 平台初始化 */
    vision_core_init();

    /* 2. LVGL port 初始化（接管 display + touch + task） */
    ESP_ERROR_CHECK(lvgl_port_init());

    /* 3. 创建主逻辑任务 */
    xTaskCreate(main_task, "main", 8192, NULL, 3, NULL);

    ESP_LOGI(TAG, "system_init() complete");
}