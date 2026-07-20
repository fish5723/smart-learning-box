/**
 * @file math_quiz_ui.c
 * @brief 数学答题赛 — LVGL 9.x 页面
 *
 * 以 game2048_ui.c 为模板，遵循 UI_DESIGN_SYSTEM.md Dark Theme：
 *   - Header(60): 返回按钮 + 金色标题 + 关卡状态
 *   - Main(flex:1): 280px 侧栏(关卡/战绩/AI提示) + 题目面板
 *   - 题目面板: “第 N 关” + 大号题目卡 + 反馈 + 2×2 答案按钮
 *   - Footer(80): 学习任务 + 奖励
 *
 * 线程模型：屏幕懒加载于首次 show()（由游戏中心按钮事件触发，
 * 已处于 LVGL task 线程），答题反馈延时通过一次性 lv_timer 实现，
 * 全程无阻塞 delay。
 */

#include "math_quiz_ui.h"
#include "math_quiz.h"
#include "game_center.h"
#include "app/icon_loader/icon_loader.h"
#include "app/achievement/achievement.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "MATH_QUIZ_UI";

/* ── 外部 CJK 字体（font_loader.c） ── */
extern lv_font_t *g_font_cjk_14;
extern lv_font_t *g_font_cjk_16;
extern lv_font_t *g_font_cjk_20;
extern lv_font_t *g_font_cjk_24;

/* ═══════════════════════════════════════════════
   颜色（对齐 UI_DESIGN_SYSTEM.md §2）
   ═══════════════════════════════════════════════ */
#define COLOR_BG              lv_color_hex(0x0F172A)
#define COLOR_CARD            lv_color_hex(0x1E293B)
#define COLOR_BORDER          lv_color_hex(0x334155)
#define COLOR_TEXT_PRIMARY    lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_SECONDARY  lv_color_hex(0x94A3B8)
#define COLOR_GOLD            lv_color_hex(0xFBBF24)
#define COLOR_SUCCESS         lv_color_hex(0x10B981)
#define COLOR_DANGER          lv_color_hex(0xEF4444)
#define COLOR_BLUE_ACCENT     lv_color_hex(0x60A5FA)
#define COLOR_AI_TEXT         lv_color_hex(0xCBD5E1)
#define COLOR_QCARD_BG        lv_color_hex(0x334155)
#define COLOR_BTN_BLUE        lv_color_hex(0x2563EB)

/* ═══════════════════════════════════════════════
   布局常量（1024×600）
   ═══════════════════════════════════════════════ */
#define SCREEN_W       1024
#define SCREEN_H       600
#define CONTAINER_PAD  16
#define ITEM_GAP       12
#define HEADER_H       60
#define SIDEBAR_W      280
#define FOOTER_H       80
#define ANSWER_BTN_H   68

/* 反馈显示时长（毫秒） */
#define FEEDBACK_MS_OK    650
#define FEEDBACK_MS_WRONG 1200

/* ═══════════════════════════════════════════════
   UI 句柄
   ═══════════════════════════════════════════════ */
static lv_obj_t *s_screen        = NULL;
static lv_obj_t *s_level_label    = NULL;   /* 侧栏关卡数值 */
static lv_obj_t *s_score_label    = NULL;   /* 侧栏得分 */
static lv_obj_t *s_streak_label   = NULL;   /* 侧栏连对 */
static lv_obj_t *s_stat_label     = NULL;   /* 侧栏 对/错 */
static lv_obj_t *s_ai_box         = NULL;   /* AI 提示 */
static lv_obj_t *s_q_level_label  = NULL;   /* 题目面板 “第 N 关” */
static lv_obj_t *s_question_label = NULL;   /* 大号题目 */
static lv_obj_t *s_feedback_label = NULL;   /* 反馈提示 */
static lv_obj_t *s_answer_btns[MATH_QUIZ_OPTION_COUNT]   = { NULL };
static lv_obj_t *s_answer_labels[MATH_QUIZ_OPTION_COUNT] = { NULL };

static lv_timer_t *s_pending_timer = NULL;  /* 一次性推进定时器 */
static bool s_locked = false;               /* 反馈期间锁定输入 */
static bool s_first_clear_awarded = false;  /* 本局是否已发首通奖励 */

