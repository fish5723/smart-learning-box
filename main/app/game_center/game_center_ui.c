/**
 * @file game_center_ui.c
 * @brief 游戏中心 UI 实现 — LVGL 9.x
 *
 * 严格遵循 UI_DESIGN_SYSTEM.md 视觉规范：
 *   - Dark Theme (Background=#0F172A, Card=#1E293B, Border=#334155)
 *   - 屏幕：1024×600
 *   - 颜色：Primary=#3B82F6, Success=#10B981, Warning=#F59E0B
 *   - 圆角：Card=16, Dialog=20, Button=12
 *
 * 对应原型：UI/Screen_GameCenter.html
 */

#include "game_center_ui.h"
#include "home.h"
#include "home_ui.h"
#include "game2048/game2048.h"
#include "game2048/game2048_ui.h"
#include "math_quiz/math_quiz_ui.h"
#include "word_king/word_king_ui.h"
#include "app/font_loader/font_loader.h"
#include "app/icon_loader/icon_loader.h"
#include "app/achievement/achievement.h"
#include "nes_sel/nes_category_ui.h"
#include "lvgl.h"
#include "esp_log.h"

/* 外部字体声明（来自 font_loader.h） */
extern lv_font_t *g_font_cjk_12;
extern lv_font_t *g_font_cjk_14;
extern lv_font_t *g_font_cjk_16;
extern lv_font_t *g_font_cjk_18;
extern lv_font_t *g_font_cjk_20;
extern lv_font_t *g_font_cjk_24;
extern lv_font_t *g_font_cjk_26;
extern lv_font_t *g_font_cjk_32;
extern lv_font_t *g_font_cjk_36;
extern lv_font_t *g_font_cjk_48;

static const char *TAG = "GAME_CENTER_UI";

/* ═══════════════════════════════════════════════
   颜色定义（严格按 UI_DESIGN_SYSTEM.md §2）
   ═══════════════════════════════════════════════ */
#define COLOR_BG              lv_color_hex(0x0F172A)
#define COLOR_CARD            lv_color_hex(0x1E293B)
#define COLOR_BORDER          lv_color_hex(0x334155)
#define COLOR_PRIMARY         lv_color_hex(0x3B82F6)
#define COLOR_SUCCESS         lv_color_hex(0x10B981)
#define COLOR_WARNING         lv_color_hex(0xF59E0B)
#define COLOR_DANGER          lv_color_hex(0xEF4444)
#define COLOR_TEXT_PRIMARY    lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_SECONDARY  lv_color_hex(0x94A3B8)
#define COLOR_TITLE_BLUE      lv_color_hex(0x60A5FA)
#define COLOR_GOLD            lv_color_hex(0xFBBF24)
#define COLOR_STAT_BLUE       lv_color_hex(0x60A5FA)
#define COLOR_BANNER_PURPLE   lv_color_hex(0x7C3AED)
#define COLOR_BTN_BLUE        lv_color_hex(0x2563EB)
#define COLOR_BTN_LOCK        lv_color_hex(0x475569)

/* ── 游戏卡片强调色（对应 HTML icon-*） ──
   使用 LV_COLOR_MAKE 替代 lv_color_hex，支持静态初始化 */
#define COLOR_ICON_2048       LV_COLOR_MAKE(0xF5, 0x9E, 0x0B)
#define COLOR_ICON_MATH       LV_COLOR_MAKE(0x25, 0x63, 0xEB)
#define COLOR_ICON_WORD       LV_COLOR_MAKE(0x10, 0xB9, 0x81)
#define COLOR_ICON_COMING     LV_COLOR_MAKE(0x64, 0x74, 0x8B)

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
#define RADIUS_BUTTON  10
#define RADIUS_ICON    18

/* ═══════════════════════════════════════════════
   布局常量（1024×600，UI_DESIGN_SYSTEM.md §6）
   ═══════════════════════════════════════════════ */
#define SCREEN_W         1024
#define SCREEN_H         600
#define CONTAINER_PAD    16
#define ITEM_GAP         12
#define HEADER_H         60
#define BANNER_H         100
#define FOOTER_H         60

