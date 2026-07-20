/**
 * @file wifi.h
 * @brief WiFi 模块 — ESP32-P4 + ESP32-C6 (SDIO / esp_wifi_remote Hosted)
 *
 * 参考: E:\esp-hosted-mcu\slave\main\slave_wifi_std.c
 *   - event_handler_wifi() (L2182-2320):  事件分发逻辑
 *   - req_wifi_scan_get_ap_records() (L1355-1450): 扫描结果字段
 *   - req_wifi_connect() (L656-710):      连接状态管理
 *
 * 架构: ESP32-P4 (Host) ──SDIO 4-bit── ESP32-C6 (Slave / WiFi 6)
 *       esp_wifi_remote (Hosted) 将 esp_wifi_* 调用重定向到 C6
 *
 * 初始化序列:
 *   NVS → netif → event_loop → create_sta → attach → wifi_init → set_mode → wifi_start
 *
 * 提供:
 *   - wifi_app_init()           一键初始化
 *   - wifi_scan()               非阻塞扫描
 *   - wifi_connect()            连接网络
 *   - wifi_disconnect()         断开连接
 *   - wifi_is_connected()       查询连接状态
 *   - wifi_get_state()          查询状态机
 *   - wifi_register_callback()  注册事件回调 (BSP → APP)
 *   - wifi_get_disconnect_reason()  获取断开原因
 */

#pragma once

#include "esp_err.h"
#include "esp_wifi.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 配置常量 ── */

#define WIFI_SCAN_MAX_ITEMS   20
#define WIFI_SSID_MAX_LEN     33
#define WIFI_IP_MAX_LEN       16
#define WIFI_PWD_MAX_LEN      64

/* 自动重连参数 */
#define WIFI_MAX_RETRY          5
#define WIFI_RETRY_BASE_DELAY_MS  500

/* RSSI 轮询周期 (ms) — 连接状态下每 5 秒更新 */
#define WIFI_RSSI_POLL_INTERVAL_MS  5000

/* ── WiFi 状态机 ── */

typedef enum {
    WIFI_STATE_IDLE,          /* 未初始化 */
    WIFI_STATE_SCANNING,      /* 正在扫描 */
    WIFI_STATE_CONNECTING,    /* 正在连接 */
    WIFI_STATE_CONNECTED,     /* 已连接 */
    WIFI_STATE_DISCONNECTED,  /* 已断开 */
} wifi_state_t;

/* ── 回调事件类型 ── */

typedef enum {
    WIFI_CB_SCAN_DONE,        /* 扫描完成 (g_wifi_scan_list 已更新) */
    WIFI_CB_CONNECTED,        /* 已连接到 AP */
    WIFI_CB_DISCONNECTED,     /* 已断开连接 */
    WIFI_CB_GOT_IP,           /* 获取到 IP 地址 */
    WIFI_CB_LOST_IP,          /* 丢失 IP 地址 */
    WIFI_CB_STATE_CHANGED,    /* 状态机状态变更 */
} wifi_cb_event_t;

/* ── 回调函数类型 ── */

typedef void (*wifi_callback_t)(wifi_cb_event_t event, void *arg);

/* ── 扫描结果项 ── */

typedef struct {
    char            ssid[WIFI_SSID_MAX_LEN];
    int8_t          rssi;
    uint8_t         channel;        /* 主信道 (1-14 for 2.4G, 36-165 for 5G) */
    wifi_auth_mode_t authmode;      /* WIFI_AUTH_OPEN / WIFI_AUTH_WPA_WPA2_PSK / WIFI_AUTH_WPA3_PSK ... */
    bool            connected;      /* 是否为当前连接的网络 */
} wifi_scan_item_t;

/* ── 共享状态 (BSP 写入, APP 只读) ── */

extern wifi_scan_item_t g_wifi_scan_list[WIFI_SCAN_MAX_ITEMS];
extern int              g_wifi_scan_count;
extern bool             g_wifi_connected;
extern char             g_wifi_current_ssid[WIFI_SSID_MAX_LEN];
extern char             g_wifi_current_ip[WIFI_IP_MAX_LEN];
extern int8_t           g_wifi_current_rssi;

/* ── API ── */

/**
 * @brief 一键初始化 WiFi (NVS → netif → event → WiFi init → start)
 *
 * C6 CHIP_PU 上电后由 esp_hosted 构造函数自动完成。
 * 手动创建 STA netif + attach（不使用默认 WIFI_STA handler，避免 double STA_START crash）。
 * 注册事件回调，设置 STA 模式并启动 WiFi 栈。
 *
 * @return ESP_OK 成功
 */
esp_err_t wifi_app_init(void);

/**
 * @brief 非阻塞扫描附近 WiFi 网络
 *
 * 扫描完成后通过 WIFI_EVENT_SCAN_DONE 事件异步通知，
 * 结果写入 g_wifi_scan_list / g_wifi_scan_count，然后触发 WIFI_CB_SCAN_DONE 回调。
 *
 * @return ESP_OK 扫描请求已发出
 */
esp_err_t wifi_scan(void);

/**
 * @brief 连接到 WiFi 网络
 *
 * @param ssid     网络名称 (最大 32 字节)
 * @param password 密码 (最大 63 字节, 开放网络传 NULL 或 "")
 * @return ESP_OK 连接请求已发出
 */
esp_err_t wifi_connect(const char *ssid, const char *password);

/**
 * @brief 断开当前 WiFi 连接
 *
 * @return ESP_OK 成功
 */
esp_err_t wifi_disconnect(void);

/**
 * @brief 查询是否已连接 (IP 层就绪)
 */
bool wifi_is_connected(void);

/**
 * @brief 查询当前 WiFi 状态机状态
 */
wifi_state_t wifi_get_state(void);

/**
 * @brief 获取最近一次断开的原因描述
 *
 * @return 可读的原因字符串; NULL 表示从未断开
 */
const char *wifi_get_disconnect_reason(void);

/**
 * @brief 注册 WiFi 事件回调（支持最多 4 个订阅者）
 *
 * 回调在 esp_event 任务上下文中执行，不应执行耗时操作。
 * 如需刷新 UI，在回调中通过 lv_async_call 通知 LVGL 任务。
 *
 * @param cb  回调函数指针
 * @param arg 用户参数 (传给每次回调)
 */
void wifi_register_callback(wifi_callback_t cb, void *arg);

/**
 * @brief 自动连接 WiFi（优先 NVS → 回退 sdkconfig）
 *
 * 仅在 CONFIG_SMARTBOX_WIFI_AUTO_CONNECT=y 时执行连接逻辑。
 * 连接优先级: NVS 保存的凭证 > sdkconfig 默认凭证。
 * 如果均未配置或为默认值，则跳过连接。
 */
void wifi_auto_connect(void);

#ifdef __cplusplus
}
#endif
