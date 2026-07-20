/**
 * @file wifi.c
 * @brief WiFi 模块 — ESP32-P4 + ESP32-C6 (SDIO / esp_wifi_remote Hosted)
 *
 * 初始化序列严格参照官方 host_hosted_events/station_example.c:
 *   esp_netif_init() → esp_netif_create_default_wifi_sta() → esp_wifi_init()
 *   → set_mode → set_config → esp_wifi_start() → (events: STA_START→connect, GOT_IP)
 *
 * 架构: ESP32-P4 (Host) ──SDIO 4-bit── ESP32-C6 (Slave / WiFi 6)
 *       esp_wifi_remote (Hosted) 将 esp_wifi_* 调用重定向到 C6
 *       DHCP 在主机端 lwIP 运行，数据帧由 esp_wifi_remote 内部 rxcb 转发
 */

#include "wifi.h"
#include "bsp/wifi/c6_ota.h"
#include "bsp/storage/storage.h"
#include "bsp/time/sys_time.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "WIFI";

/* ═══════════════════════════════════════════════
   全局状态 (BSP 写入, APP 只读)
   ═══════════════════════════════════════════════ */
wifi_scan_item_t g_wifi_scan_list[WIFI_SCAN_MAX_ITEMS] = {0};
int              g_wifi_scan_count = 0;
bool             g_wifi_connected  = false;
char             g_wifi_current_ssid[WIFI_SSID_MAX_LEN] = {0};
char             g_wifi_current_ip[WIFI_IP_MAX_LEN]     = {0};
int8_t           g_wifi_current_rssi = 0;

/* ── 内部状态 ── */
static bool                s_initialized    = false;
static wifi_state_t        s_state          = WIFI_STATE_IDLE;
static int                 s_retry_num      = 0;
static char                s_disconnect_reason[64] = {0};
static esp_netif_t        *s_sta_netif      = NULL;
static bool                s_boot_cred_set  = false;   /* 是否已在 esp_wifi_start() 之前设好 STA 配置（参考工程模式，避免 start 后再 set_config 触发二次 STA_START） */

/* ── 定时器句柄 ── */
static esp_timer_handle_t  s_retry_timer    = NULL;
static esp_timer_handle_t  s_rssi_timer     = NULL;

/* ── 回调 ── */
#define WIFI_MAX_CALLBACKS  4
static struct {
    wifi_callback_t cb;
    void *arg;
} s_callbacks[WIFI_MAX_CALLBACKS] = {0};
static int s_callback_count = 0;

/* ── 前向声明 ── */
static void set_state(wifi_state_t new_state);
static void notify_callback(wifi_cb_event_t event);
static void start_rssi_poll(void);
static void stop_rssi_poll(void);
static void schedule_reconnect(uint32_t delay_ms);
static esp_err_t wifi_stack_build(bool do_ota);

/* ═══════════════════════════════════════════════
   断开原因码 → 可读文本
   参考: ESP-IDF wifi_err_reason_t, 从机 slave_wifi_std.c L2250
   ═══════════════════════════════════════════════ */
static const char *disconnect_reason_str(uint8_t reason)
{
    switch (reason) {
    case 1:   return "关联过期";
    case 2:   return "主动断开";
    case 3:   return "认证失败 (密码错误?)";
    case 4:   return "未找到AP";
    case 15:  return "密码错误 (握手超时)";
    case 200: return "信号丢失";
    case 201: return "连接失败 (对端拒绝)";
    case 203: return "关联失败";
    case 205: return "连接失败";
    default:  return "未知原因";
    }
}

/* ═══════════════════════════════════════════════
   状态机
   ═══════════════════════════════════════════════ */
static void set_state(wifi_state_t new_state)
{
    if (s_state != new_state) {
        ESP_LOGI(TAG, "State: %d -> %d", s_state, new_state);
        s_state = new_state;
        notify_callback(WIFI_CB_STATE_CHANGED);
    }
}

wifi_state_t wifi_get_state(void)           { return s_state; }
const char *wifi_get_disconnect_reason(void) { return (s_disconnect_reason[0] != '\0') ? s_disconnect_reason : NULL; }

/* ═══════════════════════════════════════════════
   回调
   ═══════════════════════════════════════════════ */
