/**
 * @file wifi_ui.c
 * @brief WiFi 设置页面 — LVGL 9.x 实现
 *
 * 参考: 从机 esp_hosted_rpc.proto 事件 ID 定义
 *       slave_wifi_std.c L2182 event_handler_wifi() 事件类型
 *
 * 页面布局 (1024×600, Dark Theme):
 *   ┌─ Header: [← 返回]  WiFi 设置  [WiFi图标] ─┐
 *   ├─ 状态卡片: 当前网络 / 状态 / SSID / IP / RSSI ─┤
 *   ├─ WiFi 列表 (可滚动, flex:1)                │
 *   └─ Bottom: [🔄 重新扫描]                      ┘
 */

#include "wifi_ui.h"
#include "wifi.h"
#include "bsp/storage/storage.h"
#include "home.h"
#include "home_ui.h"
#include "app/font_loader/font_loader.h"
#include "esp_log.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "WIFI_UI";

/* ── 颜色 ── */
#define COLOR_BG              lv_color_hex(0x0F172A)
#define COLOR_CARD            lv_color_hex(0x1E293B)
#define COLOR_BORDER          lv_color_hex(0x334155)
#define COLOR_PRIMARY         lv_color_hex(0x3B82F6)
#define COLOR_PRIMARY_DARK    lv_color_hex(0x2563EB)
#define COLOR_SUCCESS         lv_color_hex(0x10B981)
#define COLOR_WARNING         lv_color_hex(0xF59E0B)
#define COLOR_DANGER          lv_color_hex(0xEF4444)
#define COLOR_ACCENT_BLUE     lv_color_hex(0x60A5FA)
#define COLOR_TEXT_PRIMARY    lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_SECONDARY  lv_color_hex(0x94A3B8)
#define COLOR_TEXT_INFO       lv_color_hex(0xCBD5E1)
#define COLOR_BTN_SECONDARY   lv_color_hex(0x374151)

/* ── 尺寸 ── */
#define SCREEN_W      1024
#define SCREEN_H      600
#define HEADER_H      60
#define RADIUS_CARD   16
#define RADIUS_DIALOG 20
#define RADIUS_BUTTON 12
#define SPACING_XS    4
#define SPACING_SM    8
#define SPACING_MD    12
#define SPACING_LG    16
#define SPACING_XL    24
#define LIST_ITEM_H   72

/* ── 全局对象 ── */
static lv_obj_t *s_screen       = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_status_info  = NULL;
static lv_obj_t *s_wifi_list    = NULL;

/* ── 密码输入对话框 (独立屏幕) ── */
static lv_obj_t *s_pwd_screen   = NULL;
static lv_obj_t *s_pwd_ta       = NULL;
static lv_obj_t *s_pwd_ssid     = NULL;   /* 目标 SSID */
static lv_obj_t *s_pwd_kb       = NULL;   /* 虚拟键盘 */
static lv_obj_t *s_pwd_save_cb  = NULL;   /* 保存密码复选框 */

/* ═══════════════════════════════════════════════
   辅助函数
   ═══════════════════════════════════════════════ */

static const char *rssi_text(int8_t rssi)
{
    if (rssi >= -50) return "优秀";
    if (rssi >= -65) return "良好";
    if (rssi >= -75) return "一般";
    return "较弱";
}

/**
 * @brief 认证类型 → 中文标签
 * 参考: 从机 slave_wifi_std.c L1380 wifi_ap_record_t.authmode 字段
 */
static const char *authmode_text(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN:            return "开放";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
    case WIFI_AUTH_WAPI_PSK:        return "WAPI";
    default:                        return "未知";
    }
}

/* ═══════════════════════════════════════════════
   WiFi 事件回调 (BSP → UI)
   在 esp_event 任务中执行 → 通过 lv_async_call 转发到 LVGL 线程
   ═══════════════════════════════════════════════ */

static void on_wifi_event(wifi_cb_event_t event, void *arg)
{
    LV_UNUSED(arg);

    switch (event) {
    case WIFI_CB_SCAN_DONE:
        lv_async_call((lv_async_cb_t)wifi_ui_update_scan_list, NULL);
        break;
    case WIFI_CB_CONNECTED:
    case WIFI_CB_DISCONNECTED:
    case WIFI_CB_GOT_IP:
    case WIFI_CB_LOST_IP:
        lv_async_call((lv_async_cb_t)wifi_ui_update_status, NULL);
        lv_async_call((lv_async_cb_t)wifi_ui_update_scan_list, NULL);
        break;
    case WIFI_CB_STATE_CHANGED:
        lv_async_call((lv_async_cb_t)wifi_ui_update_status, NULL);
        break;
    }
}

