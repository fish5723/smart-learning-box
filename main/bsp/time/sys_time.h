/**
 * @file sys_time.h
 * @brief 系统时间模块 — SNTP 网络时间同步
 *
 * 功能:
 *   - 通过 SNTP (Simple Network Time Protocol) 从 NTP 服务器同步系统时间
 *   - WiFi 获取 IP 后自动启动同步，断开后停止
 *   - 提供时间同步状态查询
 *   - 提供本地时间获取 (CST, UTC+8)
 *
 * 注意：本文件命名为 sys_time.h 以避免与 C 标准库 <time.h> 冲突。
 *
 * 依赖:
 *   - lwIP SNTP (sdkconfig 中已启用: CONFIG_LWIP_SNTP_MAX_SERVERS=1)
 *   - esp_sntp.h (ESP-IDF SNTP API)
 *
 * 使用:
 *   1. time_init()          — 一次初始化
 *   2. time_sync_start()    — 启动 SNTP 时间同步
 *   3. time_sync_stop()     — 停止 SNTP 时间同步
 *   4. time_is_synchronized() — 查询时间是否已同步
 *   5. time_get_now()       — 获取当前 Unix 时间戳
 *   6. time_get_local_str() — 获取格式化本地时间字符串
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── NTP 服务器配置 ── */

#define TIME_NTP_SERVER         "ntp.aliyun.com"   /* 阿里云 NTP (国内速度快) */
#define TIME_NTP_SERVER_BACKUP  "cn.pool.ntp.org"   /* 备用: 中国 NTP 池 */

/* ── 时区配置 ── */

#define TIME_TZ_CST             "CST-8"             /* 中国标准时间 UTC+8 */

/* ── 时间同步状态 ── */

typedef enum {
    TIME_SYNC_IDLE,           /* 未启动 */
    TIME_SYNC_IN_PROGRESS,    /* 正在同步 */
    TIME_SYNC_COMPLETED,      /* 同步成功 */
    TIME_SYNC_FAILED,         /* 同步失败 */
} time_sync_state_t;

/* ── 时间格式化缓冲区大小 ── */
#define TIME_STR_MAX_LEN        64

/* ═══════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════ */

/**
 * @brief 初始化时间模块
 *
 * 设置时区为 CST (UTC+8)，配置 NTP 服务器地址。
 * 应在系统初始化时调用一次 (如 system_init.c 中)。
 *
 * @return ESP_OK 成功
 */
esp_err_t time_init(void);

/**
 * @brief 启动 SNTP 时间同步
 *
 * 由 WiFi 模块在获取到 IP 地址后自动调用。
 * 如果首次同步失败，SNTP 内部会自动重试。
 *
 * @return ESP_OK 请求已发出
 */
esp_err_t time_sync_start(void);

/**
 * @brief 停止 SNTP 时间同步
 *
 * 由 WiFi 模块在断开连接时自动调用。
 * 停止后系统时间保留最后同步的值 (由 RTC 定时器维护)。
 */
void time_sync_stop(void);

/**
 * @brief 查询时间同步状态
 *
 * @return true  时间已同步 (SNTP_SYNC_STATUS_COMPLETED)
 * @return false 尚未同步或在同步中
 */
bool time_is_synchronized(void);

/**
 * @brief 获取当前时间同步状态枚举
 */
time_sync_state_t time_get_sync_state(void);

/**
 * @brief 获取当前 Unix 时间戳 (秒)
 *
 * 即使 SNTP 尚未同步，也会返回系统时间 (上电后默认从 0 开始)。
 * 建议先调用 time_is_synchronized() 判断时间是否准确。
 *
 * @return Unix 时间戳 (自 1970-01-01 00:00:00 UTC)
 */
time_t time_get_now(void);

/**
 * @brief 获取格式化本地时间字符串 (如 "2025-07-05 14:30:00")
 *
 * @param buf   输出缓冲区
 * @param len   缓冲区大小 (建议 >= TIME_STR_MAX_LEN)
 * @return 同 buf (方便内联使用)
 */
const char *time_get_local_str(char *buf, size_t len);

#ifdef __cplusplus
}
#endif