/**
 * @file battery.h
 * @brief 电池电量检测 BSP 模块 — ADC Oneshot 模式
 *
 * 参考: E:\IOT_competition\battery_adc_test
 *
 * 硬件:
 *   - ESP32-P4 ADC2_CH4 (GPIO53), ADC_ATTEN_DB_12 (0-2.5V)
 *   - 电池经分压电阻接入 (R52=68K, R57=100K), 500 次采样取平均值
 *   - 平滑: 指数移动平均 (alpha=0.5)
 *
 * 使用:
 *   1. battery_init()   — 初始化 ADC + 校准
 *   2. battery_read()   — 非阻塞读取电量百分比 (0-100)
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── ADC 硬件配置 ── */
#define BATTERY_ADC_UNIT        ADC_UNIT_2
#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_4  /* GPIO53 → ADC2_CH4 on ESP32-P4 */
#define BATTERY_ADC_ATTEN       ADC_ATTEN_DB_12
#define BATTERY_ADC_BITWIDTH    ADC_BITWIDTH_12

/* ── 分压器标定 ── */
/* 分压电阻: R52=68K (上拉), R57=100K (下拉)
 * 分压比 = 100/(68+100) = 0.595
 * 电池满电 4.2V → ADC = 4.2 * 0.595 = 2.50V = 2500mV
 * 电池截止 3.0V → ADC = 3.0 * 0.595 = 1.79V = 1790mV
 */
#define BATTERY_ADC_MAX_MV      2500    /* ADC 满量程输入电压 (mV) */
#define BATTERY_ADC_MIN_MV      1790    /* ADC 对应电池截止电压 (mV) */

/* ── 采样参数 ── */
#define BATTERY_SAMPLE_COUNT    500     /* 每次读取采样数 (取均值) */

/**
 * @brief 初始化电池 ADC 驱动
 *
 * 包括 ADC Oneshot 单元初始化、校准、通道配置。
 * 应在系统初始化时调用一次。
 *
 * @return ESP_OK 成功
 */
esp_err_t battery_init(void);

/**
 * @brief 读取电池电量百分比 (非阻塞, ~50ms)
 *
 * 采样 500 次取均值 → 毫伏转百分比 → 指数平滑.
 *
 * @param percent 输出: 0-100 (%)
 * @param voltage_mv 输出: 当前电压 (mV), 可为 NULL
 * @return ESP_OK 成功
 */
esp_err_t battery_read(int *percent, int *voltage_mv);

/**
 * @brief 快速获取上次读取的电量 (不触发 ADC 采样)
 *
 * 用于 UI 轮询, 返回最近一次 battery_read() 的结果。
 *
 * @return 0-100 (%)
 */
int battery_get_cached_percent(void);

#ifdef __cplusplus
}
#endif