/* ═══════════════════════════════════════════════
   事件回调 (UI 按钮)
   ═══════════════════════════════════════════════ */

static void on_back_click(lv_event_t *e)
{
    LV_UNUSED(e);
    wifi_ui_hide();
}

static void on_scan_click(lv_event_t *e)
{
    LV_UNUSED(e);
    ESP_LOGI(TAG, "Scan triggered from UI");
    wifi_scan();
}

static void on_disconnect_click(lv_event_t *e)
{
    LV_UNUSED(e);
    ESP_LOGI(TAG, "Disconnect button clicked");
    wifi_disconnect();
}

/* 开放网络直接连接; 加密网络弹出密码框 */
static void on_connect_click(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);

    if (idx < 0 || idx >= g_wifi_scan_count) return;

    const char *ssid = g_wifi_scan_list[idx].ssid;
    wifi_auth_mode_t auth = g_wifi_scan_list[idx].authmode;

    ESP_LOGI(TAG, "Connect to \"%s\" (auth=%d)", ssid, auth);

    if (auth != WIFI_AUTH_OPEN) {
        /* 跳转到密码输入屏幕 */
        if (s_pwd_screen) {
            lv_label_set_text(s_pwd_ssid, ssid);
            lv_textarea_set_text(s_pwd_ta, "");
            /* 重置"保存密码"复选框 */
            if (s_pwd_save_cb) {
                lv_obj_remove_state(s_pwd_save_cb, LV_STATE_CHECKED);
            }
            lv_screen_load(s_pwd_screen);
            lv_obj_add_state(s_pwd_ta, LV_STATE_FOCUSED);
        }
    } else {
        /* 开放网络直接连接 */
        wifi_connect(ssid, NULL);
        /* 保存凭证（无密码） */
        storage_save_wifi_cred(ssid, "");
    }
}

/* 密码输入确认 */
static void on_pwd_ok(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!s_pwd_ssid || !s_pwd_ta) return;

    const char *ssid = lv_label_get_text(s_pwd_ssid);
    const char *pwd  = lv_textarea_get_text(s_pwd_ta);

    if (!ssid || strlen(ssid) == 0) return;

    ESP_LOGI(TAG, "Connecting to \"%s\"...", ssid);

    /* 用户选择了"保存密码" → 写入 NVS */
    if (s_pwd_save_cb && lv_obj_has_state(s_pwd_save_cb, LV_STATE_CHECKED)) {
        storage_save_wifi_cred(ssid, pwd);
        ESP_LOGI(TAG, "Credentials saved to NVS");
    }

    wifi_connect(ssid, pwd);

    /* 返回 WiFi 列表页面 */
    if (s_screen) {
        lv_screen_load(s_screen);
    }
}

/* 密码输入取消 */
static void on_pwd_cancel(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_screen) {
        lv_screen_load(s_screen);
    }
}

/* ═══════════════════════════════════════════════
   密码输入屏幕 (独立界面)
   ═══════════════════════════════════════════════ */
