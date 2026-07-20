/**
 * @file game2048_ui.c
 * @brief Math Adventure 2048 - LVGL 9.x game board UI
 *
 * Maps UI/Math_Adventure_2048.html prototype to LVGL widgets.
 *
 * Layout (1024x600, Dark Theme per UI_DESIGN_SYSTEM.md):
 *   - Header (60px): back button + gold title + level status
 *   - Main area (flex:1): 280px sidebar + 4x4 game board
 *   - Mission bar (80px): learning task description + reward
 *
 * Fixes applied vs prototype:
 *   - Complete tile palette 2..8192 (HTML had only 2/4/8/16/32)
 *   - Sidebar overflow: LV_LABEL_LONG_WRAP + constrained width on AI text
 *   - Chinese punctuation replaced with English (colon, comma, period)
 *   - Emoji icons replaced with LV_SYMBOL_* equivalents
 *   - Swipe gesture handling with scale animations
 */

#include "game2048_ui.h"
#include "game2048.h"
#include "home_ui.h"
#include "app/font_loader/font_loader.h"
#include "app/icon_loader/icon_loader.h"
#include "game_center.h"
#include "achievement.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "GAME2048_UI";

/* ── External CJK fonts (from home_ui.c) ── */
extern lv_font_t *g_font_cjk_14;
extern lv_font_t *g_font_cjk_16;
extern lv_font_t *g_font_cjk_20;
extern lv_font_t *g_font_cjk_24;

/* ═══════════════════════════════════════════════
   Color system (UI_DESIGN_SYSTEM.md Section 2)
   ═══════════════════════════════════════════════ */
#define COLOR_BG              lv_color_hex(0x0F172A)
#define COLOR_CARD            lv_color_hex(0x1E293B)
#define COLOR_BORDER          lv_color_hex(0x334155)
#define COLOR_TEXT_PRIMARY    lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_SECONDARY  lv_color_hex(0x94A3B8)
#define COLOR_TEXT_DARK       lv_color_hex(0x333333)
#define COLOR_GOLD            lv_color_hex(0xFBBF24)
#define COLOR_SUCCESS         lv_color_hex(0x10B981)
#define COLOR_BLUE_ACCENT     lv_color_hex(0x60A5FA)
#define COLOR_AI_TEXT         lv_color_hex(0xCBD5E1)
#define COLOR_BOARD_BG        lv_color_hex(0x334155)
#define COLOR_TILE_EMPTY      lv_color_hex(0x475569)
#define COLOR_BTN_BLUE        lv_color_hex(0x2563EB)

/*
 * Complete tile color palette (standard 2048 game colors).
 * Index: log2(value), e.g. tile_colors[1] = color for 2^1=2.
 * Values beyond the array use the last entry.
 */
static const lv_color_t s_tile_bg[] = {
    LV_COLOR_MAKE(0x47, 0x55, 0x69),  /* [0] empty/0 */
    LV_COLOR_MAKE(0xEE, 0xE4, 0xDA),  /* [1] 2     - beige */
    LV_COLOR_MAKE(0xED, 0xE0, 0xC8),  /* [2] 4     - darker beige */
    LV_COLOR_MAKE(0xF2, 0xB1, 0x79),  /* [3] 8     - light orange */
    LV_COLOR_MAKE(0xF5, 0x95, 0x63),  /* [4] 16    - orange */
    LV_COLOR_MAKE(0xF6, 0x7C, 0x5F),  /* [5] 32    - red-orange */
    LV_COLOR_MAKE(0xF7, 0x5C, 0x3B),  /* [6] 64    - red */
    LV_COLOR_MAKE(0xED, 0xCF, 0x72),  /* [7] 128   - gold */
    LV_COLOR_MAKE(0xED, 0xCC, 0x61),  /* [8] 256   - bright gold */
    LV_COLOR_MAKE(0xED, 0xC8, 0x50),  /* [9] 512   - brighter gold */
    LV_COLOR_MAKE(0xED, 0xC5, 0x3F),  /* [10] 1024 - very bright gold */
    LV_COLOR_MAKE(0xED, 0xC2, 0x2E),  /* [11] 2048 - brightest gold */
    LV_COLOR_MAKE(0xFE, 0x3D, 0x6C),  /* [12] 4096 - hot pink */
    LV_COLOR_MAKE(0xFF, 0x1D, 0x5C),  /* [13] 8192 - bright red */
};

