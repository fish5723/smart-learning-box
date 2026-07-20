/**
 * @file home_ui.c
 * @brief 首页 UI 实现 — LVGL 9.x + 自生成思源宋体 CJK + PNG 图标
 *
 * 严格遵循 UI_DESIGN_SYSTEM.md 视觉规范：
 *   - Dark Theme (Background=#0F172A, Card=#1E293B)
 *   - 屏幕：1024×600
 *   - 颜色：Primary=#3B82F6, Success=#10B981, Warning=#F59E0B
 *   - 圆角：Card=16px, Dialog=20px, Button=12px, Avatar=50%
 *   - 间距：XS=4, SM=8, MD=12, LG=16, XL=24, XXL=32
 *
 * 中文字体：
 *   - font_loader 从 TF 卡加载 10 个字号 (12/14/16/18/20/24/26/32/36/48)
 *   - 固件内置 4 个字号作为 fallback (14/16/20/24)
 *   - 字符集覆盖 GB2312 常用汉字 + ASCII 32-126
 *
 * 图标：
 *   - PNG 图标从 TF 卡 emoji 文件夹加载 (优先)
 *   - LV_SYMBOL 文本图标 (回退)
 */

#include "home_ui.h"
#include "ai.h"
#include "game_center.h"
#include "ocr.h"
#include "achievement.h"
#include "wifi_ui.h"
#include "wifi.h"
#include "wrong_book_ui.h"
#include "photo_history_ui.h"
#include "bsp/time/sys_time.h"
#include "bsp/battery/battery.h"
#include "app/font_loader/font_loader.h"
#include "app/icon_loader/icon_loader.h"
#include "lvgl.h"
#include "esp_lv_adapter.h"
#include "esp_log.h"

static const char *TAG = "HOME_UI";

/* ═══════════════════════════════════════════════
   颜色定义（严格按 UI_DESIGN_SYSTEM.md §2）
   ═══════════════════════════════════════════════ */
#define COLOR_BG              lv_color_hex(0x0F172A)
#define COLOR_CARD            lv_color_hex(0x1E293B)
#define COLOR_BORDER          lv_color_hex(0x334155)
#define COLOR_PRIMARY         lv_color_hex(0x3B82F6)
#define COLOR_SUCCESS         lv_color_hex(0x10B981)
#define COLOR_WARNING         lv_color_hex(0xF59E0B)
#define COLOR_TEXT_PRIMARY    lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_SECONDARY  lv_color_hex(0x94A3B8)
#define COLOR_LOGO_BLUE       lv_color_hex(0x60A5FA)
#define COLOR_STAT_BLUE       lv_color_hex(0x60A5FA)
#define COLOR_STATUS_TEXT     lv_color_hex(0xCBD5E1)
#define COLOR_LEVEL_TEXT      lv_color_hex(0xE2E8F0)

/* ── 功能卡片强调色 ── */
#define COLOR_AI_ACCENT       lv_color_hex(0x2563EB)
#define COLOR_OCR_ACCENT      lv_color_hex(0x0891B2)
#define COLOR_GAME_ACCENT     lv_color_hex(0x7C3AED)
#define COLOR_ACHIEVEMENT_ACCENT lv_color_hex(0x10B981)
#define COLOR_DANGER_ACCENT      lv_color_hex(0xEF4444)

/* ═══════════════════════════════════════════════
   间距定义（UI_DESIGN_SYSTEM.md §5）
   ═══════════════════════════════════════════════ */
#define SPACING_SM  8
#define SPACING_MD  12
#define SPACING_LG  16
#define SPACING_XL  24

/* ── UI_DESIGN_SYSTEM.md §4 圆角 ── */
#define RADIUS_CARD    16
#define RADIUS_DIALOG  20
#define RADIUS_STATUS  14

/* ═══════════════════════════════════════════════
   布局常量（1024×600，UI_DESIGN_SYSTEM.md §6）
   ═══════════════════════════════════════════════ */
