/**
 * @file boot_ui.c
 * @brief Boot Splash 页面实现 — LVGL 9.x
 *
 * 线程模型:
 *   - boot_ui_start()       → lv_async_call → LVGL task 创建对象
 *   - boot_ui_set_progress() → lv_async_call → LVGL task 更新 bar/label
 *   - boot_ui_finish()       → 直接操作对象 (调用方已持 esp_lv_adapter_lock)
 *
 * BOOT_TIMEOUT: 15s 超时自动强制退出。
 */

#include "boot_ui.h"
#include "lvgl.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "BOOT_UI";

/* ═══════════════════════════════════════════════
   颜色 (UI_DESIGN_SYSTEM.md §2)
   ═══════════════════════════════════════════════ */
#define COLOR_BG              lv_color_hex(0x0F172A)
#define COLOR_CARD            lv_color_hex(0x1E293B)
#define COLOR_PRIMARY         lv_color_hex(0x3B82F6)
#define COLOR_SUCCESS         lv_color_hex(0x10B981)
#define COLOR_WARNING         lv_color_hex(0xF59E0B)
#define COLOR_LOGO_BLUE       lv_color_hex(0x60A5FA)
#define COLOR_TEXT_SECONDARY  lv_color_hex(0x94A3B8)

/* ═══════════════════════════════════════════════
   布局
   ═══════════════════════════════════════════════ */
#define SCREEN_W        1024
#define SCREEN_H        600
#define BAR_W           300
#define BAR_H           8
#define BAR_RADIUS      8
#define CENTER_GAP      20

/* ═══════════════════════════════════════════════
   BOOT_TIMEOUT
   ═══════════════════════════════════════════════ */
#define BOOT_TIMEOUT_MS  15000

/* ═══════════════════════════════════════════════
   全局对象 (仅在 LVGL task 上下文访问)
   ═══════════════════════════════════════════════ */
static lv_obj_t  *s_screen   = NULL;
static lv_obj_t  *s_logo     = NULL;
static lv_obj_t  *s_subtitle = NULL;
static lv_obj_t  *s_bar      = NULL;
static lv_obj_t  *s_status   = NULL;
static boot_ui_on_exit_cb_t s_exit_cb = NULL;
static esp_timer_handle_t s_timeout_timer = NULL;
static bool s_finished = false;

/* ═══════════════════════════════════════════════
   lv_async_call 数据包
   ═══════════════════════════════════════════════ */
typedef struct {
    uint8_t percent;
    char status_text[48];
} boot_progress_pkt_t;

/* ═══════════════════════════════════════════════
   内部函数
   ═══════════════════════════════════════════════ */
static void deferred_boot_create(void *arg);
static void anim_opa_cb(lv_obj_t *obj, int32_t v);
static void deferred_progress_cb(void *arg);
static void boot_exit_timer_cb(lv_timer_t *timer);
static void boot_timeout_cb(void *arg);
static void deferred_timeout_finish(void *arg);

/* ── opacity 动画回调 ── */
static void anim_opa_cb(lv_obj_t *obj, int32_t v)
{
    lv_obj_set_style_opa(obj, (lv_opa_t)v, 0);
}

