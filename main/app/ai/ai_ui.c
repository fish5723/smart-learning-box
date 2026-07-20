/**
 * @file ai_ui.c
 * @brief AI老师 UI 实现 — LVGL 9.x
 *
 * 严格遵循 UI_DESIGN_SYSTEM.md 视觉规范
 */

#include "ai_ui.h"
#include "lvgl.h"
#include "esp_log.h"
#include "home.h"
#include "home_ui.h"
#include "app/font_loader/font_loader.h"
#include "app/icon_loader/icon_loader.h"
#include "app/achievement/achievement.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"



static const char *TAG = "AI_UI";

/* ── 颜色定义 ── */
#define COLOR_BG              lv_color_hex(0x0F172A)
#define COLOR_CARD            lv_color_hex(0x1E293B)
#define COLOR_BORDER          lv_color_hex(0x334155)
#define COLOR_PRIMARY         lv_color_hex(0x3B82F6)
#define COLOR_AI_ACCENT       lv_color_hex(0x2563EB)
#define COLOR_SUCCESS         lv_color_hex(0x10B981)
#define COLOR_TEXT_PRIMARY    lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_SECONDARY  lv_color_hex(0x94A3B8)
#define COLOR_AI_BUBBLE       lv_color_hex(0x334155)
#define COLOR_USER_BUBBLE     lv_color_hex(0x2563EB)

/* ── 间距 ── */
#define SPACING_SM  8
#define SPACING_MD  12
#define SPACING_LG  16

/* ── 布局常量 ── */
#define SCREEN_W        1024
#define SCREEN_H        600
#define HEADER_H        60
#define SUBJECT_H       70
#define INPUT_H         80
#define CONTAINER_PAD   16
#define ITEM_GAP        12

/* ── 全局对象 ── */
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_chat_list = NULL;
static lv_obj_t *s_input_area = NULL;
static lv_obj_t *s_input_box = NULL;
static lv_obj_t *s_keyboard = NULL;     /* 虚拟键盘 */

/* ── 当前流式消息的标签（SSE 流式期间持续追加）── */
static lv_obj_t *s_streaming_label = NULL;

/* ── 流式累积缓冲互斥锁 ── */
static SemaphoreHandle_t s_stream_mtx = NULL;

/* ── 当前选中学科 ── */
static int s_current_subject = 0;

/* ── 学科列表 ── */
static const char *s_subjects[] = {"数学", "英语", "语文", "物理", "化学"};
static lv_obj_t *s_subject_btns[5] = {NULL};  /* 5 个学科按钮句柄，用于 active 切换 */

/* ── 内部函数 ── */
static void create_header(lv_obj_t *parent);
static void create_subject_panel(lv_obj_t *parent);
static void create_chat_panel(lv_obj_t *parent);
static void create_input_panel(lv_obj_t *parent);
static void on_back_click(lv_event_t *e);
static void on_subject_click(lv_event_t *e);
static void on_send_click(lv_event_t *e);
static void on_input_focus(lv_event_t *e);
static void on_input_defocus(lv_event_t *e);

/* ═══════════════════════════════════════════════
   页面创建
   ═══════════════════════════════════════════════ */