/* ── 2×2 栅格：剩余高度 = 600-16*2-60-12-100-12-12-60 = 312 ── */
#define GRID_GAP         14
#define GRID_H           (SCREEN_H - CONTAINER_PAD*2 - HEADER_H \
                          - ITEM_GAP - BANNER_H - ITEM_GAP     \
                          - ITEM_GAP - FOOTER_H)                /* 312 */
#define CARD_H           LV_SIZE_CONTENT   /* 自适应高度，确保内容完整显示 */
#define CARD_W           ((SCREEN_W - CONTAINER_PAD*2 - GRID_GAP) / 2) /* 489 */

/* ═══════════════════════════════════════════════
   全局对象句柄
   ═══════════════════════════════════════════════ */
static lv_obj_t *s_screen = NULL;

/* ── 动态数据 label 句柄（进入页面时刷新真实值） ── */
static lv_obj_t *s_banner_score_label = NULL;   /* Banner 积分 */
static lv_obj_t *s_footer_score_label = NULL;   /* 底部 游戏积分 */
static lv_obj_t *s_footer_level_label = NULL;   /* 底部 游戏等级 */

/* ═══════════════════════════════════════════════
   游戏卡片数据
   ═══════════════════════════════════════════════ */
typedef struct {
    const char *icon;        /* (legacy) LV_SYMBOL 文本，已由 icon_id 取代 */
    const char *name;        /* 游戏名称 */
    const char *desc;        /* 游戏描述 */
    const char *reward;      /* 积分奖励 */
    const char *btn_text;    /* 按钮文字 */
    lv_color_t  icon_bg;     /* 图标背景色 */
    bool        unlocked;    /* 是否已解锁 */
    icon_id_t   icon_id;     /* TF 卡 PNG 图标 */
} game_card_info_t;

/* ── 游戏图标使用 PNG (优先) 或 LV_SYMBOL 回退 ── */
static const game_card_info_t s_games[] = {
    {LV_SYMBOL_REFRESH, "数学2048",   "锻炼逻辑思维与数字感知", "+20 积分", "开始挑战",
     COLOR_ICON_2048,   true,  ICON_DIGITS},
    {LV_SYMBOL_PLUS,    "数学答题赛", "限时计算挑战",           "+30 积分", "开始挑战",
     COLOR_ICON_MATH,   true,  ICON_PENCIL},
    {LV_SYMBOL_AUDIO,   "单词王者",   "英语单词记忆挑战",       "+25 积分", "开始挑战",
     COLOR_ICON_WORD,   true,  ICON_BOOKS},
    {LV_SYMBOL_VIDEO,   "经典小霸王", "重温NES红白机经典",      "+15 积分", "开始挑战",
     COLOR_ICON_COMING, true,  ICON_GAME_HANDLE},  /* 更多游戏 → NES 模拟器, unlocked=true */
};

/* ═══════════════════════════════════════════════
   内部函数声明
   ═══════════════════════════════════════════════ */
static void create_header(lv_obj_t *parent);
static void create_banner(lv_obj_t *parent);
static void create_game_grid(lv_obj_t *parent);
static lv_obj_t *create_game_card(lv_obj_t *parent, int idx);
static void create_footer(lv_obj_t *parent);
static void on_back_click(lv_event_t *e);
static void on_game_card_click(lv_event_t *e);
static void refresh_dynamic_data(void);
/* ═══════════════════════════════════════════════
   页面创建
   ═══════════════════════════════════════════════ */