/* ═══════════════════════════════════════════════
   前向声明
   ═══════════════════════════════════════════════ */
static void build_screen(void);
static void create_header(lv_obj_t *parent);
static void create_main_area(lv_obj_t *parent);
static void create_sidebar(lv_obj_t *parent);
static void create_question_panel(lv_obj_t *parent);
static lv_obj_t *create_answer_button(lv_obj_t *parent, int idx);
static void create_footer(lv_obj_t *parent);

static void render_question(void);
static void refresh_sidebar(void);
static void set_button_color(int idx, lv_color_t c);

static void on_back_click(lv_event_t *e);
static void on_answer_click(lv_event_t *e);
static void schedule_advance(uint32_t delay_ms);
static void cancel_pending(void);
static void advance_timer_cb(lv_timer_t *t);

static const char *op_str(int type);

/* ═══════════════════════════════════════════════
   公共 API
   ═══════════════════════════════════════════════ */

void math_quiz_ui_init(void)
{
    ESP_LOGI(TAG, "math_quiz_ui_init() — 懒加载");
    /* 屏幕延迟到首次 show() 才创建（此时字体/图标已就绪） */
}

void math_quiz_ui_show(void)
{
    ESP_LOGI(TAG, "math_quiz_ui_show() ENTER (s_screen=%p)", (void *)s_screen);

    if (!s_screen) {
        ESP_LOGI(TAG, "  -> build_screen (first show)");
        build_screen();
        ESP_LOGI(TAG, "  -> build_screen OK (s_screen=%p)", (void *)s_screen);
    }

    cancel_pending();
    s_locked = false;
    s_first_clear_awarded = false;

    ESP_LOGI(TAG, "  -> math_quiz_start");
    math_quiz_start();

    ESP_LOGI(TAG, "  -> render_question");
    render_question();

    ESP_LOGI(TAG, "  -> refresh_sidebar");
    refresh_sidebar();

    ESP_LOGI(TAG, "  -> lv_screen_load");
    lv_screen_load(s_screen);

    ESP_LOGI(TAG, "math_quiz_ui_show() DONE");
}

void math_quiz_ui_hide(void)
{
    cancel_pending();
    /* 屏幕对象保留，由 lv_screen_load 切换 */
}

/* ═══════════════════════════════════════════════
   屏幕构建
   ═══════════════════════════════════════════════ */

static void build_screen(void)
{
    ESP_LOGI(TAG, "build_screen: create screen");
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_pad_all(s_screen, CONTAINER_PAD, 0);
    lv_obj_set_style_pad_gap(s_screen, ITEM_GAP, 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    ESP_LOGI(TAG, "build_screen: header");
    create_header(s_screen);
    ESP_LOGI(TAG, "build_screen: main area");
    create_main_area(s_screen);
    ESP_LOGI(TAG, "build_screen: footer");
    create_footer(s_screen);
    ESP_LOGI(TAG, "build_screen: DONE");
}

/* ── Header ── */
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
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "数学答题赛");
    lv_obj_set_style_text_color(title, COLOR_GOLD, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_24, 0);

    /* 状态 */
    lv_obj_t *status = lv_label_create(header);
    lv_label_set_text(status, "小学数学闯关");
    lv_obj_set_style_text_color(status, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status, g_font_cjk_14, 0);
}

/* ── Main area ── */
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

    ESP_LOGI(TAG, "  main: sidebar");
    create_sidebar(main);
    ESP_LOGI(TAG, "  main: question panel");
    create_question_panel(main);
    ESP_LOGI(TAG, "  main: DONE");
}

