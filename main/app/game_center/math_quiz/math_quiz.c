/**
 * @file math_quiz.c
 * @brief 数学答题赛 — 逻辑层实现
 *
 * 题目生成策略（难度随关卡递增）：
 *   关卡 1~3 : 纯加法
 *   关卡 4~6 : 加法 / 减法 随机
 *   关卡 7+  : 加 / 减 / 乘 随机
 * 加减法操作数范围随关卡放大（上限 100），乘法固定 1~20。
 *
 * 本模块不含任何 LVGL 调用；状态由 UI 层只读访问。
 */

#include "math_quiz.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "MATH_QUIZ";

/* 用户未作答哨兵值（避免与真实答案冲突） */
#define ANSWER_NONE  (-0x7FFFFFFF)

/* ═══════════════════════════════════════════════
   内部状态（逻辑模块拥有）
   ═══════════════════════════════════════════════ */
static math_quiz_state_t s_state;
static bool s_seeded = false;

/* ── 前向声明 ── */
static void gen_question(void);
static void gen_options(int answer, int type);
static int  range_max_for_level(int level);
static bool option_exists(const bool *filled, int cand);

/* ═══════════════════════════════════════════════
   公共 API
   ═══════════════════════════════════════════════ */

void math_quiz_init(void)
{
    ESP_LOGI(TAG, "math_quiz_init()");
    memset(&s_state, 0, sizeof(s_state));
    s_state.level = 1;
}

void math_quiz_start(void)
{
    if (!s_seeded) {
        srand((unsigned)esp_timer_get_time());
        s_seeded = true;
    }

    ESP_LOGI(TAG, "math_quiz_start()");
    memset(&s_state, 0, sizeof(s_state));
    s_state.running = true;
    s_state.level   = 1;

    gen_question();
    ESP_LOGI(TAG, "math_quiz_start() done: level=%d q=%d%s%d=%d",
             s_state.level, s_state.question.num1,
             (s_state.question.type == MATH_OP_ADD) ? "+" :
             (s_state.question.type == MATH_OP_SUB) ? "-" : "x",
             s_state.question.num2, s_state.question.answer);
}

void math_quiz_next(void)
{
    gen_question();
}

bool math_quiz_submit(int option_index)
{
    if (option_index < 0 || option_index >= MATH_QUIZ_OPTION_COUNT) {
        return false;
    }

    s_state.question.user_answer = s_state.options[option_index];
    bool correct = (option_index == s_state.answer_index);

    if (correct) {
        s_state.score  += 10;
        s_state.correct += 1;
        s_state.streak  += 1;
        s_state.level   += 1;   /* 进入下一关 */
        ESP_LOGI(TAG, "Correct! score=%d streak=%d -> level %d",
                 s_state.score, s_state.streak, s_state.level);
    } else {
        s_state.wrong  += 1;
        s_state.streak  = 0;    /* 连击清零，关卡不变（重新挑战） */
        ESP_LOGI(TAG, "Wrong. answer=%d chosen=%d",
                 s_state.question.answer, s_state.options[option_index]);
    }

    return correct;
}

const math_quiz_state_t *math_quiz_get_state(void)
{
    return &s_state;
}

/* ═══════════════════════════════════════════════
   内部：题目 / 选项生成
   ═══════════════════════════════════════════════ */

/* 加减法操作数上限：随关卡从 10 线性放大到 100 */
static int range_max_for_level(int level)
{
    int maxv = level * 10;
    if (maxv < 10)  maxv = 10;
    if (maxv > 100) maxv = 100;
    return maxv;
}

static void gen_question(void)
{
    int level = s_state.level;
    if (level < 1) level = 1;

    /* ── 选择运算类型（随关卡解锁） ── */
    int type;
    if (level <= 3) {
        type = MATH_OP_ADD;
    } else if (level <= 6) {
        type = (rand() % 2) ? MATH_OP_ADD : MATH_OP_SUB;
    } else {
        type = rand() % 3;   /* ADD / SUB / MUL */
    }

    int a = 0, b = 0, ans = 0;
    switch (type) {
    case MATH_OP_ADD: {
        int maxv = range_max_for_level(level);
        a = 1 + rand() % maxv;
        b = 1 + rand() % maxv;
        ans = a + b;
        break;
    }
    case MATH_OP_SUB: {
        int maxv = range_max_for_level(level);
        a = 1 + rand() % maxv;
        b = 1 + rand() % maxv;
        if (b > a) { int t = a; a = b; b = t; }  /* 保证结果非负 */
        ans = a - b;
        break;
    }
    case MATH_OP_MUL:
    default: {
        type = MATH_OP_MUL;
        a = 1 + rand() % 20;
        b = 1 + rand() % 20;
        ans = a * b;
        break;
    }
    }

    s_state.question.num1        = a;
    s_state.question.num2        = b;
    s_state.question.answer      = ans;
    s_state.question.user_answer = ANSWER_NONE;
    s_state.question.type        = type;
    s_state.question.level       = level;

    ESP_LOGI(TAG, "gen_question: level=%d type=%d %d op %d = %d", level, type, a, b, ans);

    gen_options(ans, type);
}

/* 判断候选值是否已存在于已填充选项中 */
static bool option_exists(const bool *filled, int cand)
{
    for (int j = 0; j < MATH_QUIZ_OPTION_COUNT; j++) {
        if (filled[j] && s_state.options[j] == cand) return true;
    }
    return false;
}

/*
 * 生成 4 个互异选项：正确答案放在随机下标，其余用邻近干扰项填充。
 * 干扰项取 answer ± delta；两级硬上限保证绝不发生无限循环：
 *   1) 随机尝试最多 30 次；
 *   2) 失败后确定性线性探测最多 64 次（一定能找到互异值）。
 */
static void gen_options(int answer, int type)
{
    ESP_LOGI(TAG, "gen_options: answer=%d type=%d", answer, type);

    bool filled[MATH_QUIZ_OPTION_COUNT] = { false };

    int ans_idx = rand() % MATH_QUIZ_OPTION_COUNT;
    s_state.answer_index     = ans_idx;
    s_state.options[ans_idx] = answer;
    filled[ans_idx]          = true;

    /* 干扰项偏移幅度：乘法结果跨度大，用更大扰动 */
    int spread = (type == MATH_OP_MUL) ? 12 : 9;

    for (int i = 0; i < MATH_QUIZ_OPTION_COUNT; i++) {
        if (filled[i]) continue;

        int  cand = answer + 1;
        bool ok   = false;

        /* ① 随机尝试（硬上限 30 次） */
        for (int tries = 0; tries < 30 && !ok; tries++) {
            int sign  = (rand() % 2) ? 1 : -1;
            int delta = sign * (1 + rand() % spread);
            cand = answer + delta;
            if (cand < 0) cand = answer + 1 + (rand() % spread);
            ok = !option_exists(filled, cand);
        }

        /* ② 兜底：确定性线性探测（硬上限 64 次，绝不无限循环） */
        if (!ok) {
            cand = (answer >= 0) ? answer + 1 : 1;
            for (int guard = 0; guard < 64; guard++) {
                if (!option_exists(filled, cand)) { ok = true; break; }
                cand++;
            }
            ESP_LOGW(TAG, "gen_options[%d] used fallback probe -> cand=%d ok=%d",
                     i, cand, (int)ok);
        }

        s_state.options[i] = cand;
        filled[i] = true;
    }

    ESP_LOGI(TAG, "gen_options done: [%d, %d, %d, %d] ans_idx=%d",
             s_state.options[0], s_state.options[1],
             s_state.options[2], s_state.options[3], s_state.answer_index);
}