static void game_center_ui_create_screen(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_pad_all(s_screen, CONTAINER_PAD, 0);
    lv_obj_set_style_pad_gap(s_screen, ITEM_GAP, 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_header(s_screen);
    create_banner(s_screen);
    create_game_grid(s_screen);
    create_footer(s_screen);
}

void game_center_ui_init(void)
{
    ESP_LOGI(TAG, "game_center_ui_init() — deferred");
    /* 屏幕延迟到 game_center_ui_show() 时创建 */
}

void game_center_ui_show(void)
{
    if (!s_screen) {
        game_center_ui_create_screen();
    }
    /* 每次进入刷新真实数据（等级/积分随 OCR/AI/游戏得分变化） */
    refresh_dynamic_data();
    if (s_screen) {
        lv_screen_load(s_screen);
    }
}

/* ═══════════════════════════════════════════════
   动态数据刷新 — 从成就系统读取 exp / level
   仅更新已存在的 label 文字, 不重建页面 (本函数在 LVGL 线程调用)
   ═══════════════════════════════════════════════ */
static void refresh_dynamic_data(void)
{
    char buf[16];

    if (s_banner_score_label) {
        lv_snprintf(buf, sizeof(buf), "%d积分", achievement_get_exp());
        lv_label_set_text(s_banner_score_label, buf);
    }
    if (s_footer_score_label) {
        lv_snprintf(buf, sizeof(buf), "%d", achievement_get_exp());
        lv_label_set_text(s_footer_score_label, buf);
    }
    if (s_footer_level_label) {
        lv_snprintf(buf, sizeof(buf), "Lv.%d", achievement_get_level());
        lv_label_set_text(s_footer_level_label, buf);
    }
}

void game_center_ui_hide(void)
{
    /* 页面由 lv_screen_load 切换，无需手动销毁 */
}

/* ═══════════════════════════════════════════════
   顶部导航栏（Height=60, Radius=16）
   [← 返回]  🎮 趣味游戏  今日奖励已获得 30%
   ═══════════════════════════════════════════════ */

static void create_header(lv_obj_t *parent)
{
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, LV_PCT(100), HEADER_H);
    lv_obj_set_style_bg_color(header, COLOR_CARD, 0);
    lv_obj_set_style_radius(header, RADIUS_CARD, 0);
    lv_obj_set_style_pad_hor(header, 20, 0);
    lv_obj_set_style_pad_ver(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    /* ── 返回按钮 100×40 ── */
    lv_obj_t *back_btn = lv_obj_create(header);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_set_style_bg_color(back_btn, COLOR_BORDER, 0);
    lv_obj_set_style_radius(back_btn, RADIUS_BUTTON, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back_btn, on_back_click, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);

    /* 返回按钮内 flex 行：← + 返回 */
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

    /* ── 标题 "趣味游戏" — 24px Bold ── */
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "趣味游戏");
    lv_obj_set_style_text_color(title, COLOR_TITLE_BLUE, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_24, 0);

    /* ── 状态文字 ── */
    lv_obj_t *level = lv_label_create(header);
    lv_label_set_text(level, "今日奖励已获得 30%");
    lv_obj_set_style_text_color(level, COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(level, g_font_cjk_14, 0);
}

/* ═══════════════════════════════════════════════
   Banner（Height=100, Radius=20）
   [🏆 游戏化学习中心              1200积分]
   [   边玩边学，获得积分与成长值    当前拥有]
   ═══════════════════════════════════════════════ */

static void create_banner(lv_obj_t *parent)
{
    lv_obj_t *banner = lv_obj_create(parent);
    lv_obj_set_size(banner, LV_PCT(100), BANNER_H);
    lv_obj_set_style_bg_color(banner, COLOR_CARD, 0);
    lv_obj_set_style_radius(banner, RADIUS_DIALOG, 0);
    lv_obj_set_style_pad_all(banner, SPACING_XL, 0);
    lv_obj_set_style_border_width(banner, 0, 0);
    lv_obj_set_flex_flow(banner, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(banner,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(banner, LV_OBJ_FLAG_SCROLLABLE);

    /* ── 左侧：图标 + 文字 ── */
    lv_obj_t *banner_left = lv_obj_create(banner);
    lv_obj_set_size(banner_left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(banner_left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(banner_left, 0, 0);
    lv_obj_set_style_pad_gap(banner_left, SPACING_LG, 0);
    lv_obj_set_flex_flow(banner_left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(banner_left,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(banner_left, LV_OBJ_FLAG_SCROLLABLE);

    /* 圆形图标 60×60，紫色背景 🏆 */
    lv_obj_t *icon = lv_obj_create(banner_left);
    lv_obj_set_size(icon, 60, 60);
    lv_obj_set_style_bg_color(icon, COLOR_BANNER_PURPLE, 0);
    lv_obj_set_style_radius(icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon_label = icon_loader_create_image(icon, ICON_TROPHY, 40, 40);
    lv_obj_center(icon_label);

    /* 文字区域 */
    lv_obj_t *banner_text = lv_obj_create(banner_left);
    lv_obj_set_size(banner_text, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(banner_text, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(banner_text, 0, 0);
    lv_obj_set_flex_flow(banner_text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(banner_text, 6, 0);
    lv_obj_clear_flag(banner_text, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *h2 = lv_label_create(banner_text);
    lv_label_set_text(h2, "游戏化学习中心");
    lv_obj_set_style_text_color(h2, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(h2, g_font_cjk_20, 0);

    lv_obj_t *p = lv_label_create(banner_text);
    lv_label_set_text(p, "边玩边学,获得积分与成长值");
    lv_obj_set_style_text_color(p, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(p, g_font_cjk_14, 0);

    /* ── 右侧：积分显示 ── */
    lv_obj_t *banner_score = lv_obj_create(banner);
    lv_obj_set_size(banner_score, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(banner_score, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(banner_score, 0, 0);
    lv_obj_set_flex_flow(banner_score, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(banner_score,
        LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(banner_score, 4, 0);
    lv_obj_clear_flag(banner_score, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *score_val = lv_label_create(banner_score);
    char banner_buf[16];
    lv_snprintf(banner_buf, sizeof(banner_buf), "%d积分", achievement_get_exp());
    lv_label_set_text(score_val, banner_buf);
    lv_obj_set_style_text_color(score_val, COLOR_GOLD, 0);
    lv_obj_set_style_text_font(score_val, g_font_cjk_20, 0);
    s_banner_score_label = score_val;

    lv_obj_t *score_label = lv_label_create(banner_score);
    lv_label_set_text(score_label, "当前拥有");
    lv_obj_set_style_text_color(score_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(score_label, g_font_cjk_14, 0);
}

/* ═══════════════════════════════════════════════
   游戏入口网格（2×2, Gap=14）
   可滑动显示更多游戏
   ═══════════════════════════════════════════════ */

static void create_game_grid(lv_obj_t *parent)
{
    /* 创建可滑动容器 */
    lv_obj_t *scroll_cont = lv_obj_create(parent);
    lv_obj_set_size(scroll_cont, LV_PCT(100), 0);
    lv_obj_set_flex_grow(scroll_cont, 1);
    lv_obj_set_style_bg_opa(scroll_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll_cont, 0, 0);
    lv_obj_set_style_pad_all(scroll_cont, 0, 0);
    lv_obj_set_flex_flow(scroll_cont, LV_FLEX_FLOW_COLUMN);
    /* 保留 SCROLLABLE 标志以支持滑动 */
    lv_obj_set_scrollbar_mode(scroll_cont, LV_SCROLLBAR_MODE_OFF);

    /* 游戏卡片网格 */
    lv_obj_t *grid = lv_obj_create(scroll_cont);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, GRID_GAP, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    /* 4 张游戏卡片 */
    for (int i = 0; i < 4; i++) {
        create_game_card(grid, i);
    }
}

/* ═══════════════════════════════════════════════
   单张游戏卡片
   [icon 72×72] 游戏名            +20 积分
                 描述        [开始挑战/开发中]
   ═══════════════════════════════════════════════ */

static lv_obj_t *create_game_card(lv_obj_t *parent, int idx)
{
    const game_card_info_t *info = &s_games[idx];

    /* ── 卡片容器（参考 home_ui 简洁结构） ── */
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_radius(card, RADIUS_DIALOG, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* ── 第一行：图标 + 名称（水平排列） ── */
    lv_obj_t *icon_label = icon_loader_create_image(card, info->icon_id, 48, 48);
    LV_UNUSED(icon_label);

    lv_obj_t *name_label = lv_label_create(card);
    lv_label_set_text(name_label, info->name);
    lv_obj_set_style_text_color(name_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(name_label, g_font_cjk_24, 0);

    /* ── 描述 ── */
    lv_obj_t *desc_label = lv_label_create(card);
    lv_label_set_text(desc_label, info->desc);
    lv_obj_set_style_text_color(desc_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(desc_label, g_font_cjk_14, 0);

    /* ── 底部区域：积分 + 按钮（水平排列） ── */
    lv_obj_t *bottom_area = lv_obj_create(card);
    lv_obj_set_size(bottom_area, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bottom_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottom_area, 0, 0);
    lv_obj_set_flex_flow(bottom_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_area,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bottom_area, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* 积分奖励（右对齐，稍大字号） */
    lv_obj_t *reward = lv_label_create(bottom_area);
    lv_label_set_text(reward, info->reward);
    lv_obj_set_style_text_color(reward, COLOR_GOLD, 0);
    lv_obj_set_style_text_font(reward, g_font_cjk_16, 0);

    /* 操作按钮 */
    lv_obj_t *btn = lv_obj_create(bottom_area);
    lv_obj_set_size(btn, 120, 42);
    lv_obj_set_style_bg_color(btn,
        info->unlocked ? COLOR_BTN_BLUE : COLOR_BTN_LOCK, 0);
    lv_obj_set_style_radius(btn, RADIUS_BUTTON, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, info->btn_text);
    lv_obj_set_style_text_color(btn_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(btn_label, g_font_cjk_16, 0);
    lv_obj_center(btn_label);
    lv_obj_clear_flag(btn_label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* ── 事件绑定（只在 card 和 btn 上） ── */
    lv_obj_add_event_cb(btn, on_game_card_click, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    if (info->unlocked) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, on_game_card_click, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    }

    return card;
}

/* ═══════════════════════════════════════════════
   底部统计栏（Height=60, Radius=16）
   4 项水平均匀分布
   ═══════════════════════════════════════════════ */

static void create_footer(lv_obj_t *parent)
{
    lv_obj_t *footer = lv_obj_create(parent);
    lv_obj_set_size(footer, LV_PCT(100), FOOTER_H);
    lv_obj_set_style_bg_color(footer, COLOR_CARD, 0);
    lv_obj_set_style_radius(footer, RADIUS_CARD, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer,
        LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    char foot_score_buf[16];
    char foot_level_buf[16];
    lv_snprintf(foot_score_buf, sizeof(foot_score_buf), "%d", achievement_get_exp());
    lv_snprintf(foot_level_buf, sizeof(foot_level_buf), "Lv.%d", achievement_get_level());

    struct { const char *val; const char *lbl; } stats[] = {
        {"18",           "累计挑战"},
        {foot_score_buf, "游戏积分"},
        {"5",            "最高连胜"},
        {foot_level_buf, "游戏等级"},
    };

    for (int i = 0; i < 4; i++) {
        lv_obj_t *item = lv_obj_create(footer);
        lv_obj_set_size(item, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item,
            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        /* 数值 — CJK 20 */
        lv_obj_t *val = lv_label_create(item);
        lv_label_set_text(val, stats[i].val);
        lv_obj_set_style_text_color(val, COLOR_STAT_BLUE, 0);
        lv_obj_set_style_text_font(val, g_font_cjk_20, 0);

        /* 记录动态数据句柄: 1=游戏积分, 3=游戏等级 */
        if (i == 1) s_footer_score_label = val;
        else if (i == 3) s_footer_level_label = val;

        /* 标签 — CJK 14 */
        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, stats[i].lbl);
        lv_obj_set_style_text_color(lbl, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(lbl, g_font_cjk_14, 0);
    }
}

/* ═══════════════════════════════════════════════
   事件处理
   ═══════════════════════════════════════════════ */

static void on_back_click(lv_event_t *e)
{
    ESP_LOGI(TAG, "Back to home");
    home_show();
}

static void on_game_card_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Game card clicked: idx=%d, name=%s", idx, s_games[idx].name);

    /* Card 0 = 数学2048 */
    if (idx == 0 && s_games[idx].unlocked) {
        game2048_ui_show();
    }
    /* Card 1 = 数学答题赛 */
    else if (idx == 1 && s_games[idx].unlocked) {
        math_quiz_ui_show();
    }
    /* Card 2 = 单词王者 */
    else if (idx == 2 && s_games[idx].unlocked) {
        word_king_ui_show();
    }
    /* Card 3 = NES 游戏 */
    else if (idx == 3 && s_games[idx].unlocked) {
        nes_category_ui_show();
    }
}