void wifi_register_callback(wifi_callback_t cb, void *arg)
{
    if (s_callback_count < WIFI_MAX_CALLBACKS) {
        s_callbacks[s_callback_count].cb  = cb;
        s_callbacks[s_callback_count].arg = arg;
        s_callback_count++;
        ESP_LOGI(TAG, "Callback #%d registered", s_callback_count);
    } else {
        ESP_LOGW(TAG, "Callback slots full (max %d)", WIFI_MAX_CALLBACKS);
    }
}

static void notify_callback(wifi_cb_event_t event)
{
    for (int i = 0; i < s_callback_count; i++) {
        if (s_callbacks[i].cb) s_callbacks[i].cb(event, s_callbacks[i].arg);
    }
}

/* ═══════════════════════════════════════════════
   RSSI 轮询 (连接后每 5s 更新)
   ═══════════════════════════════════════════════ */
static void rssi_poll_cb(void *arg)
{
    if (!g_wifi_connected) return;
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK && ap_info.rssi != g_wifi_current_rssi) {
        g_wifi_current_rssi = ap_info.rssi;
        ESP_LOGD(TAG, "RSSI: %d dBm", ap_info.rssi);
    }
}

static void start_rssi_poll(void)
{
    if (s_rssi_timer) return;
    const esp_timer_create_args_t args = {
        .callback = rssi_poll_cb, .arg = NULL, .name = "wifi_rssi",
        .dispatch_method = ESP_TIMER_TASK,
    };
    if (esp_timer_create(&args, &s_rssi_timer) == ESP_OK) {
        esp_timer_start_periodic(s_rssi_timer, WIFI_RSSI_POLL_INTERVAL_MS * 1000);
        ESP_LOGI(TAG, "RSSI poll started");
    }
}

static void stop_rssi_poll(void)
{
    if (s_rssi_timer) {
        esp_timer_stop(s_rssi_timer);
        esp_timer_delete(s_rssi_timer);
        s_rssi_timer = NULL;
    }
}

/* ═══════════════════════════════════════════════
   自动重连 (esp_timer, 不阻塞事件循环)
   ═══════════════════════════════════════════════ */
static void retry_connect_cb(void *arg)
{
    ESP_LOGI(TAG, "Retry reconnect #%d", s_retry_num);
    esp_wifi_connect();
}

static void schedule_reconnect(uint32_t delay_ms)
{
    if (!s_retry_timer) {
        const esp_timer_create_args_t args = {
            .callback = retry_connect_cb, .arg = NULL, .name = "wifi_retry",
            .dispatch_method = ESP_TIMER_TASK,
        };
        esp_timer_create(&args, &s_retry_timer);
    } else {
        esp_timer_stop(s_retry_timer);
    }
    if (s_retry_timer) {
        esp_timer_start_once(s_retry_timer, delay_ms * 1000);
        ESP_LOGI(TAG, "Reconnect in %" PRIu32 " ms", delay_ms);
    }
}