static void ai_ui_create_screen(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_pad_all(s_screen, CONTAINER_PAD, 0);
    lv_obj_set_style_pad_gap(s_screen, ITEM_GAP, 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_header(s_screen);
    create_subject_panel(s_screen);
    create_chat_panel(s_screen);
    create_input_panel(s_screen);
}

void ai_ui_init(void)
{
    ESP_LOGI(TAG, "ai_ui_init() — deferred");
    if (!s_stream_mtx) s_stream_mtx = xSemaphoreCreateMutex();  /* 流式累积缓冲互斥 */
    /* 屏幕延迟到 ai_ui_show() 时创建 */
}

void ai_ui_show(void)
{
    if (!s_screen) {
        ai_ui_create_screen();
    }
    if (s_screen) {
        lv_screen_load(s_screen);
    }
}

void ai_ui_hide(void)
{
    /* 返回首页 */
}

/* ═══════════════════════════════════════════════
   顶部导航栏（Height=60, Radius=16）
   ═══════════════════════════════════════════════ */

static void create_header(lv_obj_t *parent)
{
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, LV_PCT(100), HEADER_H);
    lv_obj_set_style_bg_color(header, COLOR_CARD, 0);
    lv_obj_set_style_radius(header, 16, 0);
    lv_obj_set_style_pad_hor(header, 20, 0);
    lv_obj_set_style_pad_ver(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    /* 返回按钮 */
    lv_obj_t *back_btn = lv_obj_create(header);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_set_style_bg_color(back_btn, COLOR_BORDER, 0);
    lv_obj_set_style_radius(back_btn, 10, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back_btn, on_back_click, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(back_icon, &lv_font_montserrat_14, 0);
    lv_obj_align(back_icon, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "返回");
    lv_obj_set_style_text_color(back_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(back_label, g_font_cjk_14, 0);
    lv_obj_align(back_label, LV_ALIGN_CENTER, 5, 0);

    /* 标题 — 24px Bold 匹配 HTML font-size:24px */
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "AI老师");
    lv_obj_set_style_text_color(title, lv_color_hex(0x60A5FA), 0);
    lv_obj_set_style_text_font(title, g_font_cjk_24, 0);

    /* 在线状态 — 实心圆点匹配 HTML "● 在线" */
    lv_obj_t *online_area = lv_obj_create(header);
    lv_obj_set_size(online_area, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(online_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(online_area, 0, 0);
    lv_obj_set_flex_flow(online_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(online_area, 4, 0);
    lv_obj_clear_flag(online_area, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *online_icon = lv_label_create(online_area);
    lv_label_set_text(online_icon, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(online_icon, COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(online_icon, &lv_font_montserrat_14, 0);


    lv_obj_t *online = lv_label_create(online_area);
    lv_label_set_text(online, "在线");
    lv_obj_set_style_text_color(online, COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(online, g_font_cjk_14, 0);
}

/* ═══════════════════════════════════════════════
   学科快捷入口（Height=70, Radius=16）
   ═══════════════════════════════════════════════ */

static void create_subject_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, LV_PCT(100), SUBJECT_H);
    lv_obj_set_style_bg_color(panel, COLOR_CARD, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(panel,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(panel, 14, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 5; i++) {
        lv_obj_t *btn = lv_obj_create(panel);
        lv_obj_set_size(btn, 120, 42);
        lv_obj_set_style_bg_color(btn,
            (i == 0) ? COLOR_AI_ACCENT : COLOR_BORDER, 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, on_subject_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, s_subjects[i]);
        lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(label, g_font_cjk_16, 0);
        lv_obj_center(label);

        s_subject_btns[i] = btn;  /* 保存句柄供 active 切换 */
    }
}

/* ═══════════════════════════════════════════════
   聊天区域（flex:1, Radius=18）
   ═══════════════════════════════════════════════ */

static void create_chat_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, LV_PCT(100), 0);
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_style_bg_color(panel, COLOR_CARD, 0);
    lv_obj_set_style_radius(panel, 18, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(panel, 18, 0);
    /* 不禁止滚动 — 消息溢出时自动出现滚动条 */

    s_chat_list = panel;

    /* 初始欢迎消息 */
    ai_ui_add_ai_message("你好,我是智趣宝盒AI老师\n有什么不会的问题都可以问我哦!");
}

/* ═══════════════════════════════════════════════
   添加消息气泡
   ═══════════════════════════════════════════════ */

/* ── 内部：创建 AI 消息行，返回气泡内的 label ── */
static lv_obj_t *create_ai_message_row(void)
{
    if (!s_chat_list) return NULL;

    lv_obj_t *row = lv_obj_create(s_chat_list);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(row, 12, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* AI 头像 */
    lv_obj_t *avatar = lv_obj_create(row);
    lv_obj_set_size(avatar, 42, 42);
    lv_obj_set_style_bg_color(avatar, COLOR_AI_ACCENT, 0);
    lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(avatar, 0, 0);
    lv_obj_clear_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);

    /* AI 头像内部：机器人 PNG（居中于蓝色圆底；SD 不可用回退符号） */
    lv_obj_t *avatar_icon = icon_loader_create_image(avatar, ICON_ROBOT, 30, 30);
    lv_obj_center(avatar_icon);

    /* AI 气泡 */
    lv_obj_t *bubble = lv_obj_create(row);
    lv_obj_set_width(bubble, LV_PCT(70));
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(bubble, COLOR_AI_BUBBLE, 0);
    lv_obj_set_style_radius(bubble, 16, 0);
    lv_obj_set_style_pad_all(bubble, 14, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(bubble);
    lv_obj_set_width(label, LV_PCT(100));   /* 撑满气泡宽度，触发换行 */
    lv_obj_set_height(label, LV_SIZE_CONTENT); /* 高度随内容自动增长 */
    lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, g_font_cjk_16, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);

    return label;
}

void ai_ui_add_ai_message(const char *text)
{
    lv_obj_t *label = create_ai_message_row();
    if (!label) return;

    lv_label_set_text(label, text);
    s_streaming_label = NULL;

    /* 新消息后自动滚到底部 */
    lv_obj_update_layout(s_chat_list);
    lv_obj_scroll_to_y(s_chat_list, LV_COORD_MAX, LV_ANIM_OFF);
}

void ai_ui_append_ai_chunk(const char *chunk, bool is_end)
{
    if (!s_chat_list) return;

    /* 首 chunk：创建 AI 气泡，显示第一段文字 */
    if (!s_streaming_label) {
        if (chunk && chunk[0] != '\0') {
            s_streaming_label = create_ai_message_row();
            if (!s_streaming_label) return;
            lv_label_set_text(s_streaming_label, chunk);
        }
    }
    /* 后续 chunk：获取旧文本 → 拼接 → 写回 label（打字机效果） */
    else if (chunk && chunk[0] != '\0') {
        const char *old = lv_label_get_text(s_streaming_label);
        size_t old_len = old ? strlen(old) : 0;
        size_t chunk_len = strlen(chunk);
        char *merged = malloc(old_len + chunk_len + 1);
        if (merged) {
            if (old_len) memcpy(merged, old, old_len);
            memcpy(merged + old_len, chunk, chunk_len);
            merged[old_len + chunk_len] = '\0';
            lv_label_set_text(s_streaming_label, merged);
            free(merged);
        }
    }

    /* is_end 时做一次精确布局；流式期间只做轻量滚动 */
    if (is_end) {
        lv_obj_update_layout(s_chat_list);
    }
    lv_obj_scroll_to_y(s_chat_list, LV_COORD_MAX, LV_ANIM_OFF);
}

void ai_ui_add_user_message(const char *text)
{
    if (!s_chat_list) return;

    /* 关闭当前流式气泡（如有），用户发新消息意味着上轮对话结束 */
    s_streaming_label = NULL;

    lv_obj_t *row = lv_obj_create(s_chat_list);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
        LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* 用户气泡 */
    lv_obj_t *bubble = lv_obj_create(row);
    lv_obj_set_width(bubble, LV_PCT(70));
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(bubble, COLOR_USER_BUBBLE, 0);
    lv_obj_set_style_radius(bubble, 16, 0);
    lv_obj_set_style_pad_all(bubble, 14, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(bubble);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, g_font_cjk_16, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);

    /* 新消息后自动滚到底部 */
    lv_obj_update_layout(s_chat_list);
    lv_obj_scroll_to_y(s_chat_list, LV_COORD_MAX, LV_ANIM_OFF);
}

/* ═══════════════════════════════════════════════
   输入区域（Height=80, Radius=16）
   ═══════════════════════════════════════════════ */

static void create_input_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, LV_PCT(100), INPUT_H);
    lv_obj_set_style_bg_color(panel, COLOR_CARD, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_hor(panel, 16, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(panel,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* 输入框 */
    s_input_box = lv_textarea_create(panel);
    lv_obj_set_size(s_input_box, 0, 50);
    lv_obj_set_flex_grow(s_input_box, 1);
    lv_obj_set_style_bg_color(s_input_box, COLOR_BORDER, 0);
    lv_obj_set_style_radius(s_input_box, 12, 0);
    lv_obj_set_style_pad_left(s_input_box, 16, 0);
    lv_obj_set_style_border_width(s_input_box, 0, 0);
    lv_textarea_set_placeholder_text(s_input_box, "输入你的问题...");
    lv_obj_set_style_text_font(s_input_box, g_font_cjk_14, 0);
    lv_obj_set_style_text_color(s_input_box, COLOR_TEXT_SECONDARY, 0);
    lv_textarea_set_one_line(s_input_box, true);
    lv_obj_add_event_cb(s_input_box, on_input_focus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_input_box, on_input_defocus, LV_EVENT_DEFOCUSED, NULL);

    /* 发送按钮 */
    lv_obj_t *send_btn = lv_obj_create(panel);
    lv_obj_set_size(send_btn, 120, 50);
    lv_obj_set_style_bg_color(send_btn, COLOR_AI_ACCENT, 0);
    lv_obj_set_style_radius(send_btn, 12, 0);
    lv_obj_set_style_border_width(send_btn, 0, 0);
    lv_obj_add_flag(send_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(send_btn, on_send_click, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(send_btn, LV_OBJ_FLAG_SCROLLABLE);
    /* 图标 + 文字横向居中布局 */
    lv_obj_set_flex_flow(send_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(send_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(send_btn, 6, 0);

    lv_obj_t *send_icon = icon_loader_create_image(send_btn, ICON_SEND, 22, 22);
    LV_UNUSED(send_icon);

    lv_obj_t *send_label = lv_label_create(send_btn);
    lv_label_set_text(send_label, "发送");
    lv_obj_set_style_text_color(send_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(send_label, g_font_cjk_20, 0);
}

/* ═══════════════════════════════════════════════
   事件处理
   ═══════════════════════════════════════════════ */

/* ── 显示虚拟键盘 ── */
static void show_keyboard(void)
{
    if (!s_keyboard) {
        s_keyboard = lv_keyboard_create(lv_layer_top());
        lv_obj_set_size(s_keyboard, LV_PCT(100), 220);
        lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_keyboard_set_textarea(s_keyboard, s_input_box);
        lv_obj_set_style_bg_color(s_keyboard, COLOR_CARD, 0);
    }
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

/* ── 隐藏虚拟键盘 ── */
static void hide_keyboard(void)
{
    if (s_keyboard) {
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_input_focus(lv_event_t *e)
{
    (void)e;
    show_keyboard();
}

static void on_input_defocus(lv_event_t *e)
{
    (void)e;
    hide_keyboard();
}

static void on_back_click(lv_event_t *e)
{
    ESP_LOGI(TAG, "Back to home");
    hide_keyboard();
    home_show();
}

static void on_subject_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    ESP_LOGI(TAG, "Subject selected: %d=%s", idx, s_subjects[idx]);
    s_current_subject = idx;

    /* 更新所有学科按钮 active 样式 */
    for (int i = 0; i < 5; i++) {
        if (!s_subject_btns[i]) continue;
        lv_obj_set_style_bg_color(s_subject_btns[i],
            (i == idx) ? COLOR_AI_ACCENT : COLOR_BORDER, 0);
    }
}

static void on_send_click(lv_event_t *e)
{
    (void)e;
    const char *text = lv_textarea_get_text(s_input_box);
    if (!text || strlen(text) == 0) return;

    ESP_LOGI(TAG, "Send: %s", text);

    /* 特殊命令: /test 触发网络测试 */
    if (strcmp(text, "/test") == 0) {
        ai_ui_add_user_message("/test");
        extern void ai_llm_test_network(void);
        ai_llm_test_network();
        lv_textarea_set_text(s_input_box, "");
        hide_keyboard();
        return;
    }

    ai_ui_add_user_message(text);

    /* 发送真实 LLM 请求（SSE 流式响应） */
    extern bool ai_llm_send_message(const char *message, const char *subject);
    bool sent = ai_llm_send_message(text, s_subjects[s_current_subject]);
    if (!sent) {
        ai_ui_add_ai_message_async("[系统] 消息发送失败，请检查网络或 API Key 配置");
    }

    lv_textarea_set_text(s_input_box, "");
    hide_keyboard();
}

/* 供 ai_llm.c 获取当前学科索引 */
int ai_llm_get_current_subject(void)
{
    return s_current_subject;
}

/* ═══════════════════════════════════════════════
   跨任务异步调度（线程安全）
   通过 lv_async_call 将 UI 操作投递到 LVGL 任务
   ═══════════════════════════════════════════════ */

/* ── 流式分片有序累积 ──
   问题: 每个分片单独 lv_async_call 会创建一次性 lv_timer, 而 lv_timer_create 插入链表
   头、lv_timer_handler 从头遍历 → 同一帧内堆积的多个分片被 LIFO(反序)处理 → 语序混乱。
   解决: 生产者(网络任务)按序累积到带锁缓冲, 单个 flush 回调在 LVGL 任务里按序排空。
   即使 flush 定时器仍 LIFO 触发, 排空的是顺序正确的缓冲, 顺序即保住。 */
#define AI_STREAM_BUF_SZ  4096
static char              s_stream_buf[AI_STREAM_BUF_SZ];
static size_t            s_stream_len     = 0;
static bool              s_stream_end     = false;
static bool              s_flush_pending  = false;

static void _flush_stream_cb(void *unused)
{
    (void)unused;
    if (!s_stream_mtx) return;
    xSemaphoreTake(s_stream_mtx, portMAX_DELAY);
    s_flush_pending = false;
    if (s_stream_len > 0) {
        s_stream_buf[s_stream_len] = '\0';
        ai_ui_append_ai_chunk(s_stream_buf, false);   /* 已在 LVGL 任务上下文 */
        s_stream_len = 0;
    }
    bool end = s_stream_end;
    s_stream_end = false;
    xSemaphoreGive(s_stream_mtx);
    if (end) ai_ui_append_ai_chunk("", true);         /* 收尾, 锁外调用 */
}

void ai_ui_append_ai_chunk_async(const char *chunk, bool is_end)
{
    if (!chunk || !s_stream_mtx) return;

    xSemaphoreTake(s_stream_mtx, portMAX_DELAY);
    size_t clen = strlen(chunk);
    if (clen > 0 && s_stream_len + clen < AI_STREAM_BUF_SZ) {
        memcpy(s_stream_buf + s_stream_len, chunk, clen);
        s_stream_len += clen;
    } else if (clen > 0) {
        ESP_LOGW(TAG, "stream buf full (%d+%d), chunk dropped", (int)s_stream_len, (int)clen);
    }
    if (is_end) s_stream_end = true;
    bool need_post = !s_flush_pending;
    s_flush_pending = true;   /* 合并多次投递为一次 flush, 减少 timer 数量 */
    xSemaphoreGive(s_stream_mtx);

    if (need_post) {
        if (lv_async_call(_flush_stream_cb, NULL) != LV_RESULT_OK) {
            ESP_LOGE(TAG, "async: lv_async_call(flush) failed");
            xSemaphoreTake(s_stream_mtx, portMAX_DELAY);
            s_flush_pending = false;   /* 让下次分片可再次尝试投递 */
            xSemaphoreGive(s_stream_mtx);
        }
    }
}

static void _add_ai_message_cb(void *user_data)
{
    char *text = (char *)user_data;
    ai_ui_add_ai_message(text);
    free(text);
}

void ai_ui_add_ai_message_async(const char *text)
{
    if (!text) return;
    char *copy = strdup(text);
    if (!copy) return;
    lv_async_call(_add_ai_message_cb, copy);
}

/* ═══ 成就接线: AI 问答成功 +经验 ═══
 * ai_llm.c 的 llm_task 线程在 HTTP 200 正常结束后调用本函数,
 * 经 lv_async_call 投递到 LVGL 线程执行 achievement_complete_task,
 * 保证所有计分统一在 GUI 线程串行, 无数据竞争。 */
static void _reward_ai_chat_cb(void *unused)
{
    (void)unused;
    achievement_complete_task(ACHV_TASK_AI_CHAT, 1);
}

void ai_ui_reward_ai_chat(void)
{
    lv_async_call(_reward_ai_chat_cb, NULL);
}