/* ── Sidebar ── */
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

    /* Card 1: 当前关卡 */
    lv_obj_t *level_card = lv_obj_create(sidebar);
    lv_obj_set_size(level_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(level_card, COLOR_CARD, 0);
    lv_obj_set_style_radius(level_card, 16, 0);
    lv_obj_set_style_pad_all(level_card, 16, 0);
    lv_obj_set_style_border_width(level_card, 0, 0);
    lv_obj_set_flex_flow(level_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(level_card, 4, 0);
    lv_obj_clear_flag(level_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lc_title = lv_label_create(level_card);
    lv_label_set_text(lc_title, "当前关卡");
    lv_obj_set_style_text_color(lc_title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lc_title, g_font_cjk_14, 0);

    s_level_label = lv_label_create(level_card);
    lv_label_set_text(s_level_label, "第 1 关");
    lv_obj_set_style_text_color(s_level_label, COLOR_BLUE_ACCENT, 0);
    lv_obj_set_style_text_font(s_level_label, g_font_cjk_24, 0);

    lv_obj_t *lc_hint = lv_label_create(level_card);
    lv_label_set_text(lc_hint, "答对进入下一关");
    lv_obj_set_style_text_color(lc_hint, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(lc_hint, g_font_cjk_14, 0);

    /* Card 2: 本局战绩 */
    lv_obj_t *score_card = lv_obj_create(sidebar);
    lv_obj_set_size(score_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(score_card, COLOR_CARD, 0);
    lv_obj_set_style_radius(score_card, 16, 0);
    lv_obj_set_style_pad_all(score_card, 16, 0);
    lv_obj_set_style_border_width(score_card, 0, 0);
    lv_obj_set_flex_flow(score_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(score_card, 4, 0);
    lv_obj_clear_flag(score_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sc_title = lv_label_create(score_card);
    lv_label_set_text(sc_title, "本局得分");
    lv_obj_set_style_text_color(sc_title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(sc_title, g_font_cjk_14, 0);

    s_score_label = lv_label_create(score_card);
    lv_label_set_text(s_score_label, "0");
    lv_obj_set_style_text_color(s_score_label, COLOR_GOLD, 0);
    lv_obj_set_style_text_font(s_score_label, g_font_cjk_24, 0);

    s_streak_label = lv_label_create(score_card);
    lv_label_set_text(s_streak_label, "连对: 0");
    lv_obj_set_style_text_color(s_streak_label, COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(s_streak_label, g_font_cjk_16, 0);

    s_stat_label = lv_label_create(score_card);
    lv_label_set_text(s_stat_label, "对 0 / 错 0");
    lv_obj_set_style_text_color(s_stat_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_stat_label, g_font_cjk_14, 0);

    /* Card 3: AI 老师提示 */
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

    lv_obj_t *ai_title_row = lv_obj_create(ai_card);
    lv_obj_set_size(ai_title_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ai_title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ai_title_row, 0, 0);
    lv_obj_set_style_pad_all(ai_title_row, 0, 0);
    lv_obj_set_style_pad_gap(ai_title_row, 6, 0);
    lv_obj_set_flex_flow(ai_title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ai_title_row,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ai_title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ai_icon = icon_loader_create_image(ai_title_row, ICON_ROBOT, 18, 18);
    LV_UNUSED(ai_icon);

    lv_obj_t *ai_title = lv_label_create(ai_title_row);
    lv_label_set_text(ai_title, "AI老师");
    lv_obj_set_style_text_color(ai_title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(ai_title, g_font_cjk_14, 0);

    s_ai_box = lv_label_create(ai_card);
    lv_label_set_long_mode(s_ai_box, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ai_box, SIDEBAR_W - 32);
    lv_label_set_text(s_ai_box,
        "欢迎来到数学答题赛!\n\n"
        "看清题目, 从四个\n"
        "选项中选出正确答案.\n\n"
        "答对得 10 分并连击,\n"
        "连对越多越厉害!");
    lv_obj_set_style_text_color(s_ai_box, COLOR_AI_TEXT, 0);
    lv_obj_set_style_text_font(s_ai_box, g_font_cjk_14, 0);
}

/* ── Question panel ── */
static void create_question_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, 0, LV_PCT(100));
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_style_bg_color(panel, COLOR_CARD, 0);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_set_style_pad_gap(panel, 14, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* 关卡标题 */
    s_q_level_label = lv_label_create(panel);
    lv_label_set_text(s_q_level_label, "第 1 关");
    lv_obj_set_style_text_color(s_q_level_label, COLOR_BLUE_ACCENT, 0);
    lv_obj_set_style_text_font(s_q_level_label, g_font_cjk_20, 0);

    /* 题目卡片 */
    lv_obj_t *q_card = lv_obj_create(panel);
    lv_obj_set_size(q_card, LV_PCT(100), 110);
    lv_obj_set_style_bg_color(q_card, COLOR_QCARD_BG, 0);
    lv_obj_set_style_radius(q_card, 16, 0);
    lv_obj_set_style_border_width(q_card, 0, 0);
    lv_obj_set_style_pad_all(q_card, 8, 0);
    lv_obj_set_flex_flow(q_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(q_card,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(q_card, LV_OBJ_FLAG_SCROLLABLE);

    s_question_label = lv_label_create(q_card);
    lv_label_set_text(s_question_label, "0 + 0 = ?");
    lv_obj_set_style_text_color(s_question_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(s_question_label, g_font_cjk_24, 0);

    /* 反馈提示（固定高度以稳定布局） */
    s_feedback_label = lv_label_create(panel);
    lv_label_set_text(s_feedback_label, "");
    lv_obj_set_style_text_color(s_feedback_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_feedback_label, g_font_cjk_20, 0);
    lv_obj_set_height(s_feedback_label, 28);

    /* 答案区：两行各两个按钮 */
    lv_obj_t *answer_area = lv_obj_create(panel);
    lv_obj_set_size(answer_area, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(answer_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(answer_area, 0, 0);
    lv_obj_set_style_pad_all(answer_area, 0, 0);
    lv_obj_set_style_pad_gap(answer_area, 12, 0);
    lv_obj_set_flex_flow(answer_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(answer_area, LV_OBJ_FLAG_SCROLLABLE);

    for (int r = 0; r < 2; r++) {
        lv_obj_t *row = lv_obj_create(answer_area);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_gap(row, 12, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        for (int c = 0; c < 2; c++) {
            int idx = r * 2 + c;
            create_answer_button(row, idx);
        }
    }
}

static lv_obj_t *create_answer_button(lv_obj_t *parent, int idx)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_size(btn, 0, ANSWER_BTN_H);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_bg_color(btn, COLOR_BTN_BLUE, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, on_answer_click, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "0");
    lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_26, 0);
    lv_obj_center(label);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_answer_btns[idx]   = btn;
    s_answer_labels[idx] = label;
    return btn;
}

/* ── Footer ── */
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
    lv_label_set_text(mission, "学习任务: 熟练加减乘法口算");
    lv_obj_set_style_text_color(mission, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(mission, g_font_cjk_16, 0);

    lv_obj_t *reward = lv_label_create(footer);
    lv_label_set_text(reward, "答对奖励 +30积分");
    lv_obj_set_style_text_color(reward, COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(reward, g_font_cjk_20, 0);
}

/* ═══════════════════════════════════════════════
   渲染 / 刷新
   ═══════════════════════════════════════════════ */

static const char *op_str(int type)
{
    switch (type) {
    case MATH_OP_ADD: return "+";
    case MATH_OP_SUB: return "-";
    case MATH_OP_MUL:
    default:          return "×";
    }
}

static void render_question(void)
{
    const math_quiz_state_t *st = math_quiz_get_state();
    ESP_LOGI(TAG, "render_question: level=%d %d..%d ans_idx=%d",
             st->question.level, st->question.num1, st->question.num2,
             st->answer_index);

    if (s_q_level_label) {
        lv_label_set_text_fmt(s_q_level_label, "第 %d 关", st->question.level);
    }
    if (s_question_label) {
        lv_label_set_text_fmt(s_question_label, "%d %s %d = ?",
            st->question.num1, op_str(st->question.type), st->question.num2);
    }
    if (s_feedback_label) {
        lv_label_set_text(s_feedback_label, "");
    }

    for (int i = 0; i < MATH_QUIZ_OPTION_COUNT; i++) {
        if (s_answer_labels[i]) {
            lv_label_set_text_fmt(s_answer_labels[i], "%d", st->options[i]);
        }
        set_button_color(i, COLOR_BTN_BLUE);
    }

    s_locked = false;
    ESP_LOGI(TAG, "render_question: DONE");
}

static void refresh_sidebar(void)
{
    const math_quiz_state_t *st = math_quiz_get_state();
    if (s_level_label)  lv_label_set_text_fmt(s_level_label, "第 %d 关", st->level);
    if (s_score_label)  lv_label_set_text_fmt(s_score_label, "%d", st->score);
    if (s_streak_label) lv_label_set_text_fmt(s_streak_label, "连对: %d", st->streak);
    if (s_stat_label)   lv_label_set_text_fmt(s_stat_label, "对 %d / 错 %d",
                                              st->correct, st->wrong);
}

static void set_button_color(int idx, lv_color_t c)
{
    if (idx >= 0 && idx < MATH_QUIZ_OPTION_COUNT && s_answer_btns[idx]) {
        lv_obj_set_style_bg_color(s_answer_btns[idx], c, 0);
    }
}

/* ═══════════════════════════════════════════════
   事件处理
   ═══════════════════════════════════════════════ */

static void on_back_click(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Back to game center");
    cancel_pending();
    game_center_show();
}

static void on_answer_click(lv_event_t *e)
{
    if (s_locked) return;

    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_locked = true;

    const math_quiz_state_t *st = math_quiz_get_state();
    int answer_index = st->answer_index;

    bool correct = math_quiz_submit(idx);

    if (correct) {
        set_button_color(idx, COLOR_SUCCESS);
        if (s_feedback_label) {
            lv_obj_set_style_text_color(s_feedback_label, COLOR_SUCCESS, 0);
            lv_label_set_text(s_feedback_label, "回答正确!  +10 分");
        }

        /* ── 成就接入 ── */
        achievement_complete_task(ACHV_TASK_QUESTION, 1);   /* +10 EXP, 题数+1, NVS */
        const math_quiz_state_t *now = math_quiz_get_state();
        if (!s_first_clear_awarded) {
            achievement_update(30);                          /* 首次通关 +30 EXP */
            s_first_clear_awarded = true;
        }
        if (now->streak > 0 && (now->streak % 5) == 0) {
            achievement_update(10);                          /* 连对 5 题 +10 EXP */
            if (s_ai_box) {
                lv_label_set_text_fmt(s_ai_box,
                    "太棒了! 连对 %d 题!\n\n"
                    "奖励额外 +10 经验.\n"
                    "保持专注, 继续冲!",
                    now->streak);
            }
        } else if (s_ai_box) {
            lv_label_set_text_fmt(s_ai_box,
                "答对啦! 得分 %d.\n\n"
                "当前连对 %d 题,\n"
                "进入第 %d 关.\n"
                "越往后题目越有挑战!",
                now->score, now->streak, now->level);
        }

        schedule_advance(FEEDBACK_MS_OK);
    } else {
        set_button_color(idx, COLOR_DANGER);
        set_button_color(answer_index, COLOR_SUCCESS);
        if (s_feedback_label) {
            lv_obj_set_style_text_color(s_feedback_label, COLOR_DANGER, 0);
            lv_label_set_text_fmt(s_feedback_label, "答错了  正确答案: %d",
                st->question.answer);
        }
        if (s_ai_box) {
            lv_label_set_text_fmt(s_ai_box,
                "不要灰心!\n\n"
                "正确答案是 %d.\n"
                "连击已清零,\n"
                "再挑战一题吧!",
                st->question.answer);
        }
        schedule_advance(FEEDBACK_MS_WRONG);
    }

    refresh_sidebar();
}

/* ═══════════════════════════════════════════════
   反馈延时推进（一次性 lv_timer，无阻塞 delay）
   ═══════════════════════════════════════════════ */

static void schedule_advance(uint32_t delay_ms)
{
    cancel_pending();
    s_pending_timer = lv_timer_create(advance_timer_cb, delay_ms, NULL);
    lv_timer_set_repeat_count(s_pending_timer, 1);  /* 触发一次后自动删除 */
}

static void cancel_pending(void)
{
    if (s_pending_timer) {
        lv_timer_delete(s_pending_timer);
        s_pending_timer = NULL;
    }
}

static void advance_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_pending_timer = NULL;   /* repeat_count=1: LVGL 触发后自动删除, 此处仅置空 */

    math_quiz_next();
    render_question();
    refresh_sidebar();
}