#define SCREEN_W        1024
#define SCREEN_H        600
#define CONTAINER_PAD   16
#define ITEM_GAP        14
#define STATUS_BAR_H    52
#define WELCOME_CARD_H  120
#define FOOTER_H        70

/* ── 栅格区域：600 - 16*2 - 52 - 14 - 120 - 14 - 14 - 70 = 284px ── */
#define GRID_AREA_H     284
#define GRID_GAP        14
#define CARD_H          ((GRID_AREA_H - GRID_GAP) / 2)   /* 135px */
#define CARD_W          ((SCREEN_W - CONTAINER_PAD * 2 - GRID_GAP * 2) / 3)  /* ~321px */

/* ═══════════════════════════════════════════════
   字体 — 由 font_loader 模块管理 (TF 卡 BinFont + 固件 fallback)
   10 个字号: 12/14/16/18/20/24/26/32/36/48
   ═══════════════════════════════════════════════ */
/* 字体指针定义在 font_loader.c 中, 此处仅通过 font_loader.h 引用 */

/* ═══════════════════════════════════════════════
   全局对象句柄
   ═══════════════════════════════════════════════ */
static lv_obj_t *s_screen       = NULL;
static lv_obj_t *s_status_bar   = NULL;
static lv_obj_t *s_welcome_card = NULL;
static lv_obj_t *s_function_grid = NULL;
static lv_obj_t *s_footer       = NULL;
static lv_obj_t *s_card_ai      = NULL;
static lv_obj_t *s_card_ocr     = NULL;
static lv_obj_t *s_card_game    = NULL;
static lv_obj_t *s_card_achievement = NULL;
static lv_obj_t *s_card_wrongbook   = NULL;
static lv_obj_t *s_card_photos      = NULL;
static lv_obj_t *s_wifi_text    = NULL;
static lv_obj_t *s_clock       = NULL;
static lv_obj_t *s_battery_label = NULL;

/* footer stat value labels (0=streak,1=questions,2=ai_chats,3=study_hours) */
static lv_obj_t *s_footer_vals[4] = {NULL};
/* welcome card dynamic labels */
static lv_obj_t *s_welcome_level_label = NULL;
static lv_obj_t *s_welcome_exp_text    = NULL;
static lv_obj_t *s_welcome_exp_bar_fg  = NULL;

/* refresh timers */
static lv_timer_t *s_clock_timer = NULL;
static lv_timer_t *s_stats_timer = NULL;

/* ═══════════════════════════════════════════════
   内部函数声明
   ═══════════════════════════════════════════════ */
static void create_status_bar(lv_obj_t *parent);
static void create_welcome_card(lv_obj_t *parent);
static void create_function_grid(lv_obj_t *parent);
static void create_footer(lv_obj_t *parent);
static lv_obj_t *create_function_card(lv_obj_t *parent,
    icon_id_t icon_id, const char *title, const char *desc, lv_color_t accent);
static void on_card_event(lv_event_t *e);
static void anim_scale_cb(lv_obj_t *obj, int32_t v);
static void refresh_home_stats_cb(lv_timer_t *timer);

/* ═══════════════════════════════════════════════
   页面创建
   ═══════════════════════════════════════════════ */

/* 前向声明 — 在 home_ui_init 中注册为 WiFi 回调 */
static void on_home_wifi_event(wifi_cb_event_t event, void *arg);