/* ═══════════════════════════════════════════════
   WiFi / IP 事件回调
   参照官方 host_hosted_events/station_example.c event_handler()
   STA_START → connect, STA_DISCONNECTED → retry, GOT_IP → success
   ═══════════════════════════════════════════════ */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "*** STA STARTED ***");
        /* netif 生命周期完全交给默认处理器，不在此调用任何 esp_netif_action_*。
         * 配置已在 esp_wifi_start() 之前设好，STA_START 只会来一次。
         * 注意: 不在事件处理器内调 esp_wifi_connect()——从事件上下文调 connect
         * 会导致默认处理器 esp_netif_action_start 再跑一次 netif_add → abort。
         * connect 改由 wifi_auto_connect() 在 main_task 上下文延迟调用。
         * 实测: esp_hosted 2.12.9 下此写法可正常连接并取 IP。 */

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *evt = (wifi_event_sta_connected_t *)event_data;
        ESP_LOGI(TAG, "*** CONNECTED to \"%s\" ch=%d auth=%d ***",
                 evt->ssid, evt->channel, evt->authmode);

        s_retry_num = 0;
        strncpy(g_wifi_current_ssid, (char *)evt->ssid, WIFI_SSID_MAX_LEN - 1);
        g_wifi_connected = true;
        s_disconnect_reason[0] = '\0';
        set_state(WIFI_STATE_CONNECTED);

        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) g_wifi_current_rssi = ap_info.rssi;

        for (int i = 0; i < g_wifi_scan_count; i++)
            g_wifi_scan_list[i].connected = (strcmp(g_wifi_scan_list[i].ssid, g_wifi_current_ssid) == 0);

        /* netif 启动 DHCP 由默认处理器 esp_netif_action_connected 负责（未注销），
         * 此处只做应用层状态记录，避免与默认处理器重复调用。 */

        start_rssi_poll();
        notify_callback(WIFI_CB_CONNECTED);

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *evt = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "*** DISCONNECTED reason=%d (%s) ***", evt->reason, disconnect_reason_str(evt->reason));

        snprintf(s_disconnect_reason, sizeof(s_disconnect_reason), "%s (code=%d)",
                 disconnect_reason_str(evt->reason), evt->reason);

        g_wifi_connected = false;
        memset(g_wifi_current_ssid, 0, sizeof(g_wifi_current_ssid));
        memset(g_wifi_current_ip, 0, sizeof(g_wifi_current_ip));
        stop_rssi_poll();

        /* netif 停止 DHCP 由默认处理器 esp_netif_action_disconnected 负责（未注销）。 */

        /* WiFi 断开后停止 SNTP 时间同步 */
        time_sync_stop();

        for (int i = 0; i < g_wifi_scan_count; i++) g_wifi_scan_list[i].connected = false;

        if (s_retry_num < WIFI_MAX_RETRY) {
            s_retry_num++;
            set_state(WIFI_STATE_DISCONNECTED);
            schedule_reconnect(s_retry_num * WIFI_RETRY_BASE_DELAY_MS);
        } else {
            ESP_LOGE(TAG, "Max retries (%d) reached", WIFI_MAX_RETRY);
            set_state(WIFI_STATE_DISCONNECTED);
        }
        notify_callback(WIFI_CB_DISCONNECTED);

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "*** SCAN DONE ***");

        uint16_t ap_count = WIFI_SCAN_MAX_ITEMS;
        wifi_ap_record_t ap_records[WIFI_SCAN_MAX_ITEMS];
        esp_err_t ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
        if (ret != ESP_OK) { ESP_LOGE(TAG, "get_ap_records: %s", esp_err_to_name(ret)); return; }

        g_wifi_scan_count = 0;
        for (int i = 0; i < ap_count && g_wifi_scan_count < WIFI_SCAN_MAX_ITEMS; i++) {
            if (ap_records[i].ssid[0] == '\0') continue; // 过滤空条目

            wifi_scan_item_t *item = &g_wifi_scan_list[g_wifi_scan_count];
            strncpy(item->ssid, (char *)ap_records[i].ssid, WIFI_SSID_MAX_LEN - 1);
            item->rssi      = ap_records[i].rssi;
            item->channel   = ap_records[i].primary;
            item->authmode  = ap_records[i].authmode;
            item->connected = (g_wifi_connected && strcmp(item->ssid, g_wifi_current_ssid) == 0);
            if (item->connected) g_wifi_current_rssi = ap_records[i].rssi;
            g_wifi_scan_count++;
        }

        ESP_LOGI(TAG, "Scan: %d AP(s)", g_wifi_scan_count);
        set_state(g_wifi_connected ? WIFI_STATE_CONNECTED : WIFI_STATE_DISCONNECTED);
        notify_callback(WIFI_CB_SCAN_DONE);

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "*** GOT IP: " IPSTR " ***", IP2STR(&evt->ip_info.ip));

        /* 默认路由 / DNS 由默认处理器 esp_netif_action_got_ip 负责（未注销）。 */
        snprintf(g_wifi_current_ip, sizeof(g_wifi_current_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        notify_callback(WIFI_CB_GOT_IP);

        /* WiFi 获取 IP 后启动 SNTP 时间同步 */
        time_sync_start();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGW(TAG, "*** LOST IP ***");
        memset(g_wifi_current_ip, 0, sizeof(g_wifi_current_ip));
        notify_callback(WIFI_CB_LOST_IP);
    }
}

/* ═══════════════════════════════════════════════
   加载 STA 凭据（NVS 优先，回退 sdkconfig）
   返回 true 表示取到可用 SSID
   ═══════════════════════════════════════════════ */
