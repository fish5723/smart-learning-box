/**
 * @file battery.c
 * @brief 电池电量检测 — ADC Oneshot 模式实现
 *
 * 参考: E:\IOT_competition\battery_adc_test\main\battery_adc_test.c
 *
 * 算法:
 *   1. ADC1_CH8 (GPIO53) 采集 500 次 → 均值 raw
 *   2. adc_cali_raw_to_voltage() → 毫伏 (校准可用时)
 *      否则 raw * 2500 / 4095 近似换算
 *   3. 毫伏 → 百分比: (mV - 2250) * 100 / (2500 - 2250)
 *   4. 指数移动平均 (smooth = (old + new) / 2)
 *   5. Clamp [0, 100]
 */

#include "battery.h"

#include "esp_log.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "BATTERY";

/* ── 内部状态 ── */
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static bool                      s_cali_available = false;
static bool                      s_initialized = false;

/* 缓存上次读取结果 */
static int s_cached_percent = -1;
static int s_cached_mv = 0;

/* ═══════════════════════════════════════════════
   校准初始化 (先 Curve Fitting, 回退 Line Fitting)
   ═══════════════════════════════════════════════ */

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t chan,
                                  adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    esp_err_t ret = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id  = unit,
        .chan     = chan,
        .atten    = atten,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, out_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration: curve fitting OK");
        return true;
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t line_config = {
        .unit_id  = unit,
        .atten    = atten,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    ret = adc_cali_create_scheme_line_fitting(&line_config, out_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration: line fitting OK");
        return true;
    }
#endif

    ESP_LOGW(TAG, "Calibration not available (eFuse not burnt), using raw→mV approx");
    return false;
}

/* ═══════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════ */

esp_err_t battery_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing battery ADC (ADC1_CH8 GPIO53)...");

    /* 1. 创建 ADC Oneshot 单元 */
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. 校准 */
    s_cali_available = adc_calibration_init(
        BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL,
        BATTERY_ADC_ATTEN, &s_cali_handle);

    /* 3. 通道配置 */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    ret = adc_oneshot_config_channel(s_adc_handle, BATTERY_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "config_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Battery ADC initialized (atten=12dB, samples=%d, cal=%s)",
             BATTERY_SAMPLE_COUNT, s_cali_available ? "yes" : "no");
    return ESP_OK;
}

esp_err_t battery_read(int *percent, int *voltage_mv)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* ── 500 次采样取均值 ── */
    int raw_sum = 0;
    for (int i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
        int raw = 0;
        esp_err_t ret = adc_oneshot_read(s_adc_handle, BATTERY_ADC_CHANNEL, &raw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "adc_oneshot_read failed: %s", esp_err_to_name(ret));
            return ret;
        }
        raw_sum += raw;
    }
    int raw_avg = raw_sum / BATTERY_SAMPLE_COUNT;

    /* ── Raw → 毫伏 ── */
    int mv = 0;
    if (s_cali_available) {
        esp_err_t ret = adc_cali_raw_to_voltage(s_cali_handle, raw_avg, &mv);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Calibration conversion failed, falling back to approx");
            mv = raw_avg * 2500 / 4095;
        }
    } else {
        /* 无校准: 线性近似 raw 12-bit → mV (12dB ≈ 0-2500mV) */
        mv = raw_avg * 2500 / 4095;
    }

    s_cached_mv = mv;

    /* ── 毫伏 → 百分比 ──
     * ADC 测量的是分压后的电压，需要根据分压比还原电池电压
     * 但实际只需用 ADC 电压范围映射到百分比即可
     * 分压比 = 100/(68+100) = 0.595
     */
    int mv_offset = mv - BATTERY_ADC_MIN_MV;
    if (mv_offset < 0) mv_offset = 0;
    int adc_range = BATTERY_ADC_MAX_MV - BATTERY_ADC_MIN_MV;
    int pct = mv_offset * 100 / adc_range;
    if (pct > 100) pct = 100;

    /* 指数移动平均 */
    if (s_cached_percent < 0) {
        s_cached_percent = pct;
    } else {
        s_cached_percent = (s_cached_percent + pct) / 2;
    }
    if (s_cached_percent > 100) s_cached_percent = 100;

    if (percent)    *percent = s_cached_percent;
    if (voltage_mv) *voltage_mv = mv;

    ESP_LOGI(TAG, "raw=%d → %dmV → %d%% (cached=%d%%)",
             raw_avg, mv, pct, s_cached_percent);

    return ESP_OK;
}

int battery_get_cached_percent(void)
{
    return s_cached_percent;  /* -1 = 未读取, >=0 有效 */
}