static void create_pwd_screen(void)
{
    s_pwd_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_pwd_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_pwd_screen, COLOR_BG, 0);
    lv_obj_set_flex_flow(s_pwd_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_pwd_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(s_pwd_screen, 60, 0);
    lv_obj_set_style_pad_gap(s_pwd_screen, SPACING_LG, 0);

    /* 标题 */
    lv_obj_t *title = lv_label_create(s_pwd_screen);
    lv_label_set_text(title, "输入WiFi密码");
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_24, 0);

    /* SSID 显示 */
    s_pwd_ssid = lv_label_create(s_pwd_screen);
    lv_label_set_text(s_pwd_ssid, "");
    lv_obj_set_style_text_color(s_pwd_ssid, COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_text_font(s_pwd_ssid, g_font_cjk_20, 0);

    /* 密码输入框容器 */
    lv_obj_t *input_container = lv_obj_create(s_pwd_screen);
    lv_obj_set_size(input_container, 500, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(input_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input_container, 0, 0);
    lv_obj_set_style_pad_all(input_container, SPACING_MD, 0);

    /* 密码输入框 */
    s_pwd_ta = lv_textarea_create(input_container);
    lv_obj_set_size(s_pwd_ta, 460, 56);
    lv_obj_set_style_bg_color(s_pwd_ta, lv_color_hex(0x1F2937), 0);
    lv_obj_set_style_radius(s_pwd_ta, RADIUS_BUTTON, 0);
    lv_obj_set_style_border_width(s_pwd_ta, 1, 0);
    lv_obj_set_style_border_color(s_pwd_ta, COLOR_BORDER, 0);
    lv_obj_set_style_text_color(s_pwd_ta, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(s_pwd_ta, g_font_cjk_16, 0);
    lv_obj_set_style_pad_hor(s_pwd_ta, SPACING_MD, 0);
    lv_textarea_set_password_mode(s_pwd_ta, true);
    lv_textarea_set_one_line(s_pwd_ta, true);
    lv_textarea_set_placeholder_text(s_pwd_ta, "请输入WiFi密码");
    lv_textarea_set_max_length(s_pwd_ta, 63);
    lv_obj_center(s_pwd_ta);

    /* "保存密码" 复选框 */
    s_pwd_save_cb = lv_checkbox_create(s_pwd_screen);
    lv_checkbox_set_text(s_pwd_save_cb, "保存密码 (下次自动连接)");
    lv_obj_set_style_text_color(s_pwd_save_cb, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_pwd_save_cb, g_font_cjk_14, 0);

    /* 虚拟键盘 */
    s_pwd_kb = lv_keyboard_create(s_pwd_screen);
    lv_obj_set_size(s_pwd_kb, LV_PCT(100), 220);
    lv_keyboard_set_textarea(s_pwd_kb, s_pwd_ta);
    lv_keyboard_set_mode(s_pwd_kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    /* 按钮行 */
    lv_obj_t *btn_row = lv_obj_create(s_pwd_screen);
    lv_obj_set_size(btn_row, 500, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(btn_row, SPACING_XL, 0);

    /* 取消按钮 */
    lv_obj_t *cancel = lv_btn_create(btn_row);
    lv_obj_set_size(cancel, 140, 48);
    lv_obj_set_style_bg_color(cancel, COLOR_BTN_SECONDARY, 0);
    lv_obj_set_style_radius(cancel, RADIUS_BUTTON, 0);
    lv_obj_add_event_cb(cancel, on_pwd_cancel, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, "取消");
    lv_obj_set_style_text_color(cancel_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(cancel_label, g_font_cjk_16, 0);
    lv_obj_center(cancel_label);

    /* 连接按钮 */
    lv_obj_t *ok = lv_btn_create(btn_row);
    lv_obj_set_size(ok, 140, 48);
    lv_obj_set_style_bg_color(ok, COLOR_PRIMARY, 0);
    lv_obj_set_style_radius(ok, RADIUS_BUTTON, 0);
    lv_obj_add_event_cb(ok, on_pwd_ok, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ok_label = lv_label_create(ok);
    lv_label_set_text(ok_label, "连接");
    lv_obj_set_style_text_color(ok_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(ok_label, g_font_cjk_16, 0);
    lv_obj_center(ok_label);
}

/* ═══════════════════════════════════════════════
   页面构建
   ═══════════════════════════════════════════════ */

static void create_header(lv_obj_t *parent)
{
    lv_obj_t *h = lv_obj_create(parent);
    lv_obj_set_size(h, LV_PCT(100), HEADER_H);
    lv_obj_set_style_bg_color(h, COLOR_CARD, 0);
    lv_obj_set_style_radius(h, RADIUS_CARD, 0);
    lv_obj_set_style_pad_hor(h, SPACING_XL, 0);
    lv_obj_set_style_pad_ver(h, 0, 0);
    lv_obj_set_style_border_width(h, 0, 0);
    lv_obj_set_flex_flow(h, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(h, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);

    /* 返回按钮 100×40 */
    lv_obj_t *back_btn = lv_obj_create(h);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_set_style_bg_color(back_btn, COLOR_BORDER, 0);
    lv_obj_set_style_radius(back_btn, RADIUS_BUTTON, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back_btn, on_back_click, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(back_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(back_btn,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(back_btn, 4, 0);

    lv_obj_t *back_arrow = lv_label_create(back_btn);
    lv_label_set_text(back_arrow, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_arrow, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(back_arrow, &lv_font_montserrat_14, 0);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "返回");
    lv_obj_set_style_text_color(back_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(back_label, g_font_cjk_14, 0);

    /* 标题 */
    lv_obj_t *title = lv_label_create(h);
    lv_label_set_text(title, "WiFi 设置");
    lv_obj_set_style_text_color(title, COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_24, 0);

    /* WiFi 图标 */
    lv_obj_t *icon = lv_label_create(h);
    lv_label_set_text(icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(icon, COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
}

static void create_status_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_radius(card, RADIUS_DIALOG, 0);
    lv_obj_set_style_pad_all(card, SPACING_XL, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(card, SPACING_SM, 0);

    /* "当前网络" */
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "当前网络");
    lv_obj_set_style_text_color(title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_16, 0);

    /* 状态: "已连接" / "连接中..." / "未连接" */
    s_status_label = lv_label_create(card);
    lv_label_set_text(s_status_label, "未连接");
    lv_obj_set_style_text_color(s_status_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_status_label, g_font_cjk_24, 0);

    /* SSID + IP + RSSI + 断开原因 信息区 */
    s_status_info = lv_obj_create(card);
    lv_obj_set_size(s_status_info, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_status_info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_status_info, 0, 0);
    lv_obj_set_flex_flow(s_status_info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_status_info, 4, 0);
    lv_obj_clear_flag(s_status_info, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ssid = lv_label_create(s_status_info);
    lv_label_set_text(ssid, "--");
    lv_obj_set_style_text_color(ssid, COLOR_TEXT_INFO, 0);
    lv_obj_set_style_text_font(ssid, g_font_cjk_14, 0);

    lv_obj_t *ip = lv_label_create(s_status_info);
    lv_label_set_text(ip, "IP: --");
    lv_obj_set_style_text_color(ip, COLOR_TEXT_INFO, 0);
    lv_obj_set_style_text_font(ip, g_font_cjk_14, 0);

    /* 断开连接按钮 */
    lv_obj_t *btn = lv_btn_create(card);
    lv_obj_set_size(btn, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(btn, COLOR_PRIMARY_DARK, 0);
    lv_obj_set_style_radius(btn, RADIUS_BUTTON, 0);
    lv_obj_add_event_cb(btn, on_disconnect_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "断开连接");
    lv_obj_set_style_text_color(btn_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(btn_label, g_font_cjk_16, 0);
    lv_obj_center(btn_label);
}

static void create_list_container(lv_obj_t *parent)
{
    s_wifi_list = lv_obj_create(parent);
    lv_obj_set_size(s_wifi_list, LV_PCT(100), 0);
    lv_obj_set_flex_grow(s_wifi_list, 1);
    lv_obj_set_style_bg_color(s_wifi_list, COLOR_CARD, 0);
    lv_obj_set_style_radius(s_wifi_list, RADIUS_DIALOG, 0);
    lv_obj_set_style_pad_all(s_wifi_list, 0, 0);
    lv_obj_set_style_border_width(s_wifi_list, 0, 0);
    lv_obj_set_flex_flow(s_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_wifi_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_wifi_list, LV_SCROLLBAR_MODE_AUTO);

    /* 占位 */
    lv_obj_t *ph = lv_label_create(s_wifi_list);
    lv_label_set_text(ph, "点击扫描搜索网络");
    lv_obj_set_style_text_color(ph, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(ph, g_font_cjk_16, 0);
    lv_obj_set_style_pad_all(ph, SPACING_XL, 0);
}

static void create_bottom_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* 重新扫描 */
    lv_obj_t *scan_btn = lv_btn_create(bar);
    lv_obj_set_size(scan_btn, 180, 52);
    lv_obj_set_style_bg_color(scan_btn, COLOR_PRIMARY_DARK, 0);
    lv_obj_set_style_radius(scan_btn, 14, 0);
    lv_obj_add_event_cb(scan_btn, on_scan_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, "🔄 重新扫描");
    lv_obj_set_style_text_color(scan_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(scan_label, g_font_cjk_20, 0);
    lv_obj_center(scan_label);
}

/* ═══════════════════════════════════════════════
   列表项
   ═══════════════════════════════════════════════ */

static void populate_list(void)
{
    if (!s_wifi_list) return;
    lv_obj_clean(s_wifi_list);

    if (g_wifi_scan_count == 0) {
        lv_obj_t *empty = lv_label_create(s_wifi_list);
        lv_label_set_text(empty, "未发现可用网络");
        lv_obj_set_style_text_color(empty, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(empty, g_font_cjk_16, 0);
        lv_obj_set_style_pad_all(empty, SPACING_XL, 0);
        return;
    }

    for (int i = 0; i < g_wifi_scan_count; i++) {
        wifi_scan_item_t *ap = &g_wifi_scan_list[i];

        /* 行容器 */
        lv_obj_t *row = lv_obj_create(s_wifi_list);
        lv_obj_set_size(row, LV_PCT(100), LIST_ITEM_H);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, COLOR_BORDER, 0);
        lv_obj_set_style_pad_hor(row, SPACING_XL, 0);
        lv_obj_set_style_pad_ver(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* 左侧: 图标 + SSID + 加密类型 + 信道 + 信号 */
        lv_obj_t *left = lv_obj_create(row);
        lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(left, 0, 0);
        lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(left, SPACING_LG, 0);
        lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

        /* WiFi 图标：开放/加密 */
        lv_obj_t *icon = lv_label_create(left);
        lv_label_set_text(icon, (ap->authmode == WIFI_AUTH_OPEN) ? LV_SYMBOL_WIFI : LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(icon,
            (ap->authmode == WIFI_AUTH_OPEN) ? COLOR_ACCENT_BLUE : COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);

        /* SSID */
        lv_obj_t *ssid_label = lv_label_create(left);
        lv_label_set_text(ssid_label, ap->ssid);
        lv_obj_set_style_text_color(ssid_label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(ssid_label, g_font_cjk_20, 0);

        /* 加密类型标签 */
        const char *auth_str = authmode_text(ap->authmode);
        bool is_encrypted = (ap->authmode != WIFI_AUTH_OPEN);
        lv_obj_t *auth_label = lv_label_create(left);
        lv_label_set_text(auth_label, auth_str);
        lv_obj_set_style_text_color(auth_label, is_encrypted ? COLOR_WARNING : COLOR_SUCCESS, 0);
        lv_obj_set_style_text_font(auth_label, g_font_cjk_14, 0);

        /* 信道 + 信号强度 */
        char info[48];
        snprintf(info, sizeof(info), "CH.%u | 信号: %s", ap->channel, rssi_text(ap->rssi));
        lv_obj_t *info_label = lv_label_create(left);
        lv_label_set_text(info_label, info);
        lv_obj_set_style_text_color(info_label, COLOR_ACCENT_BLUE, 0);
        lv_obj_set_style_text_font(info_label, g_font_cjk_14, 0);

        /* 右侧: 按钮 */
        if (ap->connected) {
            lv_obj_t *badge = lv_label_create(row);
            lv_label_set_text(badge, "✓ 已连接");
            lv_obj_set_style_text_color(badge, COLOR_SUCCESS, 0);
            lv_obj_set_style_text_font(badge, g_font_cjk_14, 0);
        } else {
            lv_obj_t *btn = lv_btn_create(row);
            lv_obj_set_size(btn, 100, 42);
            lv_obj_set_style_bg_color(btn, COLOR_PRIMARY, 0);
            lv_obj_set_style_radius(btn, RADIUS_BUTTON, 0);
            lv_obj_set_user_data(btn, (void *)(intptr_t)i);
            lv_obj_add_event_cb(btn, on_connect_click, LV_EVENT_CLICKED, NULL);

            lv_obj_t *btn_label = lv_label_create(btn);
            lv_label_set_text(btn_label, "连接");
            lv_obj_set_style_text_color(btn_label, COLOR_TEXT_PRIMARY, 0);
            lv_obj_set_style_text_font(btn_label, g_font_cjk_16, 0);
            lv_obj_center(btn_label);
        }
    }
}

/* ═══════════════════════════════════════════════
   公共接口
   ═══════════════════════════════════════════════ */

static void wifi_ui_create_screen(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_pad_all(s_screen, SPACING_LG, 0);
    lv_obj_set_style_pad_gap(s_screen, SPACING_MD, 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_header(s_screen);
    create_status_card(s_screen);
    create_list_container(s_screen);
    create_bottom_bar(s_screen);
    create_pwd_screen();
}

void wifi_ui_init(void)
{
    ESP_LOGI(TAG, "wifi_ui_init() — deferred + register callback");

    /* 注册 BSP → UI 回调 */
    wifi_register_callback(on_wifi_event, NULL);
}

void wifi_ui_show(void)
{
    if (!s_screen) {
        wifi_ui_create_screen();
    }
    if (s_screen) {
        wifi_ui_update_status();
        wifi_ui_update_scan_list();
        lv_screen_load(s_screen);
    }
}

void wifi_ui_hide(void)
{
    home_show();
}

void wifi_ui_update_scan_list(void)
{
    ESP_LOGI(TAG, "update_scan_list: %d AP(s)", g_wifi_scan_count);
    populate_list();
}

void wifi_ui_update_status(void)
{
    if (!s_status_label || !s_status_info) return;

    wifi_state_t state = wifi_get_state();

    if (g_wifi_connected && strlen(g_wifi_current_ssid) > 0) {
        lv_label_set_text(s_status_label, "已连接");
        lv_obj_set_style_text_color(s_status_label, COLOR_SUCCESS, 0);

        lv_obj_clean(s_status_info);

        lv_obj_t *s = lv_label_create(s_status_info);
        lv_label_set_text(s, g_wifi_current_ssid);
        lv_obj_set_style_text_color(s, COLOR_TEXT_INFO, 0);
        lv_obj_set_style_text_font(s, g_font_cjk_14, 0);

        /* IP 地址 */
        char ip_buf[48];
        if (strlen(g_wifi_current_ip) > 0) {
            snprintf(ip_buf, sizeof(ip_buf), "IP: %s", g_wifi_current_ip);
        } else {
            snprintf(ip_buf, sizeof(ip_buf), "IP: 获取中...");
        }
        lv_obj_t *i = lv_label_create(s_status_info);
        lv_label_set_text(i, ip_buf);
        lv_obj_set_style_text_color(i, COLOR_TEXT_INFO, 0);
        lv_obj_set_style_text_font(i, g_font_cjk_14, 0);

        /* 信号强度 */
        char rssi_buf[32];
        snprintf(rssi_buf, sizeof(rssi_buf), "信号: %s (%d dBm)", rssi_text(g_wifi_current_rssi), g_wifi_current_rssi);
        lv_obj_t *r = lv_label_create(s_status_info);
        lv_label_set_text(r, rssi_buf);
        lv_obj_set_style_text_color(r, COLOR_TEXT_INFO, 0);
        lv_obj_set_style_text_font(r, g_font_cjk_14, 0);

    } else if (state == WIFI_STATE_CONNECTING) {
        lv_label_set_text(s_status_label, "连接中...");
        lv_obj_set_style_text_color(s_status_label, COLOR_ACCENT_BLUE, 0);

        lv_obj_clean(s_status_info);
        lv_obj_t *s = lv_label_create(s_status_info);
        lv_label_set_text(s, g_wifi_current_ssid);
        lv_obj_set_style_text_color(s, COLOR_TEXT_INFO, 0);
        lv_obj_set_style_text_font(s, g_font_cjk_14, 0);

        lv_obj_t *hint = lv_label_create(s_status_info);
        lv_label_set_text(hint, "正在获取IP地址...");
        lv_obj_set_style_text_color(hint, COLOR_TEXT_INFO, 0);
        lv_obj_set_style_text_font(hint, g_font_cjk_14, 0);

    } else if (state == WIFI_STATE_SCANNING) {
        lv_label_set_text(s_status_label, "扫描中...");
        lv_obj_set_style_text_color(s_status_label, COLOR_ACCENT_BLUE, 0);
        /* 保持原有信息区内容不变 */

    } else {
        /* DISCONNECTED or IDLE */
        const char *reason = wifi_get_disconnect_reason();

        if (reason) {
            char status_text[64];
            snprintf(status_text, sizeof(status_text), "已断开");
            lv_label_set_text(s_status_label, status_text);
            lv_obj_set_style_text_color(s_status_label, COLOR_DANGER, 0);

            lv_obj_clean(s_status_info);
            lv_obj_t *r = lv_label_create(s_status_info);
            lv_label_set_text(r, reason);
            lv_obj_set_style_text_color(r, COLOR_DANGER, 0);
            lv_obj_set_style_text_font(r, g_font_cjk_14, 0);
        } else {
            lv_label_set_text(s_status_label, "未连接");
            lv_obj_set_style_text_color(s_status_label, COLOR_TEXT_SECONDARY, 0);

            lv_obj_clean(s_status_info);
            lv_obj_t *hint = lv_label_create(s_status_info);
            lv_label_set_text(hint, "请选择网络连接");
            lv_obj_set_style_text_color(hint, COLOR_TEXT_INFO, 0);
            lv_obj_set_style_text_font(hint, g_font_cjk_14, 0);
        }
    }
}