static bool load_sta_credentials(char *ssid, size_t ssid_len, char *pwd, size_t pwd_len)
{
    if (storage_load_wifi_cred(ssid, ssid_len, pwd, pwd_len) == ESP_OK && ssid[0]) {
        ESP_LOGI(TAG, "Using NVS credentials: \"%s\"", ssid);
        return true;
    }
    const char *cs = CONFIG_SMARTBOX_WIFI_SSID;
    if (cs && cs[0]) {
        strncpy(ssid, cs, ssid_len - 1);
        strncpy(pwd, CONFIG_SMARTBOX_WIFI_PASSWORD, pwd_len - 1);
        ESP_LOGI(TAG, "Using sdkconfig credentials: \"%s\"", ssid);
        return true;
    }
    return false;
}

/* ═══════════════════════════════════════════════
   wifi_app_init
   严格参照官方 host_hosted_events/station_example.c example_wifi_init_sta():
     esp_netif_create_default_wifi_sta() → esp_wifi_init() → set_mode → set_config → start
   ═══════════════════════════════════════════════ */
esp_err_t wifi_app_init(void)
{
    if (s_initialized) { ESP_LOGW(TAG, "Already initialized"); return ESP_OK; }

    ESP_LOGI(TAG, "===== WiFi App Init Start =====");

    esp_err_t ret = wifi_stack_build(true);  /* 首次启动: 含 C6 OTA 检查 */
    if (ret != ESP_OK) return ret;

    set_state(WIFI_STATE_DISCONNECTED);
    ESP_LOGI(TAG, "===== WiFi App Init Complete =====");
    return ESP_OK;
}

/* ═══════════════════════════════════════════════
   wifi_stack_build — netif/驱动/wifi 栈的可复用构建序列
   首次启动(do_ota=true, 含 C6 OTA 检查)和优雅恢复(do_ota=false)共用,
   保证初始化次序单一来源(set_config 必须在 start 前, 参考工程约束)。
   ═══════════════════════════════════════════════ */
static esp_err_t wifi_stack_build(bool do_ota)
{
    /* 步骤 1: 创建默认 STA netif —— 完全沿用官方默认处理器（含 Hosted 版对数据通道 /
     *   DHCP 取 IP 的关键绑定）。netif 生命周期全部交给默认处理器,
     *   本模块 event_handler 只做应用层状态记录。 */
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) { ESP_LOGE(TAG, "netif create failed"); return ESP_FAIL; }
    ESP_LOGI(TAG, "STA netif created: %p", s_sta_netif);

    /* 步骤 2: 注册 WIFI/IP 事件回调（必须在 esp_wifi_init() 之前） */
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_LOST_IP, &event_handler, NULL));
    ESP_LOGI(TAG, "Event handlers registered");

    /* 步骤 3: 初始化 WiFi (内部触发 add_esp_wifi_remote_channels) */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "wifi_init: %s", esp_err_to_name(ret)); return ret; }
    ESP_LOGI(TAG, "esp_wifi_init() OK");

    /* 步骤 3.5: C6 协处理器固件检查 */
    if (do_ota) {
        esp_err_t ota_ret = c6_ota_check_and_update();
        if (ota_ret != ESP_OK && ota_ret != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "C6 OTA failed (%s), continuing with current firmware",
                     esp_err_to_name(ota_ret));
        }
    }

    /* 步骤 4: STA 模式 */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    /* 步骤 4.5: 【关键】在 esp_wifi_start() 之前设好凭据(避免二次 STA_START → netif_add 崩溃)。 */
    s_boot_cred_set = false;
#if CONFIG_SMARTBOX_WIFI_AUTO_CONNECT
    {
        char ssid[WIFI_SSID_MAX_LEN] = {0}, pwd[WIFI_PWD_MAX_LEN] = {0};
        if (load_sta_credentials(ssid, sizeof(ssid), pwd, sizeof(pwd))) {
            wifi_config_t wc = {0};
            strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
            if (pwd[0]) strncpy((char *)wc.sta.password, pwd, sizeof(wc.sta.password) - 1);
            esp_err_t cret = esp_wifi_set_config(WIFI_IF_STA, &wc);
            if (cret == ESP_OK) {
                strncpy(g_wifi_current_ssid, ssid, WIFI_SSID_MAX_LEN - 1);
                s_boot_cred_set = true;
                ESP_LOGI(TAG, "Boot credentials applied before start: \"%s\"", ssid);
            } else {
                ESP_LOGW(TAG, "set_config(boot): %s", esp_err_to_name(cret));
            }
        }
    }
