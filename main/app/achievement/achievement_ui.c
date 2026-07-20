/**
 * @file achievement_ui.c
 * @brief Achievement center UI - LVGL 9.x
 *
 * Based on Screen_Achievement.html prototype:
 *   - Level card with avatar + EXP progress bar
 *   - 4-stat grid (streak / problems / AI / hours)
 *   - Badge wall: 2-column grid, unlocked/locked states
 *
 * Strictly follows UI_DESIGN_SYSTEM.md:
 *   - Dark Theme (Background=#0F172A, Card=#1E293B)
 *   - Screen: 1024x600
 *   - Colors: Primary=#3B82F6, Success=#10B981
 *   - Radius: Card=16, Dialog=20, Button=12, Avatar=50%
 *   - Spacing: XS=4, SM=8, MD=12, LG=16, XL=24
 */

#include "achievement_ui.h"
#include "home_ui.h"
#include "home.h"
#include "app/font_loader/font_loader.h"
#include "app/icon_loader/icon_loader.h"
#include "bsp/time/sys_time.h"
#include "lvgl.h"
#include "esp_log.h"
#include "storage.h"

static const char *TAG = "ACHV_UI";

/* ═══════════════════════════════════════════════
   Colors (UI_DESIGN_SYSTEM.md S2)
   ═══════════════════════════════════════════════ */
#define COLOR_BG              lv_color_hex(0x0F172A)
#define COLOR_CARD            lv_color_hex(0x1E293B)
#define COLOR_BORDER          lv_color_hex(0x334155)
#define COLOR_PRIMARY         lv_color_hex(0x3B82F6)
#define COLOR_SUCCESS         lv_color_hex(0x10B981)
#define COLOR_TEXT_PRIMARY    lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_SECONDARY  lv_color_hex(0x94A3B8)
#define COLOR_TITLE_BLUE      lv_color_hex(0x60A5FA)
#define COLOR_STAT_BLUE       lv_color_hex(0x60A5FA)
#define COLOR_LEVEL_TEXT      lv_color_hex(0xE2E8F0)
#define COLOR_LOCKED          lv_color_hex(0x64748B)
#define COLOR_AVATAR_BG       lv_color_hex(0x2563EB)
#define COLOR_BADGE_BG        lv_color_hex(0x334155)

/* ═══════════════════════════════════════════════
   Spacing (UI_DESIGN_SYSTEM.md S5)
   ═══════════════════════════════════════════════ */
#define SPACING_SM  8
#define SPACING_MD  12
#define SPACING_LG  16
#define SPACING_XL  24

/* ═══════════════════════════════════════════════
   Layout constants (1024x600)
   ═══════════════════════════════════════════════ */
#define SCREEN_W         1024
#define SCREEN_H         600
#define CONTAINER_PAD    16
#define ITEM_GAP         12
#define HEADER_H         60
#define LEVEL_CARD_H     120
#define STAT_CARD_H      95
#define BADGE_H          80
#define BADGE_GAP        12
#define BADGE_COLS       2
/* (1024 - 16*2 - 16*2 - 12) / 2 */
#define BADGE_W          474

/* ═══════════════════════════════════════════════
   Badge data
   ═══════════════════════════════════════════════ */
typedef struct {
    const char *title;
    const char *desc;
    bool unlocked;
} badge_t;

static const badge_t s_badges[] = {
    {"初出茅庐", "完成第一次学习任务", true},
    {"学习新星", "累计学习10小时",     true},
    {"AI探索者", "完成20次AI问答",     true},
    {"百题挑战", "完成100道题目",      false},
    {"学习大师", "达到Lv.10",          false},
    {"全能学霸", "完成全部挑战任务",    false},
};

#define BADGE_COUNT (sizeof(s_badges) / sizeof(s_badges[0]))

/* ═══════════════════════════════════════════════
   Slogan text (English punctuation)
   ═══════════════════════════════════════════════ */
static const char *s_slogan = "让学习像游戏一样有趣";

/* ═══════════════════════════════════════════════
   Achievement NVS keys
   ═══════════════════════════════════════════════ */
#define KEY_ACHV_LEVEL       "achv_level"
#define KEY_ACHV_EXP         "achv_exp"
#define KEY_ACHV_EXP_MAX     "achv_exp_max"
#define KEY_ACHV_STREAK      "achv_streak"
#define KEY_ACHV_QUESTIONS   "achv_questions"
#define KEY_ACHV_AI_CHATS    "achv_ai_chats"
#define KEY_ACHV_STUDY_HRS "achv_study_hrs"
#define KEY_ACHV_LAST_DATE "achv_last_date"

/* ═══════════════════════════════════════════════
   User data (loaded from NVS, with defaults)
   ═══════════════════════════════════════════════ */
static struct {
    int level;
    int exp;
    int exp_max;
    int streak;
    int questions;
    int ai_chats;
    int study_hours;
    uint32_t last_study_date;   /* YYYYMMDD, 上次学习日期 (0=从未) */
} s_user = {1, 0, 1000, 0, 0, 0, 0, 0};

/* ═══════════════════════════════════════════════
   Global objects
   ═══════════════════════════════════════════════ */
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_exp_bar_fg = NULL;
static lv_obj_t *s_exp_text = NULL;
static lv_obj_t *s_level_name = NULL;
static lv_obj_t *s_level_tag = NULL;
static lv_obj_t *s_avatar_icon = NULL;

/* ── UI 实时刷新追踪 ── */
static lv_obj_t *s_stat_value_labels[4] = {NULL};
static lv_obj_t *s_badge_widgets[BADGE_COUNT] = {NULL};
static lv_obj_t *s_badge_count_label = NULL;
static lv_obj_t *s_header_level_label = NULL;
static bool      s_prev_badge_unlocked[BADGE_COUNT] = {false};
static bool      s_badge_states_valid = false;   /* s_prev_badge_unlocked 是否已初始化 */