/* 构建首页所有控件 + 定时器（可重复调用，用于字体/图标就绪后重建） */
static void build_home_screen(void)
{
    /* 创建屏幕：固定 1024×600，flex column */
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_pad_all(s_screen, CONTAINER_PAD, 0);
    lv_obj_set_style_pad_gap(s_screen, ITEM_GAP, 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(s_screen);      /* 内部创建 s_clock_timer */
    create_welcome_card(s_screen);
    create_function_grid(s_screen);
    create_footer(s_screen);

    /* 每 5 秒从成就模块刷新首页数据 */
    s_stats_timer = lv_timer_create(refresh_home_stats_cb, 5000, NULL);
    lv_timer_ready(s_stats_timer);  /* 立即刷新一次 */
}

void home_ui_init(void)
{
    ESP_LOGI(TAG, "home_ui_init()");

    /* 注册 WiFi 状态回调 — 自动更新状态栏文字（仅一次，重建时不重复注册） */
    wifi_register_callback(on_home_wifi_event, NULL);

    /* 确保字体已初始化 (fallback 保底) */
    if (!g_font_cjk_16) {
        font_loader_init();
    }

    build_home_screen();
}

void home_ui_show(void)
{
    if (s_screen) {
        lv_screen_load(s_screen);
    }
}

void home_ui_hide(void)
{
    /* 根页面，隐藏即返回系统默认 */
}

/* ═══════════════════════════════════════════════
   顶部状态栏（Height=52, Radius=14）
   [WiFi状态]  [智趣宝盒]  [电量 时间]
   ═══════════════════════════════════════════════ */

/* 时钟更新回调 — 每秒从 sys_time 读取并刷新显示 */
static void update_clock_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (!s_clock) return;

    char time_str[TIME_STR_MAX_LEN];
    time_get_local_str(time_str, sizeof(time_str));

    /* time_get_local_str 返回 "2025-07-05 14:30:00", 取后 8 字节 "14:30:00" */
    const char *hhmmss = time_str + 11;  /* skip "YYYY-MM-DD " */
    lv_label_set_text(s_clock, hhmmss);
}

