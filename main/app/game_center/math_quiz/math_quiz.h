/**
 * @file math_quiz.h
 * @brief 数学答题赛 — 小学数学闯关答题 逻辑层 API
 *
 * 纯逻辑模块（不含任何 LVGL 调用）：
 *   - 题目生成（加 / 减 / 乘，难度随关卡递增）
 *   - 四选一选项生成（正确答案 + 3 个干扰项）
 *   - 答题判定与积分 / 连击统计
 *
 * 状态由 UI 层通过 math_quiz_get_state() 只读访问，
 * 通过 math_quiz_submit() / math_quiz_next() 驱动更新。
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 每题选项数量 */
#define MATH_QUIZ_OPTION_COUNT 4

/** @brief 运算类型 */
typedef enum {
    MATH_OP_ADD = 0,   /* 加法 1~100 + 1~100 */
    MATH_OP_SUB = 1,   /* 减法 1~100 - 1~100（结果非负） */
    MATH_OP_MUL = 2,   /* 乘法 1~20 × 1~20 */
} math_op_t;

/** @brief 单道题目 */
typedef struct {
    int num1;          /* 第一个操作数 */
    int num2;          /* 第二个操作数 */
    int answer;        /* 正确答案 */
    int user_answer;   /* 用户所选答案（未作答为 INT 哨兵） */
    int type;          /* 运算类型 math_op_t */
    int level;         /* 该题所属关卡 */
} math_question_t;

/** @brief 游戏状态（UI 层只读共享） */
typedef struct {
    bool running;                            /* 是否进行中 */
    int  level;                              /* 当前关卡（从 1 起） */
    int  score;                              /* 本局得分（每题 +10） */
    int  correct;                            /* 累计答对 */
    int  wrong;                              /* 累计答错 */
    int  streak;                             /* 当前连对 */
    math_question_t question;                /* 当前题目 */

    /* ── 供 UI 展示的选项（正确答案 + 干扰项，已打乱顺序） ── */
    int  options[MATH_QUIZ_OPTION_COUNT];
    int  answer_index;                       /* 正确答案在 options 中的下标 */
} math_quiz_state_t;

/**
 * @brief 初始化模块（清零状态，不生成题目）
 */
void math_quiz_init(void);

/**
 * @brief 开始新一局（重置得分/连击/关卡，并生成第 1 题）
 */
void math_quiz_start(void);

/**
 * @brief 按当前关卡生成下一题（含选项）
 *
 * 答对后由 UI 调用以进入新题（关卡已在 submit 中 +1）；
 * 答错后调用则在同一关卡重新出题（“重新挑战”）。
 */
void math_quiz_next(void);

/**
 * @brief 提交一个选项作为答案。
 *
 * @param option_index  用户点击的选项下标 [0, MATH_QUIZ_OPTION_COUNT)
 * @return true  答对（score+10, streak+1, level+1），
 * @return false 答错（wrong+1, streak 清零，关卡不变）。
 */
bool math_quiz_submit(int option_index);

/**
 * @brief 获取只读状态指针。
 */
const math_quiz_state_t *math_quiz_get_state(void);

#ifdef __cplusplus
}
#endif