/* ═══════════════════════════════════════════════
   Internal function declarations
   ═══════════════════════════════════════════════ */
static void create_header(lv_obj_t *parent);
static void create_level_card(lv_obj_t *parent);
static void create_stats_grid(lv_obj_t *parent);
static void create_badge_panel(lv_obj_t *parent);
static lv_obj_t *create_stat_item(lv_obj_t *parent,
    const char *value, const char *label, int index);
static lv_obj_t *create_badge(lv_obj_t *parent,
    const char *title, const char *desc, bool unlocked);
static void on_back_click(lv_event_t *e);
static void on_stat_click(lv_event_t *e);
static void on_badge_click(lv_event_t *e);
static void show_level_up_modal(void);
static void on_level_up_close(lv_event_t *e);
static void load_user_data_from_nvs(void);
static void save_user_data_to_nvs(void);
static void deferred_nvs_save_cb(void *arg)
{
    (void)arg;
    save_user_data_to_nvs();
}
static void save_user_data_to_nvs_deferred(void)
{
    lv_async_call(deferred_nvs_save_cb, NULL);
}
static const char *get_level_title(int level);
static bool check_level_up(void);
static void update_badge_unlock_status(void);
static void refresh_ui_in_place(void);
static void animate_exp_bar(int old_pct, int new_pct);
static void anim_exp_bar_cb_w(lv_obj_t *obj, int32_t v);
static void check_badge_unlocks(void);
static void show_badge_unlock_modal(int badge_index);
static void snapshot_badge_states(void);
static bool is_screen_active(void);
static bool is_badge_unlocked_runtime(int index);
static uint32_t get_today_date(void);

/* ═══════════════════════════════════════════════
   Page lifecycle
   ═══════════════════════════════════════════════ */

static void achievement_ui_create_screen(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_pad_all(s_screen, CONTAINER_PAD, 0);
    lv_obj_set_style_pad_gap(s_screen, ITEM_GAP, 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_header(s_screen);
    create_level_card(s_screen);
    create_stats_grid(s_screen);
    create_badge_panel(s_screen);
}

void achievement_ui_init(void)
{
    ESP_LOGI(TAG, "achievement_ui_init() — deferred");
    load_user_data_from_nvs();
}

void achievement_ui_show(void)
{
    /* 重新加载最新数据 */
    load_user_data_from_nvs();

    /* 清空 UI 追踪指针 */
    s_exp_bar_fg = NULL;
    s_exp_text = NULL;
    s_level_name = NULL;
    s_level_tag = NULL;
    s_avatar_icon = NULL;
    s_header_level_label = NULL;
    s_badge_count_label = NULL;
    for (int i = 0; i < 4; i++) s_stat_value_labels[i] = NULL;
    for (int i = 0; i < BADGE_COUNT; i++) s_badge_widgets[i] = NULL;

    /* 销毁旧页面，用最新数据重建 */
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    achievement_ui_create_screen();

    /* 记录当前徽章状态，用于后续新解锁检测 */
    snapshot_badge_states();

    if (s_screen) {
        lv_screen_load(s_screen);
    }
}

void achievement_ui_hide(void)
{
    /* return to home */
}

/* ═══════════════════════════════════════════════
   Header (Height=60, Radius=16)
   [Back]  [Growth Center]  [Lv.3]
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

    /* Back button */
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

    /* Title */
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "成长中心");
    lv_obj_set_style_text_color(title, COLOR_TITLE_BLUE, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_24, 0);

    /* Level tag */
    lv_obj_t *level_tag = lv_label_create(header);
    char tag_buf[32];
    lv_snprintf(tag_buf, sizeof(tag_buf), "Lv.%d %s", s_user.level, get_level_title(s_user.level));
    lv_label_set_text(level_tag, tag_buf);
    lv_obj_set_style_text_color(level_tag, COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(level_tag, g_font_cjk_16, 0);
    s_header_level_label = level_tag;
}

/* ═══════════════════════════════════════════════
   Level card (Height=120, Radius=20)
   [Avatar circle + name / slogan]  [EXP progress]
   ═══════════════════════════════════════════════ */

