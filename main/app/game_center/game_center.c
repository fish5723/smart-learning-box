/**
 * @file game_center.c
 * @brief 游戏中心模块 — 业务逻辑与页面管理
 *
 * 职责：
 *   - 管理游戏中心页面生命周期
 *   - 处理游戏入口点击事件
 *   - 调用各游戏子模块接口
 */

#include "game_center.h"
#include "game_center_ui.h"
#include "game2048/game2048.h"
#include "game2048/game2048_ui.h"
#include "math_quiz/math_quiz.h"
#include "math_quiz/math_quiz_ui.h"
#include "word_king/word_king.h"
#include "word_king/word_king_ui.h"
#include "more_games/more_games.h"
#include "esp_log.h"

static const char *TAG = "GAME_CENTER";

void game_center_init(void)
{
    ESP_LOGI(TAG, "game_center_init() — deferred");

    /* 游戏子模块延迟到首次进入游戏中心时初始化 */
    game_center_ui_init();

    /* 初始化 2048 游戏 UI */
    game2048_ui_init();

    /* 初始化 数学答题赛 UI（懒加载屏幕） */
    math_quiz_ui_init();

    /* 初始化 单词王者 UI（懒加载屏幕） */
    word_king_ui_init();
}

void game_center_show(void)
{
    ESP_LOGI(TAG, "game_center_show()");
    game_center_ui_show();
}

void game_center_hide(void)
{
    ESP_LOGI(TAG, "game_center_hide()");
    game_center_ui_hide();
}