#endif

    /* 步骤 5: 启动 */
    ret = esp_wifi_start();
    if (ret != ESP_OK) { ESP_LOGE(TAG, "wifi_start: %s", esp_err_to_name(ret)); return ret; }
    ESP_LOGI(TAG, "esp_wifi_start() OK");

    s_initialized = true;
    return ESP_OK;
}

/* ═══════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════ */

esp_err_t wifi_scan(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG, "Scanning...");
    set_state(WIFI_STATE_SCANNING);

    /* 增加每信道扫描时间以发现更多 AP
     * C6 仅 2.4GHz (ch 1-13)，active scan 每信道 200-500ms */
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL, .bssid = NULL, .channel = 0,
        .show_hidden = true, .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 200, .max = 500 } },
    };
    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, false);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "scan_start: %s", esp_err_to_name(ret)); }
    return ret;
}

esp_err_t wifi_connect(const char *ssid, const char *password)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!ssid || ssid[0] == '\0') return ESP_ERR_INVALID_ARG;

    if (s_state == WIFI_STATE_CONNECTING) { ESP_LOGW(TAG, "Already connecting"); return ESP_ERR_INVALID_STATE; }

    ESP_LOGI(TAG, "Connecting to \"%s\"%s...", ssid,
             (password && password[0]) ? " (encrypted)" : " (open)");

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    if (password && password[0]) strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);

    s_retry_num = 0;
    s_disconnect_reason[0] = '\0';
    strncpy(g_wifi_current_ssid, ssid, WIFI_SSID_MAX_LEN - 1);
    set_state(WIFI_STATE_CONNECTING);

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "set_config: %s", esp_err_to_name(ret)); set_state(WIFI_STATE_DISCONNECTED); return ret; }

    ret = esp_wifi_connect();
    if (ret != ESP_OK) { ESP_LOGE(TAG, "connect: %s", esp_err_to_name(ret)); set_state(WIFI_STATE_DISCONNECTED); return ret; }

    ESP_LOGI(TAG, "Connection request sent");
    return ESP_OK;
}

esp_err_t wifi_disconnect(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG, "Disconnecting...");

    s_retry_num = WIFI_MAX_RETRY;
    if (s_retry_timer) esp_timer_stop(s_retry_timer);
    stop_rssi_poll();

    g_wifi_connected = false;
    memset(g_wifi_current_ssid, 0, sizeof(g_wifi_current_ssid));
    memset(g_wifi_current_ip, 0, sizeof(g_wifi_current_ip));
    for (int i = 0; i < g_wifi_scan_count; i++) g_wifi_scan_list[i].connected = false;

    set_state(WIFI_STATE_DISCONNECTED);
    return esp_wifi_disconnect();
}

bool wifi_is_connected(void) { return g_wifi_connected; }

void wifi_auto_connect(void)
{
#if CONFIG_SMARTBOX_WIFI_AUTO_CONNECT
    if (s_boot_cred_set) {
        /* 延迟 200ms 让 STA_START 事件被默认处理器处理完毕 (netif_add),
         * 然后在 main_task 安全上下文中发起连接。
         * 不在 STA_START 事件处理器内调 esp_wifi_connect() 的原因:
         *   事件上下文内 connect 会导致默认 esp_netif_action_start 二次执行
         *   → netif_add abort。实测 esp_hosted 2.12.9 下此写法可正常取 IP。 */
        vTaskDelay(pdMS_TO_TICKS(200));
        set_state(WIFI_STATE_CONNECTING);
        esp_err_t r = esp_wifi_connect();
        ESP_LOGI(TAG, "Auto-connect: %s", esp_err_to_name(r));
    } else {
        ESP_LOGI(TAG, "Auto-connect: no stored/sdkconfig credentials, waiting for UI config");
    }
#else
    ESP_LOGI(TAG, "Auto-connect disabled");
#endif
}