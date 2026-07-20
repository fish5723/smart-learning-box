/**
 * @file word_king.h
 * @brief 单词王者 — 英语单词记忆挑战 逻辑层 API
 *
 * 纯逻辑模块（不含任何 LVGL 调用）：
 *   - 内置词库（60 个常用小学英语单词，方便后续扩展）
 *   - 四选一选项生成（正确中文释义 + 3 个干扰释义）
 *   - 答题判定与积分 / 连击统计
 *
 * 状态由 UI 层通过 word_king_get_state() 只读访问，
 * 当前题目通过 word_king_get_word() / word_king_get_option() 读取。
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 每题选项数量 */
#define WORD_KING_OPTION_COUNT 4

/** @brief 词库条目 */
typedef struct {
    const char *word;      /* 英文单词 */
    const char *meaning;   /* 中文释义 */
} word_item_t;

/** @brief 游戏状态（UI 层只读共享） */
typedef struct {
    bool running;   /* 是否进行中 */
    int  level;     /* 当前关卡（从 1 起） */
    int  score;     /* 本局得分（每题 +10） */
    int  correct;   /* 累计答对 */
    int  wrong;     /* 累计答错 */
    int  streak;    /* 当前连对 */
} word_game_state_t;

/**
 * @brief 初始化模块（清零状态，不生成题目）
 */
void word_king_init(void);

/**
 * @brief 开始新一局（重置得分/连击/关卡，并生成第 1 题）
 */
void word_king_start(void);

/**
 * @brief 生成下一题（新单词 + 四个中文释义选项）
 */
void word_king_next(void);

/**
 * @brief 提交一个选项作为答案。
 *
 * @param option_index  用户点击的选项下标 [0, WORD_KING_OPTION_COUNT)
 * @return true  答对（score+10, streak+1, level+1），
 * @return false 答错（wrong+1, streak 清零，关卡不变）。
 */
bool word_king_submit(int option_index);

/**
 * @brief 获取只读状态指针。
 */
const word_game_state_t *word_king_get_state(void);

/**
 * @brief 获取当前题目的英文单词。
 */
const char *word_king_get_word(void);

/**
 * @brief 获取指定选项的中文释义。
 * @param index [0, WORD_KING_OPTION_COUNT)
 */
const char *word_king_get_option(int index);

/**
 * @brief 获取正确释义在选项中的下标。
 */
int word_king_get_answer_index(void);

/**
 * @brief 获取内置词库大小。
 */
int word_king_get_word_count(void);

#ifdef __cplusplus
}
#endif