static void create_status_bar(lv_obj_t *parent)
{
    s_status_bar = lv_obj_create(parent);
    lv_obj_set_size(s_status_bar, LV_PCT(100), STATUS_BAR_H);
    lv_obj_set_style_bg_color(s_status_bar, COLOR_CARD, 0);
    lv_obj_set_style_radius(s_status_bar, RADIUS_STATUS, 0);
    lv_obj_set_style_pad_hor(s_status_bar, SPACING_XL, 0);
    lv_obj_set_style_pad_ver(s_status_bar, 0, 0);
    lv_obj_set_style_border_width(s_status_bar, 0, 0);
    lv_obj_set_flex_flow(s_status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_status_bar,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_status_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* ── 左侧 WiFi 图标 + 文字 ── */
    lv_obj_t *wifi_area = lv_obj_create(s_status_bar);
    lv_obj_set_size(wifi_area, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wifi_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_area, 0, 0);
    lv_obj_set_style_pad_gap(wifi_area, 4, 0);
    lv_obj_set_flex_flow(wifi_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wifi_area, LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(wifi_area, LV_OBJ_FLAG_SCROLLABLE);
    /* 点击跳转 WiFi 配置页面 */
    lv_obj_add_flag(wifi_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wifi_area, on_card_event, LV_EVENT_ALL, NULL);
    lv_obj_set_user_data(wifi_area, (void *)5);

    lv_obj_t *wifi_icon = lv_label_create(wifi_area);
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi_icon, COLOR_STATUS_TEXT, 0);
    lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_14, 0);

    s_wifi_text = lv_label_create(wifi_area);
    lv_label_set_text(s_wifi_text, "WiFi未连接");
    lv_obj_set_style_text_color(s_wifi_text, COLOR_STATUS_TEXT, 0);
    lv_obj_set_style_text_font(s_wifi_text, g_font_cjk_14, 0);

    /* ── 中间 Logo ── */
    lv_obj_t *logo = lv_label_create(s_status_bar);
    lv_label_set_text(logo, "智趣宝盒");
    lv_obj_set_style_text_color(logo, COLOR_LOGO_BLUE, 0);
    lv_obj_set_style_text_font(logo, g_font_cjk_20, 0);

    /* ── 右侧 电量 + 时间 ── */
    lv_obj_t *right = lv_obj_create(s_status_bar);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_gap(right, SPACING_MD, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *battery_icon = lv_label_create(right);
    lv_label_set_text(battery_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_color(battery_icon, COLOR_STATUS_TEXT, 0);
    lv_obj_set_style_text_font(battery_icon, &lv_font_montserrat_14, 0);

    lv_obj_t *battery = lv_label_create(right);
    int batt_pct = battery_get_cached_percent();
    if (batt_pct >= 0)
        lv_label_set_text_fmt(battery, "%d%%", batt_pct);
    else
        lv_label_set_text(battery, "--");
    lv_obj_set_style_text_color(battery, COLOR_STATUS_TEXT, 0);
    lv_obj_set_style_text_font(battery, g_font_cjk_14, 0);
    s_battery_label = battery;

    char time_str[TIME_STR_MAX_LEN];
    lv_obj_t *clock = lv_label_create(right);
    time_get_local_str(time_str, sizeof(time_str));
    lv_label_set_text(clock, time_str + 11);  /* "HH:MM:SS" */
    lv_obj_set_style_text_color(clock, COLOR_STATUS_TEXT, 0);
    lv_obj_set_style_text_font(clock, g_font_cjk_14, 0);
    s_clock = clock;

    /* 每秒更新时钟显示 */
    s_clock_timer = lv_timer_create(update_clock_cb, 1000, NULL);
    lv_timer_ready(s_clock_timer);  /* 立即触发第一次更新 */
}

/* ═══════════════════════════════════════════════
   欢迎卡片（Height=120, Radius=20）
   [头像 用户信息]           [等级 EXP 进度条]
   ═══════════════════════════════════════════════ */

static void create_welcome_card(lv_obj_t *parent)
{
    s_welcome_card = lv_obj_create(parent);
    lv_obj_set_size(s_welcome_card, LV_PCT(100), WELCOME_CARD_H);
    lv_obj_set_style_bg_color(s_welcome_card, COLOR_CARD, 0);
    lv_obj_set_style_radius(s_welcome_card, RADIUS_DIALOG, 0);
    lv_obj_set_style_pad_all(s_welcome_card, SPACING_XL, 0);
    lv_obj_set_style_border_width(s_welcome_card, 0, 0);
    lv_obj_set_flex_flow(s_welcome_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_welcome_card,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_welcome_card, LV_OBJ_FLAG_SCROLLABLE);

    /* ── 左侧：头像 + 欢迎语 ── */
    lv_obj_t *user_area = lv_obj_create(s_welcome_card);
    lv_obj_set_size(user_area, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(user_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(user_area, 0, 0);
    lv_obj_set_style_pad_gap(user_area, 18, 0);
    lv_obj_set_flex_flow(user_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(user_area, LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(user_area, LV_OBJ_FLAG_SCROLLABLE);

    /* 头像（圆形 72×72） */
    lv_obj_t *avatar = lv_obj_create(user_area);
    lv_obj_set_size(avatar, 72, 72);
    lv_obj_set_style_bg_color(avatar, COLOR_AI_ACCENT, 0);
    lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(avatar, 0, 0);
    lv_obj_clear_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);

    /* 头像内部：TF 卡 PNG 图标（全彩，居中于蓝色圆底；SD 不可用自动回退符号）
     * 位于 build_home_screen() 链路，靠 home_ui_update_fonts() 重建后显示 PNG。 */
    lv_obj_t *avatar_icon = icon_loader_create_image(avatar, ICON_SMILE, 48, 48);
    lv_obj_center(avatar_icon);

    /* 欢迎语 + 口号 */
    lv_obj_t *user_info = lv_obj_create(user_area);
    lv_obj_set_size(user_info, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(user_info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(user_info, 0, 0);
    lv_obj_set_flex_flow(user_info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(user_info, 6, 0);
    lv_obj_clear_flag(user_info, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *welcome = lv_label_create(user_info);
    lv_label_set_text(welcome, "欢迎回来,小礼");
    lv_obj_set_style_text_color(welcome, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(welcome, g_font_cjk_24, 0);

    lv_obj_t *slogan = lv_label_create(user_info);
    lv_label_set_text(slogan, "让学习像游戏一样有趣");
    lv_obj_set_style_text_color(slogan, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(slogan, g_font_cjk_14, 0);

    /* ── 右侧：学习数据 ── */
    lv_obj_t *study = lv_obj_create(s_welcome_card);
    lv_obj_set_size(study, 320, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(study, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(study, 0, 0);
    lv_obj_set_flex_flow(study, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(study, 10, 0);
    lv_obj_clear_flag(study, LV_OBJ_FLAG_SCROLLABLE);

    /* 等级行 */
    lv_obj_t *level_row = lv_obj_create(study);
    lv_obj_set_size(level_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(level_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(level_row, 0, 0);
    lv_obj_set_flex_flow(level_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(level_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(level_row, LV_OBJ_FLAG_SCROLLABLE);

    int lvl = achievement_ui_get_level();
    int e = achievement_ui_get_exp();
    int em = achievement_ui_get_exp_max();
    const char *lev_title = lvl >= 10 ? "学神" :
                            lvl >= 7  ? "学习达人" :
                            lvl >= 5  ? "学习能手" :
                            lvl >= 3  ? "学习新星" : "初学者";
    char lev_buf[32], exp_buf[32];

    lv_obj_t *level = lv_label_create(level_row);
    lv_snprintf(lev_buf, sizeof(lev_buf), "Lv.%d %s", lvl, lev_title);
    lv_label_set_text(level, lev_buf);
    lv_obj_set_style_text_color(level, COLOR_LEVEL_TEXT, 0);
    lv_obj_set_style_text_font(level, g_font_cjk_16, 0);
    s_welcome_level_label = level;

    lv_obj_t *exp = lv_label_create(level_row);
    lv_snprintf(exp_buf, sizeof(exp_buf), "%d / %d EXP", e, em);
    lv_label_set_text(exp, exp_buf);
    lv_obj_set_style_text_color(exp, COLOR_LEVEL_TEXT, 0);
    lv_obj_set_style_text_font(exp, g_font_cjk_14, 0);
    s_welcome_exp_text = exp;

    /* 进度条背景 */
    lv_obj_t *bar_bg = lv_obj_create(study);
    lv_obj_set_size(bar_bg, LV_PCT(100), 10);
    lv_obj_set_style_bg_color(bar_bg, COLOR_BORDER, 0);
    lv_obj_set_style_radius(bar_bg, 10, 0);
    lv_obj_set_style_border_width(bar_bg, 0, 0);
    lv_obj_set_style_pad_all(bar_bg, 0, 0);   
    lv_obj_clear_flag(bar_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* 进度条前景 — 基于真实经验值 */
    int bar_pct = em > 0 ? (e * 100) / em : 0;
    lv_obj_t *bar_fg = lv_obj_create(bar_bg);
    lv_obj_set_size(bar_fg, LV_PCT(bar_pct), 10);
    lv_obj_set_style_bg_color(bar_fg, COLOR_SUCCESS, 0);
    lv_obj_set_style_radius(bar_fg, 10, 0);
    lv_obj_set_style_border_width(bar_fg, 0, 0);
    lv_obj_set_style_pad_all(bar_fg, 0, 0);
    lv_obj_align(bar_fg, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(bar_fg, LV_OBJ_FLAG_SCROLLABLE);
    s_welcome_exp_bar_fg = bar_fg;
}

/* ═══════════════════════════════════════════════
   功能入口网格（2×2, Gap=14）
   flex_grow=1 填满剩余空间，杜绝溢出
   ═══════════════════════════════════════════════ */

static void create_function_grid(lv_obj_t *parent)
{
    s_function_grid = lv_obj_create(parent);
    /* 宽度 100%，flex_grow 决定高度（取代 LV_PCT(100) 的溢出问题） */
    lv_obj_set_size(s_function_grid, LV_PCT(100), 0);
    lv_obj_set_flex_grow(s_function_grid, 1);
    lv_obj_set_style_bg_opa(s_function_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_function_grid, 0, 0);
    lv_obj_set_style_pad_all(s_function_grid, 0, 0);
    lv_obj_set_style_pad_gap(s_function_grid, GRID_GAP, 0);
    lv_obj_set_flex_flow(s_function_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_function_grid,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(s_function_grid, LV_OBJ_FLAG_SCROLLABLE);

    /* 六张功能卡 */
    s_card_ai = create_function_card(s_function_grid,
        ICON_ROBOT, "AI老师", "智能问答与学习辅导", COLOR_AI_ACCENT);
    lv_obj_set_user_data(s_card_ai, (void *)1);

    s_card_ocr = create_function_card(s_function_grid,
        ICON_FLASH_CAMERA, "拍照解题", "OCR识别与AI讲解", COLOR_OCR_ACCENT);
    lv_obj_set_user_data(s_card_ocr, (void *)2);

    s_card_game = create_function_card(s_function_grid,
        ICON_GAME_HANDLE, "趣味游戏", "数学2048挑战", COLOR_GAME_ACCENT);
    lv_obj_set_user_data(s_card_game, (void *)3);

    s_card_achievement = create_function_card(s_function_grid,
        ICON_TROPHY, "成长中心", "积分与成就系统", COLOR_ACHIEVEMENT_ACCENT);
    lv_obj_set_user_data(s_card_achievement, (void *)4);

    s_card_wrongbook = create_function_card(s_function_grid,
        ICON_BOOKS, "错题本", "标记错题 随时复习", COLOR_DANGER_ACCENT);
    lv_obj_set_user_data(s_card_wrongbook, (void *)6);

    s_card_photos = create_function_card(s_function_grid,
        ICON_FLASH_CAMERA, "拍照历史", "回看照片与解析", COLOR_OCR_ACCENT);
    lv_obj_set_user_data(s_card_photos, (void *)7);
}

static lv_obj_t *create_function_card(lv_obj_t *parent,
    icon_id_t icon_id, const char *title, const char *desc,
    lv_color_t accent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* 左边框强调色（仿 HTML .ai/.ocr/.game/.achievement） */
    lv_obj_set_style_border_width(card, 6, 0);
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(card, accent, 0);

    /* 图标 — PNG 优先，回退到 LV_SYMBOL */
    lv_obj_t *icon = icon_loader_create_image(card, icon_id, 48, 48);
    if (icon) {
        /* PNG 图标已创建，设置颜色 (仅对 label fallback 有意义) */
        lv_obj_set_style_text_color(icon, accent, 0);
    }

    /* 标题 — CJK 20（Card Title ≈ 18-20px） */
    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title_label, g_font_cjk_20, 0);

    /* 描述 — CJK 14（Caption） */
    lv_obj_t *desc_label = lv_label_create(card);
    lv_label_set_text(desc_label, desc);
    lv_obj_set_style_text_color(desc_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(desc_label, g_font_cjk_14, 0);

    /* 事件绑定 */
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, on_card_event, LV_EVENT_ALL, NULL);

    /* transform_scale 初始值 1.0 (=256) */
    lv_obj_set_style_transform_scale_x(card, 256, 0);
    lv_obj_set_style_transform_scale_y(card, 256, 0);

    return card;
}

/* ═══════════════════════════════════════════════
   底部统计栏（Height=70, Radius=16）
   4 项水平均匀分布
   ═══════════════════════════════════════════════ */

static void create_footer(lv_obj_t *parent)
{
    s_footer = lv_obj_create(parent);
    lv_obj_set_size(s_footer, LV_PCT(100), FOOTER_H);
    lv_obj_set_style_bg_color(s_footer, COLOR_CARD, 0);
    lv_obj_set_style_radius(s_footer, RADIUS_CARD, 0);
    lv_obj_set_style_border_width(s_footer, 0, 0);
    lv_obj_set_style_pad_all(s_footer, 0, 0);
    lv_obj_set_flex_flow(s_footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_footer,
        LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_footer, LV_OBJ_FLAG_SCROLLABLE);

    char val_buf[4][16];

    lv_snprintf(val_buf[0], sizeof(val_buf[0]), "%d天", achievement_ui_get_streak());
    lv_snprintf(val_buf[1], sizeof(val_buf[1]), "%d", achievement_ui_get_questions());
    lv_snprintf(val_buf[2], sizeof(val_buf[2]), "%d", achievement_ui_get_ai_chats());
    lv_snprintf(val_buf[3], sizeof(val_buf[3]), "%dh", achievement_ui_get_study_hours());

    const char *labels[] = {"连续学习", "完成题目", "AI问答", "学习时长"};

    for (int i = 0; i < 4; i++) {
        lv_obj_t *item = lv_obj_create(s_footer);
        lv_obj_set_size(item, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item,
            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        /* 数值 — CJK 20 */
        lv_obj_t *val = lv_label_create(item);
        lv_label_set_text(val, val_buf[i]);
        lv_obj_set_style_text_color(val, COLOR_STAT_BLUE, 0);
        lv_obj_set_style_text_font(val, g_font_cjk_20, 0);
        s_footer_vals[i] = val;

        /* 标签 — CJK 14 */
        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_color(lbl, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(lbl, g_font_cjk_14, 0);
    }
}

/* ═══════════════════════════════════════════════
   动画回调
   ═══════════════════════════════════════════════ */

static void anim_scale_cb(lv_obj_t *obj, int32_t v)
{
    lv_obj_set_style_transform_scale_x(obj, v, 0);
    lv_obj_set_style_transform_scale_y(obj, v, 0);
}

/* 延迟页面切换，避免在事件回调中直接删除当前对象 */
static void deferred_wifi_show(void *unused)
{
    LV_UNUSED(unused);
    wifi_ui_show();
}

/* ═══════════════════════════════════════════════
   WiFi 状态更新
   ═══════════════════════════════════════════════ */

void home_ui_update_wifi_status(void)
{
    if (!s_wifi_text) return;

    if (wifi_is_connected()) {
        lv_label_set_text(s_wifi_text, "WiFi已连接");
    } else {
        lv_label_set_text(s_wifi_text, "WiFi未连接");
    }
}

/* 首页数据刷新 — 每 5 秒从成就模块读取实时数据 */
static void refresh_home_stats_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    char buf[16];

    /* 连续学习天数 */
    if (s_footer_vals[0]) {
        lv_snprintf(buf, sizeof(buf), "%d天", achievement_ui_get_streak());
        lv_label_set_text(s_footer_vals[0], buf);
    }
    /* 完成题目 */
    if (s_footer_vals[1]) {
        lv_snprintf(buf, sizeof(buf), "%d", achievement_ui_get_questions());
        lv_label_set_text(s_footer_vals[1], buf);
    }
    /* AI 问答 */
    if (s_footer_vals[2]) {
        lv_snprintf(buf, sizeof(buf), "%d", achievement_ui_get_ai_chats());
        lv_label_set_text(s_footer_vals[2], buf);
    }
    /* 学习时长 (小时) */
    if (s_footer_vals[3]) {
        lv_snprintf(buf, sizeof(buf), "%dh", achievement_ui_get_study_hours());
        lv_label_set_text(s_footer_vals[3], buf);
    }

    /* 欢迎卡片 — 等级 */
    if (s_welcome_level_label) {
        int level = achievement_ui_get_level();
        const char *title = level >= 10 ? "学神" :
                            level >= 7  ? "学习达人" :
                            level >= 5  ? "学习能手" :
                            level >= 3  ? "学习新星" : "初学者";
        lv_snprintf(buf, sizeof(buf), "Lv.%d %s", level, title);
        lv_label_set_text(s_welcome_level_label, buf);
    }
    /* 欢迎卡片 — EXP 文字 + 进度条 */
    if (s_welcome_exp_text) {
        int exp = achievement_ui_get_exp();
        int exp_max = achievement_ui_get_exp_max();
        lv_snprintf(buf, sizeof(buf), "%d / %d EXP", exp, exp_max);
        lv_label_set_text(s_welcome_exp_text, buf);
    }
    if (s_welcome_exp_bar_fg) {
        int exp = achievement_ui_get_exp();
        int exp_max = achievement_ui_get_exp_max();
        int pct = exp_max > 0 ? (exp * 100) / exp_max : 0;
        lv_obj_set_width(s_welcome_exp_bar_fg, LV_PCT(pct));
    }

    /* 电池电量 */
    if (s_battery_label) {
        int batt = battery_get_cached_percent();
        lv_label_set_text_fmt(s_battery_label, "%d%%", batt >= 0 ? batt : 0);
    }
}

/* ═══════════════════════════════════════════════
   字体/图标热更新 — SD 卡就绪后调用
   ═══════════════════════════════════════════════ */

void home_ui_update_fonts(void)
{
    ESP_LOGI(TAG, "home_ui_update_fonts() — 重建首页应用 SD 字体/图标");

    /* 字体和图标已由调用方 (system_init) 加载完成，这里重建首页控件让它们生效。
     * 原因：控件在 home_ui_init 时用 fallback 字体创建，字体指针地址已变，
     * 必须重建控件才能应用新字体/图标（旧代码只重载字体不重建控件，UI 不变）。
     * 重建涉及 LVGL 对象增删，必须持有 adapter 锁（本函数在 main_task 调用，未持锁）。 */
    esp_lv_adapter_lock(-1);

    /* 记录旧的屏幕和定时器，重建后再删除（避免删除当前正在显示的屏幕） */
    lv_obj_t   *old_screen = s_screen;
    lv_timer_t *old_clock  = s_clock_timer;
    lv_timer_t *old_stats  = s_stats_timer;

    /* 构建新屏幕（覆盖 s_screen / s_clock_timer / s_stats_timer 及所有子控件句柄） */
    build_home_screen();
    lv_screen_load(s_screen);

    /* 删除旧屏幕及其定时器（子控件随屏幕一并销毁） */
    if (old_clock)  lv_timer_delete(old_clock);
    if (old_stats)  lv_timer_delete(old_stats);
    if (old_screen) lv_obj_delete(old_screen);

    esp_lv_adapter_unlock();
}

/* WiFi 事件回调 — 当连接状态变化时更新状态栏文字 */
static void on_home_wifi_event(wifi_cb_event_t event, void *arg)
{
    LV_UNUSED(arg);
    switch (event) {
    case WIFI_CB_CONNECTED:
    case WIFI_CB_DISCONNECTED:
    case WIFI_CB_GOT_IP:
    case WIFI_CB_LOST_IP:
        lv_async_call((lv_async_cb_t)home_ui_update_wifi_status, NULL);
        break;
    default:
        break;
    }
}

/* ═══════════════════════════════════════════════
   事件处理（统一入口，按 code 分发）
   ═══════════════════════════════════════════════ */

static void on_card_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *card = lv_event_get_target(e);
    int id = (int)(intptr_t)lv_obj_get_user_data(card);

    if (code == LV_EVENT_PRESSED) {
        /* Scale: 1.0 → 0.95（256 → 243） */
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, card);
        lv_anim_set_values(&a, 256, 243);
        lv_anim_set_duration(&a, 100);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_scale_cb);
        lv_anim_start(&a);
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        /* Scale: 0.95 → 1.0（243 → 256） */
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, card);
        lv_anim_set_values(&a, 243, 256);
        lv_anim_set_duration(&a, 150);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_scale_cb);
        lv_anim_start(&a);
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Card clicked: id=%d", id);
        switch (id) {
        case 1: ESP_LOGI(TAG, " → AI老师"); ai_show(); break;
        case 2: ESP_LOGI(TAG, " -> 拍照解题"); ocr_show(); break;
        case 3: ESP_LOGI(TAG, " → 趣味游戏"); game_center_show(); break;
        case 4: ESP_LOGI(TAG, " → 成长中心"); achievement_show(); break;
        case 5: ESP_LOGI(TAG, " → WiFi配置"); lv_async_call(deferred_wifi_show, NULL); break;
        case 6: ESP_LOGI(TAG, " → 错题本"); wrong_book_ui_show(); break;
        case 7: ESP_LOGI(TAG, " → 拍照历史"); photo_history_ui_show(); break;
        }
    }
}