/* ── 真正创建 Boot Splash (LVGL task 上下文) ── */
static void deferred_boot_create(void *arg)
{
    LV_UNUSED(arg);

    ESP_LOGI(TAG, "create root...");

    /* ── 全屏背景 ── */
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    ESP_LOGI(TAG, "create root — done");

    /* ── 居中容器 ── */
    lv_obj_t *center = lv_obj_create(s_screen);
    lv_obj_set_size(center, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(center, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center, 0, 0);
    lv_obj_set_flex_flow(center, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(center,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(center, CENTER_GAP, 0);
    lv_obj_clear_flag(center, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(center);

    /* ── Logo: "智趣宝盒"
     * Boot stage — use LVGL built-in Montserrat (always available).
     * CJK glyphs fall back to LVGL default replacement char.            */
    ESP_LOGI(TAG, "create logo...");
    s_logo = lv_label_create(center);
    lv_label_set_text(s_logo, "SmartBox");
    lv_obj_set_style_text_color(s_logo, COLOR_LOGO_BLUE, 0);
    lv_obj_set_style_text_font(s_logo, &lv_font_montserrat_24, 0);
    lv_obj_set_style_opa(s_logo, LV_OPA_0, 0);
    ESP_LOGI(TAG, "create logo — done");

    /* ── 副标题 ── */
    ESP_LOGI(TAG, "create subtitle...");
    s_subtitle = lv_label_create(center);
    lv_label_set_text(s_subtitle, "AI Learning Terminal");
    lv_obj_set_style_text_color(s_subtitle, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_opa(s_subtitle, LV_OPA_0, 0);
    ESP_LOGI(TAG, "create subtitle — done");

    /* ── 进度条 ── */
    ESP_LOGI(TAG, "create bar...");
    s_bar = lv_bar_create(center);
    lv_obj_set_size(s_bar, BAR_W, BAR_H);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, COLOR_CARD, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, BAR_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, BAR_RADIUS, LV_PART_INDICATOR);
    lv_obj_set_style_opa(s_bar, LV_OPA_0, 0);
    ESP_LOGI(TAG, "create bar — done");

    /* ── 状态文字 ── */
    ESP_LOGI(TAG, "create status label...");
    s_status = lv_label_create(center);
    lv_label_set_text(s_status, "SYSTEM STARTING...");
    lv_obj_set_style_text_color(s_status, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_opa(s_status, LV_OPA_0, 0);
    ESP_LOGI(TAG, "create status label — done");

    /* ── 加载并强制刷新 ── */
    lv_screen_load(s_screen);
    lv_refr_now(NULL);
    ESP_LOGI(TAG, "create ready — screen loaded + refreshed");

    /* ═══════════════════════════════════════════════
       动画序列 (Fade ease_out, 在 LVGL task 内启动)
       ═══════════════════════════════════════════════ */
    lv_anim_t a;

    /* 1. Logo 淡入 500ms */
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_logo);
    lv_anim_set_values(&a, LV_OPA_0, LV_OPA_100);
    lv_anim_set_duration(&a, 500);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_opa_cb);
    lv_anim_start(&a);

    /* 2. 副标题淡入 delay 300ms */
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_subtitle);
    lv_anim_set_values(&a, LV_OPA_0, LV_OPA_100);
    lv_anim_set_duration(&a, 300);
    lv_anim_set_delay(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_opa_cb);
    lv_anim_start(&a);

    /* 3. 进度条 + 状态文字淡入 delay 600ms */
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_bar);
    lv_anim_set_values(&a, LV_OPA_0, LV_OPA_100);
    lv_anim_set_duration(&a, 300);
    lv_anim_set_delay(&a, 600);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_opa_cb);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_status);
    lv_anim_set_values(&a, LV_OPA_0, LV_OPA_100);
    lv_anim_set_duration(&a, 300);
    lv_anim_set_delay(&a, 600);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_opa_cb);
    lv_anim_start(&a);

    /* ── BOOT_TIMEOUT 看门狗 ── */
    const esp_timer_create_args_t timeout_args = {
        .callback = boot_timeout_cb,
        .arg = NULL,
        .name = "boot_wdg",
        .dispatch_method = ESP_TIMER_TASK,
    };
    if (esp_timer_create(&timeout_args, &s_timeout_timer) == ESP_OK) {
        esp_timer_start_once(s_timeout_timer, BOOT_TIMEOUT_MS * 1000);
        ESP_LOGI(TAG, "BOOT_TIMEOUT armed: %d ms", BOOT_TIMEOUT_MS);
    }

    ESP_LOGI(TAG, "boot splash fully created");
}