/* Text color: 2 and 4 use dark text, all others use white */
static lv_color_t get_tile_bg(int value);
static lv_color_t get_tile_fg(int value);
static const lv_font_t *get_tile_font(int value);

/* ═══════════════════════════════════════════════
   Layout constants (1024x600)
   ═══════════════════════════════════════════════ */
#define SCREEN_W          1024
#define SCREEN_H          600
#define CONTAINER_PAD     16
#define ITEM_GAP          12
#define HEADER_H          60
#define SIDEBAR_W         280
#define FOOTER_H          80

/* Board geometry: fits within main area (~404px available height).
   BOARD_SIZE + board_pad*2 <= main area height => 380+20=400 < 404 OK */
#define BOARD_SIZE        380
#define TILE_GAP          10
#define TILE_RADIUS       12
#define TILE_COUNT        4
#define TILE_SIZE         ((BOARD_SIZE - TILE_GAP * 2 - TILE_GAP * (TILE_COUNT - 1)) / TILE_COUNT)

/* ═══════════════════════════════════════════════
   Static UI handles
   ═══════════════════════════════════════════════ */
static lv_obj_t *s_screen          = NULL;
static lv_obj_t *s_tiles[GAME2048_SIZE][GAME2048_SIZE];
static lv_obj_t *s_tile_labels[GAME2048_SIZE][GAME2048_SIZE];
static lv_obj_t *s_score_label     = NULL;
static lv_obj_t *s_best_label      = NULL;
static lv_obj_t *s_ai_box          = NULL;
static lv_obj_t *s_game_over_overlay = NULL;

/* ═══════════════════════════════════════════════
   Forward declarations
   ═══════════════════════════════════════════════ */
static void create_header(lv_obj_t *parent);
static void create_main_area(lv_obj_t *parent);
static void create_sidebar(lv_obj_t *parent);
static void create_board(lv_obj_t *parent);
static lv_obj_t *create_tile(lv_obj_t *parent, int row, int col);
static void create_footer(lv_obj_t *parent);
static void update_tile(int row, int col, int value);
static void refresh_all_tiles(void);
static void refresh_score(void);
static void on_back_click(lv_event_t *e);
static void on_swipe_event(lv_event_t *e);
static void on_restart_click(lv_event_t *e);
static void anim_scale_cb(lv_obj_t *obj, int32_t v);

/* ═══════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════ */

