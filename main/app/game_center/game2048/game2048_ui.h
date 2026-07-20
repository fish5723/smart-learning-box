/**
 * @file game2048_ui.h
 * @brief 数学冒险 2048 游戏 UI 接口
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void game2048_ui_init(void);
void game2048_ui_show(void);
void game2048_ui_hide(void);

/**
 * @brief 更新游戏板显示
 */
void game2048_ui_update_board(void);

/**
 * @brief 更新分数显示
 */
void game2048_ui_update_score(int score);

/**
 * @brief 显示游戏结束
 */
void game2048_ui_show_game_over(void);

#ifdef __cplusplus
}
#endif