static void create_level_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LEVEL_CARD_H);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- Left: avatar + info ---- */
    lv_obj_t *left = lv_obj_create(card);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_gap(left, SPACING_LG, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    /* Avatar circle (72x72) */
    lv_obj_t *avatar = lv_obj_create(left);
    lv_obj_set_size(avatar, 72, 72);
    lv_obj_set_style_bg_color(avatar, COLOR_AVATAR_BG, 0);
    lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(avatar, 0, 0);
    lv_obj_clear_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);

    /* 等级卡头像：皇冠 PNG（居中于圆底；SD 不可用回退符号） */
    lv_obj_t *avatar_icon = icon_loader_create_image(avatar, ICON_CROWN, 48, 48);
    lv_obj_center(avatar_icon);

    /* Level name + slogan */
    lv_obj_t *info = lv_obj_create(left);
    lv_obj_set_size(info, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info, 0, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(info, 6, 0);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = lv_label_create(info);
    char name_buf[32];
    lv_snprintf(name_buf, sizeof(name_buf), "Lv.%d %s", s_user.level, get_level_title(s_user.level));
    lv_label_set_text(name, name_buf);
    lv_obj_set_style_text_color(name, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(name, g_font_cjk_24, 0);
    s_level_name = name;

    lv_obj_t *slogan = lv_label_create(info);
    lv_label_set_text(slogan, s_slogan);
    lv_obj_set_style_text_color(slogan, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(slogan, g_font_cjk_14, 0);

    /* ---- Right: EXP progress ---- */
    lv_obj_t *right = lv_obj_create(card);
    lv_obj_set_size(right, 340, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(right, 10, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    /* EXP text row */
    lv_obj_t *exp_row = lv_obj_create(right);
    lv_obj_set_size(exp_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(exp_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(exp_row, 0, 0);
    lv_obj_set_flex_flow(exp_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(exp_row,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(exp_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *exp_label = lv_label_create(exp_row);
    lv_label_set_text(exp_label, "经验值");
    lv_obj_set_style_text_color(exp_label, COLOR_LEVEL_TEXT, 0);
    lv_obj_set_style_text_font(exp_label, g_font_cjk_16, 0);

    s_exp_text = lv_label_create(exp_row);
    char exp_buf[32];
    lv_snprintf(exp_buf, sizeof(exp_buf), "%d / %d", s_user.exp, s_user.exp_max);
    lv_label_set_text(s_exp_text, exp_buf);
    lv_obj_set_style_text_color(s_exp_text, lv_color_hex(0xFBBF24), 0);
    lv_obj_set_style_text_font(s_exp_text, g_font_cjk_14, 0);

    /* Progress bar background */
    lv_obj_t *bar_bg = lv_obj_create(right);
    lv_obj_set_size(bar_bg, LV_PCT(100), 12);
    lv_obj_set_style_bg_color(bar_bg, COLOR_BORDER, 0);
    lv_obj_set_style_radius(bar_bg, 12, 0);
    lv_obj_set_style_border_width(bar_bg, 0, 0);
    lv_obj_set_style_pad_all(bar_bg, 0, 0);
    lv_obj_clear_flag(bar_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* Progress bar fill (animated from 0 to target) */
    s_exp_bar_fg = lv_obj_create(bar_bg);
    lv_obj_set_size(s_exp_bar_fg, 0, 12);
    lv_obj_set_style_bg_color(s_exp_bar_fg, COLOR_SUCCESS, 0);
    lv_obj_set_style_radius(s_exp_bar_fg, 12, 0);
    lv_obj_set_style_border_width(s_exp_bar_fg, 0, 0);
    lv_obj_set_style_pad_all(s_exp_bar_fg, 0, 0);
    lv_obj_align(s_exp_bar_fg, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(s_exp_bar_fg, LV_OBJ_FLAG_SCROLLABLE);

    /* Animate exp bar on first show */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_exp_bar_fg);
    lv_anim_set_values(&a, 0, (s_user.exp * 100) / s_user.exp_max);
    lv_anim_set_duration(&a, 1000);
    lv_anim_set_delay(&a, 300);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_exp_bar_cb_w);
    lv_anim_start(&a);
}

/* ═══════════════════════════════════════════════
   Stats grid (4 columns, Height=95)
   ═══════════════════════════════════════════════ */

static void create_stats_grid(lv_obj_t *parent)
{
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_set_size(grid, LV_PCT(100), STAT_CARD_H);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, ITEM_GAP, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    char val_buf[4][16];
    char detail_buf[4][64];

    lv_snprintf(val_buf[0], sizeof(val_buf[0]), "%d天", s_user.streak);
    lv_snprintf(detail_buf[0], sizeof(detail_buf[0]),
                "已连续学习 %d 天, 继续保持!", s_user.streak);

    lv_snprintf(val_buf[1], sizeof(val_buf[1]), "%d", s_user.questions);
    lv_snprintf(detail_buf[1], sizeof(detail_buf[1]),
                "累计完成 %d 道题目, 加油!", s_user.questions);

    lv_snprintf(val_buf[2], sizeof(val_buf[2]), "%d", s_user.ai_chats);
    lv_snprintf(detail_buf[2], sizeof(detail_buf[2]),
                "与 AI 老师对话 %d 次, 真棒!", s_user.ai_chats);

    lv_snprintf(val_buf[3], sizeof(val_buf[3]), "%dh", s_user.study_hours);
    lv_snprintf(detail_buf[3], sizeof(detail_buf[3]),
                "累计学习 %d 小时, 日积月累!", s_user.study_hours);

    struct { const char *val; const char *lbl; const char *detail; } stats[] = {
        {val_buf[0], "连续学习", detail_buf[0]},
        {val_buf[1], "完成题目", detail_buf[1]},
        {val_buf[2], "AI问答",   detail_buf[2]},
        {val_buf[3], "学习时长", detail_buf[3]},
    };

    for (int i = 0; i < 4; i++) {
        s_stat_value_labels[i] = NULL;  /* 重置 */
        lv_obj_t *item = create_stat_item(grid, stats[i].val, stats[i].lbl, i);
        lv_obj_set_flex_grow(item, 1);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(item, on_stat_click, LV_EVENT_CLICKED, (void *)stats[i].detail);
    }
}

static lv_obj_t *create_stat_item(lv_obj_t *parent,
    const char *value, const char *label, int index)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 0, LV_PCT(100));
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(card, SPACING_SM, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *val = lv_label_create(card);
    lv_label_set_text(val, value);
    lv_obj_set_style_text_color(val, COLOR_STAT_BLUE, 0);
    lv_obj_set_style_text_font(val, g_font_cjk_24, 0);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, g_font_cjk_14, 0);

    /* 存储引用以便实时刷新 */
    if (index >= 0 && index < 4) {
        s_stat_value_labels[index] = val;
    }

    return card;
}

/* ═══════════════════════════════════════════════
   Badge panel (flex_grow=1, scrollable, 2 columns)
   ═══════════════════════════════════════════════ */

static void create_badge_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, LV_PCT(100), 0);
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_style_bg_color(panel, COLOR_CARD, 0);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_pad_all(panel, SPACING_LG, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(panel, BADGE_GAP, 0);

    /* Section title with count */
    lv_obj_t *title_row = lv_obj_create(panel);
    lv_obj_set_size(title_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(title_row, SPACING_SM, 0);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(title_row);
    lv_label_set_text(title, "已获得成就");
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_20, 0);

    lv_obj_t *count_label = lv_label_create(title_row);
    int unlocked_count = 0;
    for (int i = 0; i < BADGE_COUNT; i++) {
        if (is_badge_unlocked_runtime(i)) unlocked_count++;
    }
    char count_buf[16];
    lv_snprintf(count_buf, sizeof(count_buf), "(%d/%d)", unlocked_count, BADGE_COUNT);
    lv_label_set_text(count_label, count_buf);
    lv_obj_set_style_text_color(count_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(count_label, g_font_cjk_14, 0);
    s_badge_count_label = count_label;

    /* Badge grid (2 columns, row wrap) */
    lv_obj_t *badge_grid = lv_obj_create(panel);
    lv_obj_set_size(badge_grid, LV_PCT(100), 0);
    lv_obj_set_flex_grow(badge_grid, 1);
    lv_obj_set_style_bg_opa(badge_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(badge_grid, 0, 0);
    lv_obj_set_style_pad_all(badge_grid, 0, 0);
    lv_obj_set_style_pad_gap(badge_grid, BADGE_GAP, 0);
    lv_obj_set_flex_flow(badge_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(badge_grid,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(badge_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(badge_grid, LV_DIR_VER);

    for (int i = 0; i < BADGE_COUNT; i++) {
        s_badge_widgets[i] = NULL;
        lv_obj_t *badge = create_badge(badge_grid,
            s_badges[i].title, s_badges[i].desc, is_badge_unlocked_runtime(i));
        lv_obj_set_width(badge, BADGE_W);
        s_badge_widgets[i] = badge;
    }
}

static lv_obj_t *create_badge(lv_obj_t *parent,
    const char *title, const char *desc, bool unlocked)
{
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_set_height(badge, BADGE_H);
    lv_obj_set_style_bg_color(badge, COLOR_BADGE_BG, 0);
    lv_obj_set_style_radius(badge, 14, 0);
    lv_obj_set_style_pad_all(badge, 14, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(badge,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(badge, 14, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    /* Left accent border */
    lv_obj_set_style_border_width(badge, 6, 0);
    lv_obj_set_style_border_side(badge, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(badge,
        unlocked ? COLOR_SUCCESS : COLOR_LOCKED, 0);

    /* Locked badges at reduced opacity */
    if (!unlocked) {
        lv_obj_set_style_opa(badge, LV_OPA_70, 0);
    }

    /* Badge icon */
    lv_obj_t *icon_circle = lv_obj_create(badge);
    lv_obj_set_size(icon_circle, 44, 44);
    lv_obj_set_style_bg_color(icon_circle,
        unlocked ? COLOR_SUCCESS : COLOR_LOCKED, 0);
    lv_obj_set_style_radius(icon_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(icon_circle, 0, 0);
    lv_obj_clear_flag(icon_circle, LV_OBJ_FLAG_SCROLLABLE);

    /* 徽章状态：已解锁→奖杯 PNG，未解锁→锁 PNG（居中于圆底） */
    lv_obj_t *icon = icon_loader_create_image(icon_circle,
        unlocked ? ICON_TROPHY : ICON_LOCK, 28, 28);
    lv_obj_center(icon);

    /* Badge info */
    lv_obj_t *info = lv_obj_create(badge);
    lv_obj_set_size(info, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info, 0, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(info, 6, 0);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = lv_label_create(info);
    lv_label_set_text(name, title);
    lv_obj_set_style_text_color(name, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(name, g_font_cjk_16, 0);

    lv_obj_t *desc_label = lv_label_create(info);
    lv_label_set_text(desc_label, desc);
    lv_obj_set_style_text_color(desc_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(desc_label, g_font_cjk_14, 0);

    return badge;
}

/* ═══════════════════════════════════════════════
   Events
   ═══════════════════════════════════════════════ */

/* ═══════════════════════════════════════════════
   Event handlers
   ═══════════════════════════════════════════════ */

static void on_back_click(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Back to home");
    home_show();
}

static void on_stat_click(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    const char *detail = (const char *)lv_event_get_user_data(e);
    if (detail) {
        ESP_LOGI(TAG, "Stat detail: %s", detail);
        /* TODO: show toast with detail */
    }
}

static void on_badge_click(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    lv_obj_t *badge = lv_event_get_target(e);
    /* TODO: show badge detail popup */
    ESP_LOGI(TAG, "Badge clicked");
}

static void on_level_up_close(lv_event_t *e)
{
    (void)e;
    /* Hide level up modal */
}

/* ═══════════════════════════════════════════════
   Level up modal (TODO: implement full modal)
   ═══════════════════════════════════════════════ */

static void show_level_up_modal(void)
{
    ESP_LOGI(TAG, "Level up! Lv.%d", s_user.level);
    /* TODO: create modal with bounce animation */
}

/* ═══════════════════════════════════════════════
   Level title helper
   ═══════════════════════════════════════════════ */

static const char *get_level_title(int level)
{
    if (level >= 20) return "学神";
    if (level >= 15) return "学霸";
    if (level >= 10) return "学习大师";
    if (level >= 7)  return "学习达人";
    if (level >= 5)  return "学习能手";
    if (level >= 3)  return "学习新星";
    return "初学者";
}

/* ═══════════════════════════════════════════════
   Level up logic
   ═══════════════════════════════════════════════ */

static bool check_level_up(void)
{
    bool leveled_up = false;
    while (s_user.exp >= s_user.exp_max) {
        s_user.exp -= s_user.exp_max;
        s_user.level++;
        s_user.exp_max = s_user.level * 1000;
        leveled_up = true;
        ESP_LOGI(TAG, "Level up! Now Lv.%d (exp=%d/%d)",
                 s_user.level, s_user.exp, s_user.exp_max);
        show_level_up_modal();
    }
    return leveled_up;
}

/* ═══════════════════════════════════════════════
   Badge unlock logic (dynamic based on s_user)
   ═══════════════════════════════════════════════ */

static void update_badge_unlock_status(void)
{
    /* Badge 0: 初出茅庐 — 完成第一次学习任务 (level >= 1 即默认解锁) */
    /* Badge 1: 学习新星 — 累计学习10小时 */
    /* Badge 2: AI探索者 — 完成20次AI问答 */
    /* Badge 3: 百题挑战 — 完成100道题目 */
    /* Badge 4: 学习大师 — 达到Lv.10 */
    /* Badge 5: 全能学霸 — 完成全部挑战任务 */

    /* 注意：s_badges 是 const 数组，实际项目中应改为可修改 */
    /* 此处通过运行时计算返回状态，不修改 const 数据 */
}

static bool is_badge_unlocked_runtime(int index)
{
    switch (index) {
        case 0: return s_user.level >= 1 || s_user.questions >= 1;
        case 1: return s_user.study_hours >= 10;
        case 2: return s_user.ai_chats >= 20;
        case 3: return s_user.questions >= 100;
        case 4: return s_user.level >= 10;
        case 5: return s_user.questions >= 100 && s_user.ai_chats >= 20 &&
                       s_user.study_hours >= 10 && s_user.level >= 10;
        default: return false;
    }
}

/* ═══════════════════════════════════════════════
   Daily streak — 基于真实日期判断连续学习
   ═══════════════════════════════════════════════ */

/**
 * @brief 获取当前日期，返回 YYYYMMDD 格式的 uint32_t
 *
 * 依赖 time_get_now() → localtime_r()，时间未同步时返回 0。
 */
static uint32_t get_today_date(void)
{
    time_t now = time_get_now();
    if (now < 86400) {
        /* 时间尚未同步 (Unix epoch < 1970-01-02) 或异常 */
        return 0;
    }

    struct tm tm_now = {0};
    localtime_r(&now, &tm_now);
    return (uint32_t)((tm_now.tm_year + 1900) * 10000
                    + (tm_now.tm_mon + 1) * 100
                    + tm_now.tm_mday);
}

/**
 * @brief 基于真实日期检查并更新连续学习天数
 *
 * 规则:
 *   - 时间未同步 (get_today_date() == 0): 跳过，保留原 streak
 *   - 首次学习 (last_study_date == 0): streak = 1
 *   - 昨天学过 (today == last + 1): streak++ 连续
 *   - 今天已签到 (today == last): 不重复计数
 *   - 中断 (today > last + 1): streak 重置为 1
 *
 * @return true 如果今天成功签到 (streak 发生了新增)
 */
bool achievement_check_daily_streak(void)
{
    uint32_t today = get_today_date();
    if (today == 0) {
        ESP_LOGW(TAG, "Daily streak: time not synced, skip");
        return false;
    }

    uint32_t last = s_user.last_study_date;

    if (last == 0) {
        /* 首次学习 */
        s_user.streak = 1;
        s_user.last_study_date = today;
        ESP_LOGI(TAG, "Daily streak: first study day! streak=%d", s_user.streak);
        save_user_data_to_nvs_deferred();
        return true;
    }

    if (today == last) {
        /* 今天已经签到过 */
        ESP_LOGD(TAG, "Daily streak: already checked in today");
        return false;
    }

    /* 计算日期差 */
    uint32_t yesterday = last + 1;
    /* 处理月末/年末跨越: 并非简单 +1，需要 tm 运算 */
    {
        struct tm tm_last = {0};
        time_t last_time = (time_t)0;
        /* 用计算的方式: 把 last 转成 time_t，加一天再转回 YYYYMMDD */
        /* 简便方法: 判断 today > last (必然), 再判断是否跨月 */
        /* 用 localtime 精确计算"昨天" */
        time_t now = time_get_now();
        time_t day_before = now - 86400;  /* 昨天 00:00 附近 */
        struct tm tm_yesterday = {0};
        localtime_r(&day_before, &tm_yesterday);
        uint32_t yesterday_real = (uint32_t)((tm_yesterday.tm_year + 1900) * 10000
                                           + (tm_yesterday.tm_mon + 1) * 100
                                           + tm_yesterday.tm_mday);
        yesterday = yesterday_real;
    }

    if (today == yesterday) {
        /* 连续签到 */
        s_user.streak++;
        s_user.last_study_date = today;
        ESP_LOGI(TAG, "Daily streak: consecutive! streak=%d", s_user.streak);
        /* 不给经验，经验由外部 achievement_complete_task 发放 */
        save_user_data_to_nvs_deferred();
        return true;
    }

    /* 中断 — 重置连续天数 */
    ESP_LOGI(TAG, "Daily streak: broken (last=%" PRIu32 ", today=%" PRIu32
             "), reset to 1", last, today);
    s_user.streak = 1;
    s_user.last_study_date = today;
    save_user_data_to_nvs_deferred();
    return true;
}

/**
 * @brief 启动时修复连续学习天数 (不发经验)
 *
 * 设备可能关机多日，需要在启动后检查 streak 是否已中断。
 * 如果距上次学习 >= 2 天，重置 streak = 0，保留 last_study_date，
 * 用户实际的后续学习动作会将其设为 1。
 */
void achievement_reconcile_streak(void)
{
    if (s_user.last_study_date == 0) {
        return;  /* 从未学习过，无需处理 */
    }

    uint32_t today = get_today_date();
    if (today == 0) {
        ESP_LOGW(TAG, "Streak reconcile: time not synced, skip");
        return;
    }

    if (today == s_user.last_study_date) {
        return;  /* 今天已签过到 */
    }

    /* 检查是否昨天有签到 (精确计算昨天日期) */
    time_t now = time_get_now();
    time_t day_before = now - 86400;
    struct tm tm_yesterday = {0};
    localtime_r(&day_before, &tm_yesterday);
    uint32_t yesterday = (uint32_t)((tm_yesterday.tm_year + 1900) * 10000
                                  + (tm_yesterday.tm_mon + 1) * 100
                                  + tm_yesterday.tm_mday);

    if (s_user.last_study_date == yesterday) {
        return;  /* 昨天刚学过，streak 仍有效 */
    }

    /* last_study_date 是 2+ 天前 — streak 中断 */
    ESP_LOGI(TAG, "Streak reconcile: last=%" PRIu32 " today=%" PRIu32
             " (gap >= 2 days), reset streak 0", s_user.last_study_date, today);
    s_user.streak = 0;
    save_user_data_to_nvs_deferred();
}

/* ═══════════════════════════════════════════════
   UI Refresh helpers — 实时更新 + 动画 + 徽章检测
   ═══════════════════════════════════════════════ */

static bool is_screen_active(void)
{
    return s_screen != NULL && lv_screen_active() == s_screen;
}

static void snapshot_badge_states(void)
{
    for (int i = 0; i < BADGE_COUNT; i++) {
        s_prev_badge_unlocked[i] = is_badge_unlocked_runtime(i);
    }
    s_badge_states_valid = true;
}

/**
 * @brief 更新页面内所有数据驱动的控件文字
 *
 * 仅更新 s_screen 上已存在的标签，不重建对象。
 * 通过 lv_async_call 安全调用。
 */
static void refresh_ui_in_place(void)
{
    if (!s_screen) return;

    /* EXP 文字 */
    if (s_exp_text) {
        char exp_buf[32];
        lv_snprintf(exp_buf, sizeof(exp_buf), "%d / %d", s_user.exp, s_user.exp_max);
        lv_label_set_text(s_exp_text, exp_buf);
    }

    /* 进度条宽度 */
    if (s_exp_bar_fg) {
        int pct = s_user.exp_max > 0 ? (s_user.exp * 100) / s_user.exp_max : 0;
        lv_obj_set_width(s_exp_bar_fg, LV_PCT(pct));
    }

    /* 等级名称 */
    if (s_level_name) {
        char name_buf[32];
        lv_snprintf(name_buf, sizeof(name_buf), "Lv.%d %s", s_user.level, get_level_title(s_user.level));
        lv_label_set_text(s_level_name, name_buf);
    }

    /* Header 等级标签 */
    if (s_header_level_label) {
        char tag_buf[32];
        lv_snprintf(tag_buf, sizeof(tag_buf), "Lv.%d %s", s_user.level, get_level_title(s_user.level));
        lv_label_set_text(s_header_level_label, tag_buf);
    }

    /* 统计数值 */
    if (s_stat_value_labels[0]) {
        char buf[4][16];
        lv_snprintf(buf[0], sizeof(buf[0]), "%d天", s_user.streak);
        lv_snprintf(buf[1], sizeof(buf[1]), "%d", s_user.questions);
        lv_snprintf(buf[2], sizeof(buf[2]), "%d", s_user.ai_chats);
        lv_snprintf(buf[3], sizeof(buf[3]), "%dh", s_user.study_hours);
        for (int i = 0; i < 4; i++) {
            if (s_stat_value_labels[i])
                lv_label_set_text(s_stat_value_labels[i], buf[i]);
        }
    }

    /* 徽章计数 */
    if (s_badge_count_label) {
        int unlocked = 0;
        for (int i = 0; i < BADGE_COUNT; i++) {
            if (is_badge_unlocked_runtime(i)) unlocked++;
        }
        char count_buf[16];
        lv_snprintf(count_buf, sizeof(count_buf), "(%d/%d)", unlocked, BADGE_COUNT);
        lv_label_set_text(s_badge_count_label, count_buf);
    }

    /* 徽章状态 */
    for (int i = 0; i < BADGE_COUNT; i++) {
        if (s_badge_widgets[i]) {
            bool unlocked = is_badge_unlocked_runtime(i);
            lv_obj_set_style_border_color(s_badge_widgets[i],
                unlocked ? COLOR_SUCCESS : COLOR_LOCKED, LV_PART_MAIN);
            lv_obj_set_style_opa(s_badge_widgets[i],
                unlocked ? LV_OPA_100 : LV_OPA_70, LV_PART_MAIN);
        }
    }
}

static void anim_exp_bar_cb_w(lv_obj_t *obj, int32_t v)
{
    lv_obj_set_width(obj, LV_PCT(v));
}

/**
 * @brief 经验值变化时播放进度条动画
 */
static void animate_exp_bar(int old_pct, int new_pct)
{
    if (!s_exp_bar_fg) return;
    if (old_pct >= new_pct) {
        lv_obj_set_width(s_exp_bar_fg, LV_PCT(new_pct));
        return;
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_exp_bar_fg);
    lv_anim_set_values(&a, old_pct, new_pct);
    lv_anim_set_duration(&a, 600);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_exp_bar_cb_w);
    lv_anim_start(&a);
}

/**
 * @brief 检测新解锁徽章，弹出庆祝弹窗
 */
static void check_badge_unlocks(void)
{
    if (!s_badge_states_valid) return;

    for (int i = 0; i < BADGE_COUNT; i++) {
        bool was_unlocked = s_prev_badge_unlocked[i];
        bool now_unlocked = is_badge_unlocked_runtime(i);
        if (!was_unlocked && now_unlocked) {
            /* 新解锁! */
            ESP_LOGI(TAG, "Badge unlocked: [%d] %s", i, s_badges[i].title);
            show_badge_unlock_modal(i);
            s_prev_badge_unlocked[i] = true;
        }
    }
}

static void badge_modal_auto_close_cb(lv_timer_t *timer)
{
    lv_obj_t *overlay = (lv_obj_t *)lv_timer_get_user_data(timer);
    if (overlay) {
        lv_obj_delete(overlay);
    }
}

static void badge_modal_close_cb(lv_event_t *e)
{
    if (e) {
        lv_obj_t *modal = lv_event_get_user_data(e);
        if (modal) lv_obj_delete(modal);
    }
}

/**
 * @brief 徽章解锁庆祝弹窗 — 居中卡片 + 图标 + 标题 + 描述 + 关闭按钮
 */
static void show_badge_unlock_modal(int badge_index)
{
    if (badge_index < 0 || badge_index >= BADGE_COUNT) return;

    lv_obj_t *overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);  /* 阻止穿透 */
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(overlay, badge_modal_close_cb, LV_EVENT_CLICKED, overlay);

    /* 弹窗卡片 */
    lv_obj_t *modal = lv_obj_create(overlay);
    lv_obj_set_size(modal, 400, 280);
    lv_obj_set_style_bg_color(modal, COLOR_CARD, 0);
    lv_obj_set_style_radius(modal, 24, 0);
    lv_obj_set_style_border_width(modal, 2, 0);
    lv_obj_set_style_border_color(modal, COLOR_SUCCESS, 0);
    lv_obj_center(modal);
    lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(modal, 0, 0);
    lv_obj_set_style_pad_gap(modal, SPACING_MD, 0);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

    /* 庆祝图标：礼花 PNG（回退符号/文字） */
    lv_obj_t *celeb_icon = icon_loader_create_image(modal, ICON_PARTY_POPPER, 56, 56);
    LV_UNUSED(celeb_icon);

    /* 大标题 */
    lv_obj_t *big_title = lv_label_create(modal);
    lv_label_set_text(big_title, "成就解锁!");
    lv_obj_set_style_text_color(big_title, COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(big_title, g_font_cjk_24, 0);

    /* 徽章名 */
    lv_obj_t *badge_name = lv_label_create(modal);
    lv_label_set_text(badge_name, s_badges[badge_index].title);
    lv_obj_set_style_text_color(badge_name, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(badge_name, g_font_cjk_20, 0);

    /* 描述 */
    lv_obj_t *badge_desc = lv_label_create(modal);
    lv_label_set_text(badge_desc, s_badges[badge_index].desc);
    lv_obj_set_style_text_color(badge_desc, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(badge_desc, g_font_cjk_14, 0);

    /* 关闭按钮 */
    lv_obj_t *close_btn = lv_obj_create(modal);
    lv_obj_set_size(close_btn, 140, 44);
    lv_obj_set_style_bg_color(close_btn, COLOR_SUCCESS, 0);
    lv_obj_set_style_radius(close_btn, 12, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(close_btn, badge_modal_close_cb, LV_EVENT_CLICKED, overlay);
    lv_obj_clear_flag(close_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "知道了!");
    lv_obj_set_style_text_color(close_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(close_label, g_font_cjk_16, 0);
    lv_obj_center(close_label);

    /* 入场动画: 缩放 + 淡入 */
    lv_obj_set_style_opa(overlay, LV_OPA_0, 0);
    lv_anim_t af;
    lv_anim_init(&af);
    lv_anim_set_var(&af, overlay);
    lv_anim_set_values(&af, LV_OPA_0, LV_OPA_60);
    lv_anim_set_duration(&af, 250);
    lv_anim_start(&af);

    /* 5 秒后自动关闭 */
    lv_timer_t *auto_close = lv_timer_create(badge_modal_auto_close_cb, 5000, overlay);
    lv_timer_set_repeat_count(auto_close, 1);  /* 单次触发 */

    /* 刷新徽章面板 */
    refresh_ui_in_place();
}

/* ═══════════════════════════════════════════════
   NVS data persistence
   ═══════════════════════════════════════════════ */

static void load_user_data_from_nvs(void)
{
    uint32_t val;
    if (storage_get_u32(KEY_ACHV_LEVEL, &val) == ESP_OK) {
        s_user.level = (int)val;
    }
    if (storage_get_u32(KEY_ACHV_EXP, &val) == ESP_OK) {
        s_user.exp = (int)val;
    }
    if (storage_get_u32(KEY_ACHV_EXP_MAX, &val) == ESP_OK) {
        s_user.exp_max = (int)val;
    } else {
        s_user.exp_max = 1000; /* default */
    }
    if (storage_get_u32(KEY_ACHV_STREAK, &val) == ESP_OK) {
        s_user.streak = (int)val;
    }
    if (storage_get_u32(KEY_ACHV_QUESTIONS, &val) == ESP_OK) {
        s_user.questions = (int)val;
    }
    if (storage_get_u32(KEY_ACHV_AI_CHATS, &val) == ESP_OK) {
        s_user.ai_chats = (int)val;
    }
    if (storage_get_u32(KEY_ACHV_STUDY_HRS, &val) == ESP_OK) {
        s_user.study_hours = (int)val;
    }
    if (storage_get_u32(KEY_ACHV_LAST_DATE, &val) == ESP_OK) {
        s_user.last_study_date = val;
    }

    ESP_LOGI(TAG, "Loaded from NVS: Lv.%d (%d/%d), streak=%d, q=%d, ai=%d, h=%d, last_date=%" PRIu32,
             s_user.level, s_user.exp, s_user.exp_max,
             s_user.streak, s_user.questions, s_user.ai_chats, s_user.study_hours,
             s_user.last_study_date);
}

static void save_user_data_to_nvs(void)
{
    storage_set_u32(KEY_ACHV_LEVEL, (uint32_t)s_user.level);
    storage_set_u32(KEY_ACHV_EXP, (uint32_t)s_user.exp);
    storage_set_u32(KEY_ACHV_EXP_MAX, (uint32_t)s_user.exp_max);
    storage_set_u32(KEY_ACHV_STREAK, (uint32_t)s_user.streak);
    storage_set_u32(KEY_ACHV_QUESTIONS, (uint32_t)s_user.questions);
    storage_set_u32(KEY_ACHV_AI_CHATS, (uint32_t)s_user.ai_chats);
    storage_set_u32(KEY_ACHV_STUDY_HRS, (uint32_t)s_user.study_hours);
    storage_set_u32(KEY_ACHV_LAST_DATE, s_user.last_study_date);

    ESP_LOGI(TAG, "Saved to NVS: Lv.%d (%d/%d), streak=%d, q=%d, ai=%d, h=%d, last=%" PRIu32,
             s_user.level, s_user.exp, s_user.exp_max,
             s_user.streak, s_user.questions, s_user.ai_chats, s_user.study_hours,
             s_user.last_study_date);
}

void achievement_ui_save(void)
{
    save_user_data_to_nvs();
}

/* ═══════════════════════════════════════════════
   Public getter interfaces
   ═══════════════════════════════════════════════ */

int achievement_ui_get_level(void)       { return s_user.level; }
int achievement_ui_get_exp(void)         { return s_user.exp; }
int achievement_ui_get_exp_max(void)     { return s_user.exp_max; }
int achievement_ui_get_streak(void)      { return s_user.streak; }
int achievement_ui_get_questions(void)   { return s_user.questions; }
int achievement_ui_get_ai_chats(void)    { return s_user.ai_chats; }
int achievement_ui_get_study_hours(void) { return s_user.study_hours; }

/* ═══════════════════════════════════════════════
   Public modifier interfaces
   ═══════════════════════════════════════════════ */

/* ── 延迟 UI 刷新参数 (线程安全: 从任意任务投递到 LVGL) ── */
typedef struct {
    int old_pct;
    int new_pct;
} exp_anim_param_t;

static void deferred_exp_anim_and_refresh(void *arg)
{
    exp_anim_param_t *p = (exp_anim_param_t *)arg;
    if (!p) return;
    animate_exp_bar(p->old_pct, p->new_pct);
    refresh_ui_in_place();
    check_badge_unlocks();
    free(p);
}

bool achievement_ui_add_exp(int exp)
{
    if (exp <= 0) {
        return false;
    }

    int old_pct = s_user.exp_max > 0 ? (s_user.exp * 100) / s_user.exp_max : 0;
    s_user.exp += exp;
    int new_pct = s_user.exp_max > 0 ? (s_user.exp * 100) / s_user.exp_max : 0;

    ESP_LOGI(TAG, "Add exp +%d, now %d/%d (%d%% → %d%%)",
             exp, s_user.exp, s_user.exp_max, old_pct, new_pct);

    bool leveled = check_level_up();
    save_user_data_to_nvs_deferred();

    /* 线程安全: 如果成就页面正在显示, 通过 lv_async_call 投递 UI 更新 */
    if (is_screen_active()) {
        exp_anim_param_t *p = malloc(sizeof(exp_anim_param_t));
        if (p) {
            p->old_pct = old_pct;
            p->new_pct = new_pct;
            lv_async_call(deferred_exp_anim_and_refresh, p);
        }
    }

    return leveled;
}

int achievement_ui_complete_task(achievement_task_t task, int count)
{
    if (count <= 0) {
        return s_user.exp;
    }

    /* 首次任务触发时自动修复 streak (处理设备关机多日) */
    achievement_reconcile_streak();

    int exp_gained = 0;

    switch (task) {
        case ACHV_TASK_QUESTION:
            s_user.questions += count;
            exp_gained = count * 10;
            ESP_LOGI(TAG, "Complete %d questions, total=%d", count, s_user.questions);
            break;

        case ACHV_TASK_AI_CHAT:
            s_user.ai_chats += count;
            exp_gained = count * 5;
            ESP_LOGI(TAG, "Complete %d AI chats, total=%d", count, s_user.ai_chats);
            break;

        case ACHV_TASK_STUDY_MINUTE:
            s_user.study_hours += count / 60;
            exp_gained = count;
            ESP_LOGI(TAG, "Study %d min, total=%d hours", count, s_user.study_hours);
            break;

        case ACHV_TASK_STUDY_DAY:
            if (achievement_check_daily_streak()) {
                exp_gained = 50;
                ESP_LOGI(TAG, "Daily check-in! streak=%d, exp+%d", s_user.streak, exp_gained);
            } else {
                ESP_LOGI(TAG, "Already checked in today, no extra exp");
            }
            break;

        default:
            return s_user.exp;
    }

    /* P1: 任何任务完成时自动触发每日签到 (幂等, 每天仅首次加分) */
    if (task != ACHV_TASK_STUDY_DAY) {
        if (achievement_check_daily_streak()) {
            exp_gained += 50;
            ESP_LOGI(TAG, "Auto daily check-in! streak=%d, +50 EXP", s_user.streak);
        }
    }

    if (exp_gained > 0) {
        bool leveled = achievement_ui_add_exp(exp_gained);
        /* 升级时 also check badges (add_exp already calls check_badge_unlocks) */
        if (is_screen_active()) {
            refresh_ui_in_place();
            check_badge_unlocks();
        }
    } else {
        save_user_data_to_nvs_deferred();
    }

    return s_user.exp;
}

bool achievement_ui_is_badge_unlocked(int index)
{
    if (index < 0 || index >= BADGE_COUNT) {
        return false;
    }
    return is_badge_unlocked_runtime(index);
}

int achievement_ui_get_unlocked_badge_count(void)
{
    int count = 0;
    for (int i = 0; i < BADGE_COUNT; i++) {
        if (is_badge_unlocked_runtime(i)) {
            count++;
        }
    }
    return count;
}