/* 构建 2048 屏幕（懒加载：首次 show 时才调用，确保 icon_loader 已就绪 PNG 生效） */
static void build_game2048_screen(void)
{
    /* ── Screen ── */
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_pad_all(s_screen, CONTAINER_PAD, 0);
    lv_obj_set_style_pad_gap(s_screen, ITEM_GAP, 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_header(s_screen);
    create_main_area(s_screen);
    create_footer(s_screen);

    /* Attach swipe gesture to the whole screen */
    lv_obj_add_event_cb(s_screen, on_swipe_event, LV_EVENT_GESTURE, NULL);
}

void game2048_ui_init(void)
{
    ESP_LOGI(TAG, "game2048_ui_init()");
    /* 屏幕改为懒加载：不在启动早期建屏（那时 icon_loader 尚未就绪，图标会回退符号）。
     * 首次 game2048_ui_show() 时才 build_game2048_screen()，此时 PNG 图标已可用。 */
}

void game2048_ui_show(void)
{
    if (!s_screen) build_game2048_screen();

    /* Only start a new game if no game is in progress or game is over */
    const game_state_t *st = game2048_get_state();
    bool has_tiles = false;
    for (int r = 0; r < GAME2048_SIZE && !has_tiles; r++) {
        for (int c = 0; c < GAME2048_SIZE && !has_tiles; c++) {
            if (st->board[r][c] != 0) has_tiles = true;
        }
    }
    if (!has_tiles || st->game_over) {
        game2048_start();
    }

    refresh_all_tiles();
    refresh_score();
    lv_screen_load(s_screen);
}

void game2048_ui_hide(void)
{
    /* Screen is switched away by lv_screen_load; objects persist */
}

void game2048_ui_update_board(void)
{
    refresh_all_tiles();
}

void game2048_ui_update_score(int score)
{
    if (s_score_label) {
        lv_label_set_text_fmt(s_score_label, "%d", score);
    }
    const game_state_t *st = game2048_get_state();
    if (s_best_label) {
        lv_label_set_text_fmt(s_best_label, "最高分: %d", st->best_score);
    }
}

void game2048_ui_show_game_over(void)
{
    if (!s_screen || s_game_over_overlay) return;

    const game_state_t *st = game2048_get_state();

    /* 计入成就: 每局 2048 游戏奖励一次 (按分数阶梯) */
    if (st->score > 0) {
        int games = st->score >= 2000 ? 3 :
                    st->score >= 1000 ? 2 : 1;
        achievement_complete_task(ACHV_TASK_QUESTION, games);
    }

    /* Semi-transparent overlay covering the whole screen */
    s_game_over_overlay = lv_obj_create(s_screen);
    lv_obj_set_size(s_game_over_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_game_over_overlay, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_game_over_overlay, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_game_over_overlay, 0, 0);
    lv_obj_set_style_radius(s_game_over_overlay, 0, 0);
    lv_obj_set_flex_flow(s_game_over_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_game_over_overlay,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(s_game_over_overlay, 20, 0);
    lv_obj_add_flag(s_game_over_overlay, LV_OBJ_FLAG_CLICKABLE);

    /* "Game Over" title */
    lv_obj_t *go_label = lv_label_create(s_game_over_overlay);
    lv_label_set_text(go_label, "游戏结束");
    lv_obj_set_style_text_color(go_label, COLOR_GOLD, 0);
    lv_obj_set_style_text_font(go_label, g_font_cjk_24, 0);

    /* Final score */
    lv_obj_t *final_score = lv_label_create(s_game_over_overlay);
    lv_label_set_text_fmt(final_score, "得分: %d", st->score);
    lv_obj_set_style_text_color(final_score, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(final_score, g_font_cjk_20, 0);

    /* Best score */
    lv_obj_t *best_score = lv_label_create(s_game_over_overlay);
    lv_label_set_text_fmt(best_score, "最高分: %d", st->best_score);
    lv_obj_set_style_text_color(best_score, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(best_score, g_font_cjk_16, 0);

    /* Win/lose message */
    lv_obj_t *msg = lv_label_create(s_game_over_overlay);
    lv_label_set_text(msg, st->won ? "达成2048! 继续挑战!" : "没有可移动的方块了, 再试一次!");
    lv_obj_set_style_text_color(msg, st->won ? COLOR_SUCCESS : COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(msg, g_font_cjk_14, 0);

    /* Restart button */
    lv_obj_t *restart_btn = lv_obj_create(s_game_over_overlay);
    lv_obj_set_size(restart_btn, 160, 50);
    lv_obj_set_style_bg_color(restart_btn, COLOR_BTN_BLUE, 0);
    lv_obj_set_style_radius(restart_btn, 12, 0);
    lv_obj_set_style_border_width(restart_btn, 0, 0);
    lv_obj_add_flag(restart_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(restart_btn, on_restart_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *restart_label = lv_label_create(restart_btn);
    lv_label_set_text(restart_label, "重新开始");
    lv_obj_set_style_text_color(restart_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(restart_label, g_font_cjk_16, 0);
    lv_obj_center(restart_label);
}

/* ═══════════════════════════════════════════════
   Header (h=60)
   [< Back]  Math Adventure 2048  Lv.3 Explorer
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

    /* ── Back button (100x40) ── */
    lv_obj_t *back_btn = lv_obj_create(header);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_set_style_bg_color(back_btn, COLOR_BORDER, 0);
    lv_obj_set_style_radius(back_btn, 10, 0);
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

    /* ── Title (gold, 24px) ── */
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "数学冒险 2048");
    lv_obj_set_style_text_color(title, COLOR_GOLD, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_24, 0);

    /* ── Level status ── */
    lv_obj_t *level = lv_label_create(header);
    lv_label_set_text(level, "Lv.3 数字探险家");
    lv_obj_set_style_text_color(level, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(level, g_font_cjk_14, 0);
}

/* ═══════════════════════════════════════════════
   Main area (flex:1): sidebar + board panel
   ═══════════════════════════════════════════════ */

static void create_main_area(lv_obj_t *parent)
{
    lv_obj_t *main = lv_obj_create(parent);
    lv_obj_set_size(main, LV_PCT(100), 0);
    lv_obj_set_flex_grow(main, 1);
    lv_obj_set_style_bg_opa(main, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main, 0, 0);
    lv_obj_set_style_pad_all(main, 0, 0);
    lv_obj_set_style_pad_gap(main, ITEM_GAP, 0);
    lv_obj_set_flex_flow(main, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(main,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(main, LV_OBJ_FLAG_SCROLLABLE);

    create_sidebar(main);
    create_board(main);
}

/* ═══════════════════════════════════════════════
   Sidebar (280px wide, three info cards)
   Card 1: Current level
   Card 2: Current score + best score
   Card 3: AI teacher tips (flex_grow, scroll-protected)
   ═══════════════════════════════════════════════ */

static void create_sidebar(lv_obj_t *parent)
{
    lv_obj_t *sidebar = lv_obj_create(parent);
    lv_obj_set_size(sidebar, SIDEBAR_W, LV_PCT(100));
    lv_obj_set_style_bg_opa(sidebar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sidebar, 0, 0);
    lv_obj_set_style_pad_all(sidebar, 0, 0);
    lv_obj_set_style_pad_gap(sidebar, ITEM_GAP, 0);
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Card 1: Current level ── */
    lv_obj_t *level_card = lv_obj_create(sidebar);
    lv_obj_set_size(level_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(level_card, COLOR_CARD, 0);
    lv_obj_set_style_radius(level_card, 16, 0);
    lv_obj_set_style_pad_all(level_card, 16, 0);
    lv_obj_set_style_border_width(level_card, 0, 0);
    lv_obj_set_flex_flow(level_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(level_card, 4, 0);
    lv_obj_clear_flag(level_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *level_title = lv_label_create(level_card);
    lv_label_set_text(level_title, "当前关卡");
    lv_obj_set_style_text_color(level_title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(level_title, g_font_cjk_14, 0);

    lv_obj_t *level_name = lv_label_create(level_card);
    lv_label_set_text(level_name, "数字森林");
    lv_obj_set_style_text_color(level_name, COLOR_BLUE_ACCENT, 0);
    lv_obj_set_style_text_font(level_name, g_font_cjk_20, 0);

    lv_obj_t *level_goal = lv_label_create(level_card);
    lv_label_set_text(level_goal, "目标: 合成64");
    lv_obj_set_style_text_color(level_goal, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(level_goal, g_font_cjk_14, 0);

    /* ── Card 2: Score ── */
    lv_obj_t *score_card = lv_obj_create(sidebar);
    lv_obj_set_size(score_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(score_card, COLOR_CARD, 0);
    lv_obj_set_style_radius(score_card, 16, 0);
    lv_obj_set_style_pad_all(score_card, 16, 0);
    lv_obj_set_style_border_width(score_card, 0, 0);
    lv_obj_set_flex_flow(score_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(score_card, 4, 0);
    lv_obj_clear_flag(score_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *score_title = lv_label_create(score_card);
    lv_label_set_text(score_title, "当前得分");
    lv_obj_set_style_text_color(score_title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(score_title, g_font_cjk_14, 0);

    s_score_label = lv_label_create(score_card);
    lv_label_set_text(s_score_label, "0");
    lv_obj_set_style_text_color(s_score_label, COLOR_BLUE_ACCENT, 0);
    lv_obj_set_style_text_font(s_score_label, g_font_cjk_24, 0);

    s_best_label = lv_label_create(score_card);
    lv_label_set_text(s_best_label, "最高分: 0");
    lv_obj_set_style_text_color(s_best_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_best_label, g_font_cjk_14, 0);

    /* ── Card 3: AI teacher (flex_grow to fill space, WRAP to prevent overflow) ── */
    lv_obj_t *ai_card = lv_obj_create(sidebar);
    lv_obj_set_size(ai_card, LV_PCT(100), 0);
    lv_obj_set_flex_grow(ai_card, 1);
    lv_obj_set_style_bg_color(ai_card, COLOR_CARD, 0);
    lv_obj_set_style_radius(ai_card, 16, 0);
    lv_obj_set_style_pad_all(ai_card, 16, 0);
    lv_obj_set_style_border_width(ai_card, 0, 0);
    lv_obj_set_flex_flow(ai_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(ai_card, 8, 0);
    lv_obj_clear_flag(ai_card, LV_OBJ_FLAG_SCROLLABLE);

    /* AI老师 标题：机器人图标 + 文字 */
    lv_obj_t *ai_title_row = lv_obj_create(ai_card);
    lv_obj_set_size(ai_title_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ai_title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ai_title_row, 0, 0);
    lv_obj_set_style_pad_all(ai_title_row, 0, 0);
    lv_obj_set_style_pad_gap(ai_title_row, 6, 0);
    lv_obj_set_flex_flow(ai_title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ai_title_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ai_title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ai_title_icon = icon_loader_create_image(ai_title_row, ICON_ROBOT, 18, 18);
    LV_UNUSED(ai_title_icon);

    lv_obj_t *ai_title = lv_label_create(ai_title_row);
    lv_label_set_text(ai_title, "AI老师");
    lv_obj_set_style_text_color(ai_title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(ai_title, g_font_cjk_14, 0);

    s_ai_box = lv_label_create(ai_card);
    /* CRITICAL: Wrap + fixed width prevents sidebar overflow */
    lv_label_set_long_mode(s_ai_box, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ai_box, SIDEBAR_W - 32);  /* card width - 2*pad */
    lv_label_set_text(s_ai_box,
        "欢迎来到数学冒险!\n\n"
        "滑动方块来移动.\n"
        "当两个相同数字的\n"
        "方块碰撞时,\n"
        "它们会合并成一个!\n"
        "\n"
        "数学知识:\n"
        "2^1=2, 2^2=4, 2^3=8,\n"
        "2^4=16, 2^5=32...\n"
        "你能到达2048吗?");
    lv_obj_set_style_text_color(s_ai_box, COLOR_AI_TEXT, 0);
    lv_obj_set_style_text_font(s_ai_box, g_font_cjk_14, 0);
}

/* ═══════════════════════════════════════════════
   Game board (4x4 grid, BOARD_SIZExBOARD_SIZE)
   ═══════════════════════════════════════════════ */

static void create_board(lv_obj_t *parent)
{
    /* Board panel: flex_grow fills remaining width, centers the board */
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, 0, LV_PCT(100));
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_style_bg_color(panel, COLOR_CARD, 0);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Board background */
    lv_obj_t *board = lv_obj_create(panel);
    lv_obj_set_size(board, BOARD_SIZE, BOARD_SIZE);
    lv_obj_set_style_bg_color(board, COLOR_BOARD_BG, 0);
    lv_obj_set_style_radius(board, 20, 0);
    lv_obj_set_style_pad_all(board, TILE_GAP, 0);
    lv_obj_set_style_pad_gap(board, TILE_GAP, 0);
    lv_obj_set_style_border_width(board, 0, 0);
    lv_obj_set_flex_flow(board, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(board,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(board, LV_OBJ_FLAG_SCROLLABLE);

    /* Create 16 tiles */
    for (int r = 0; r < GAME2048_SIZE; r++) {
        for (int c = 0; c < GAME2048_SIZE; c++) {
            s_tiles[r][c] = create_tile(board, r, c);
        }
    }
}

static lv_obj_t *create_tile(lv_obj_t *parent, int row, int col)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_size(tile, TILE_SIZE, TILE_SIZE);
    lv_obj_set_style_bg_color(tile, COLOR_TILE_EMPTY, 0);
    lv_obj_set_style_radius(tile, TILE_RADIUS, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    /* transform_scale initial = 1.0 (256) */
    lv_obj_set_style_transform_scale_x(tile, 256, 0);
    lv_obj_set_style_transform_scale_y(tile, 256, 0);

    lv_obj_t *label = lv_label_create(tile);
    lv_label_set_text(label, "");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);

    s_tile_labels[row][col] = label;

    return tile;
}

/* ═══════════════════════════════════════════════
   Footer / Mission bar (h=80)
   [LV_SYMBOL_GPS Task description]  [Reward +20 points]
   ═══════════════════════════════════════════════ */

static void create_footer(lv_obj_t *parent)
{
    lv_obj_t *footer = lv_obj_create(parent);
    lv_obj_set_size(footer, LV_PCT(100), FOOTER_H);
    lv_obj_set_style_bg_color(footer, COLOR_CARD, 0);
    lv_obj_set_style_radius(footer, 16, 0);
    lv_obj_set_style_pad_hor(footer, 20, 0);
    lv_obj_set_style_pad_ver(footer, 0, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    /* Left: task icon + description */
    lv_obj_t *task_area = lv_obj_create(footer);
    lv_obj_set_size(task_area, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(task_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(task_area, 0, 0);
    lv_obj_set_flex_flow(task_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(task_area, 8, 0);
    lv_obj_clear_flag(task_area, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *task_icon = icon_loader_create_image(task_area, ICON_BULLSEYE, 20, 20);
    LV_UNUSED(task_icon);

    lv_obj_t *mission = lv_label_create(task_area);
    lv_label_set_text(mission, "学习任务: 认识平方数与立方数");
    lv_obj_set_style_text_color(mission, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(mission, g_font_cjk_16, 0);

    /* Right: reward */
    lv_obj_t *reward = lv_label_create(footer);
    lv_label_set_text(reward, "完成奖励 +20积分");
    lv_obj_set_style_text_color(reward, COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(reward, g_font_cjk_20, 0);
}

/* ═══════════════════════════════════════════════
   Tile color helpers
   ═══════════════════════════════════════════════ */

static lv_color_t get_tile_bg(int value)
{
    if (value == 0) return COLOR_TILE_EMPTY;
    /* Find log2(value) */
    int idx = 0;
    int v = value;
    while (v > 1) { v /= 2; idx++; }
    int max_idx = (int)(sizeof(s_tile_bg) / sizeof(s_tile_bg[0])) - 1;
    if (idx > max_idx) idx = max_idx;
    if (idx < 0) idx = 0;
    return s_tile_bg[idx];
}

static lv_color_t get_tile_fg(int value)
{
    /* 2 and 4 use dark text; all other values use white */
    if (value == 2 || value == 4) {
        return COLOR_TEXT_DARK;
    }
    return COLOR_TEXT_PRIMARY;
}

static const lv_font_t *get_tile_font(int value)
{
    if (value >= 10000) return &lv_font_montserrat_16;
    if (value >= 1000)  return &lv_font_montserrat_20;
    if (value >= 100)   return &lv_font_montserrat_24;
    return &lv_font_montserrat_26;
}

/* ═══════════════════════════════════════════════
   Board refresh
   ═══════════════════════════════════════════════ */

static void update_tile(int row, int col, int value)
{
    lv_obj_t *tile  = s_tiles[row][col];
    lv_obj_t *label = s_tile_labels[row][col];
    if (!tile || !label) return;

    lv_color_t bg = get_tile_bg(value);

    lv_obj_set_style_bg_color(tile, bg, 0);

    if (value == 0) {
        lv_label_set_text(label, "");
    } else {
        lv_label_set_text_fmt(label, "%d", value);
        lv_obj_set_style_text_color(label, get_tile_fg(value), 0);
        lv_obj_set_style_text_font(label, get_tile_font(value), 0);
    }
}

static void refresh_all_tiles(void)
{
    const game_state_t *st = game2048_get_state();
    for (int r = 0; r < GAME2048_SIZE; r++) {
        for (int c = 0; c < GAME2048_SIZE; c++) {
            update_tile(r, c, st->board[r][c]);
        }
    }
}

static void refresh_score(void)
{
    const game_state_t *st = game2048_get_state();
    if (s_score_label) {
        lv_label_set_text_fmt(s_score_label, "%d", st->score);
    }
    if (s_best_label) {
        lv_label_set_text_fmt(s_best_label, "最高分: %d", st->best_score);
    }
}

/* ═══════════════════════════════════════════════
   Animation
   ═══════════════════════════════════════════════ */

static void anim_scale_cb(lv_obj_t *obj, int32_t v)
{
    lv_obj_set_style_transform_scale_x(obj, v, 0);
    lv_obj_set_style_transform_scale_y(obj, v, 0);
}

static void anim_tile_pop(lv_obj_t *tile)
{
    if (!tile) return;
    /* Pop-in: 0.6 (154) -> 1.0 (256) in 150ms */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, tile);
    lv_anim_set_values(&a, 154, 256);
    lv_anim_set_duration(&a, 150);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_scale_cb);
    lv_anim_start(&a);
}

/* ═══════════════════════════════════════════════
   Event handlers
   ═══════════════════════════════════════════════ */

static void on_back_click(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Back to game center");
    game_center_show();
}

static void on_swipe_event(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    game2048_direction_t move_dir;
    bool valid = true;

    switch (dir) {
    case LV_DIR_TOP:    move_dir = GAME2048_UP;    break;
    case LV_DIR_BOTTOM: move_dir = GAME2048_DOWN;  break;
    case LV_DIR_LEFT:   move_dir = GAME2048_LEFT;  break;
    case LV_DIR_RIGHT:  move_dir = GAME2048_RIGHT; break;
    default:            valid = false;              break;
    }

    if (!valid) return;

    const game_state_t *st_before = game2048_get_state();
    int score_before = st_before->score;

    bool moved = game2048_move(move_dir);

    if (moved) {
        ESP_LOGI(TAG, "Swipe dir=%d, score %d -> %d",
            (int)move_dir, score_before, game2048_get_state()->score);

        refresh_all_tiles();
        refresh_score();

        /* Pop animation on tiles that changed value (newly merged or spawned) */
        const game_state_t *st = game2048_get_state();
        for (int r = 0; r < GAME2048_SIZE; r++) {
            for (int c = 0; c < GAME2048_SIZE; c++) {
                if (st->board[r][c] != 0 && st->board[r][c] != st_before->board[r][c]) {
                    anim_tile_pop(s_tiles[r][c]);
                }
            }
        }

        /* Update AI hint with score info while preserving educational content */
        if (s_ai_box) {
            lv_label_set_text_fmt(s_ai_box,
                "太棒了!\n\n"
                "得分: %d  最高分: %d\n\n"
                "数学知识:\n"
                "2^1=2, 2^2=4, 2^3=8,\n"
                "2^4=16, 2^5=32...\n"
                "继续合并获得更高数字!",
                st->score, st->best_score);
        }

        /* Check game over */
        if (st->game_over) {
            game2048_ui_show_game_over();
        }
    }
}

static void on_restart_click(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Restart game");

    /* Remove overlay */
    if (s_game_over_overlay) {
        lv_obj_delete(s_game_over_overlay);
        s_game_over_overlay = NULL;
    }

    game2048_restart();
    refresh_all_tiles();
    refresh_score();

    /* Reset AI hint */
    if (s_ai_box) {
        lv_label_set_text(s_ai_box,
                "新游戏!\n\n"
                "滑动方块来移动.\n"
                "合并相同数字\n"
                "来成长. 达到2048!");
    }
}

/* end */