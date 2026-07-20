/**
 * @file crash_log.h
 * @brief 崩溃日志 — 系统异常时通过串口输出 backtrace + 状态
 *
 * 设计:
 *   - 启动时检查复位原因, 异常复位自动通过 ESP_LOG 输出状态快照
 *   - 不写 SD 卡 — 避免频繁复位 (boot loop) 损坏 SD 卡
 *   - BSP 级别, 不依赖 LVGL/UI, 不依赖 SD 卡
 *
 * 依赖:
 *   - FreeRTOS 任务统计 (configUSE_TRACE_FACILITY)
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化崩溃日志子系统
 *
 * - 检查 esp_reset_reason(), 若为异常复位则通过 ESP_LOG 输出状态快照
 * - 所有输出走串口，不写 SD 卡
 *
 * 可在系统初始化阶段任意时机调用，不依赖 SD 卡挂载。
 *
 * @return ESP_OK 成功
 */
esp_err_t crash_log_init(void);

/**
 * @brief 手动触发状态 dump 通过串口输出
 *
 * 输出内容: 触发原因、复位原因、free heap、uptime
 *
 * @param trigger_reason 触发原因 (如 "manual", "shutdown", "panic")
 * @return ESP_OK 成功
 */
esp_err_t crash_log_dump_state(const char *trigger_reason);

#ifdef __cplusplus
}
#endif