/* ── lv_async_call 进度更新 (LVGL task 上下文) ── */
static void deferred_progress_cb(void *arg)
{
    boot_progress_pkt_t *pkt = (boot_progress_pkt_t *)arg;
    if (!pkt) return;

    if (s_bar) {
        lv_bar_set_value(s_bar, pkt->percent, LV_ANIM_ON);
    }
    if (s_status && pkt->status_text[0] != '\0') {
        lv_label_set_text(s_status, pkt->status_text);
    }

    free(pkt);
}

/* ── 退出定时器回调 (LVGL task 上下文) ── */
static void boot_exit_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    ESP_LOGI(TAG, "exiting boot splash — deleting screen");

    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen   = NULL;
        s_logo     = NULL;
        s_subtitle = NULL;
        s_bar      = NULL;
        s_status   = NULL;
    }

    if (s_exit_cb) {
        s_exit_cb();
    }
}

/* ── BOOT_TIMEOUT (ESP_TIMER_TASK) ── */
static void boot_timeout_cb(void *arg)
{
    LV_UNUSED(arg);
    ESP_LOGW(TAG, "BOOT_TIMEOUT (%d ms) — forcing exit", BOOT_TIMEOUT_MS);
    lv_async_call(deferred_timeout_finish, NULL);
}

/* ── 超时强制退出 (LVGL task 上下文) ── */
static void deferred_timeout_finish(void *arg)
{
    LV_UNUSED(arg);
    if (s_finished) return;
    s_finished = true;

    if (s_bar) {
        lv_bar_set_value(s_bar, 100, LV_ANIM_ON);
    }
    if (s_status) {
        lv_label_set_text(s_status, "BOOT TIMEOUT");
        lv_obj_set_style_text_color(s_status, COLOR_WARNING, 0);
    }
    s_exit_cb = NULL;

    lv_timer_t *t = lv_timer_create(boot_exit_timer_cb, 1000, NULL);
    lv_timer_set_repeat_count(t, 1);
}

/* ═══════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════ */

void boot_ui_start(void)
{
    ESP_LOGI(TAG, "boot_ui_start() — deferring creation to LVGL task");
    s_finished = false;
    lv_async_call(deferred_boot_create, NULL);
}

void boot_ui_set_progress(uint8_t percent, const char *status_text)
{
    if (percent > 100) percent = 100;

    boot_progress_pkt_t *pkt = calloc(1, sizeof(boot_progress_pkt_t));
    if (!pkt) return;

    pkt->percent = percent;
    if (status_text) {
        strncpy(pkt->status_text, status_text, sizeof(pkt->status_text) - 1);
        pkt->status_text[sizeof(pkt->status_text) - 1] = '\0';
    }

    lv_async_call(deferred_progress_cb, pkt);
}

void boot_ui_finish(boot_ui_on_exit_cb_t on_exit)
{
    if (s_finished) {
        ESP_LOGW(TAG, "boot_ui_finish: already finished, ignoring");
        return;
    }
    s_finished = true;

    /* 取消 BOOT_TIMEOUT */
    if (s_timeout_timer) {
        esp_timer_stop(s_timeout_timer);
        esp_timer_delete(s_timeout_timer);
        s_timeout_timer = NULL;
        ESP_LOGI(TAG, "BOOT_TIMEOUT disarmed");
    }

    ESP_LOGI(TAG, "boot_ui_finish — SYSTEM READY");

    s_exit_cb = on_exit;

    /* finish 由 system_init 持锁调用，此处直接操作对象安全 */
    if (s_bar) {
        lv_bar_set_value(s_bar, 100, LV_ANIM_ON);
    }
    if (s_status) {
        lv_label_set_text(s_status, "SYSTEM READY");
        lv_obj_set_style_text_color(s_status, COLOR_SUCCESS, 0);
    }

    lv_timer_t *t = lv_timer_create(boot_exit_timer_cb, 500, NULL);
    lv_timer_set_repeat_count(t, 1);
}
