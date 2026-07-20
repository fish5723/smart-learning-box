/**
 * @file word_king.c
 * @brief 单词王者 — 逻辑层实现
 *
 * 内置 60 个常用小学英语单词（水果/文具/动物/校园/食物/颜色/自然/身体/家庭）。
 * 每题随机取一个单词，正确释义 + 3 个互异干扰释义打乱后作为四个选项。
 *
 * 本模块不含任何 LVGL 调用；状态由 UI 层只读访问。
 * 选项生成采用两级硬上限，绝不发生无限循环。
 */

#include "word_king.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "WORD_KING";

/* ═══════════════════════════════════════════════
   内置词库（60 词，方便后续扩展：直接往数组追加即可）
   ═══════════════════════════════════════════════ */
static const word_item_t s_bank[] = {
    /* 水果 */
    {"apple", "苹果"}, {"banana", "香蕉"}, {"orange", "橙子"}, {"pear", "梨"},
    {"grape", "葡萄"}, {"watermelon", "西瓜"},
    /* 文具 / 教室 */
    {"book", "书"}, {"pen", "钢笔"}, {"pencil", "铅笔"}, {"ruler", "尺子"},
    {"bag", "书包"}, {"desk", "书桌"}, {"chair", "椅子"},
    /* 动物 */
    {"cat", "猫"}, {"dog", "狗"}, {"pig", "猪"}, {"duck", "鸭子"},
    {"rabbit", "兔子"}, {"bird", "鸟"}, {"fish", "鱼"}, {"tiger", "老虎"},
    {"lion", "狮子"}, {"monkey", "猴子"}, {"elephant", "大象"},
    /* 校园 */
    {"school", "学校"}, {"teacher", "老师"}, {"student", "学生"},
    {"classroom", "教室"}, {"friend", "朋友"},
    /* 食物 / 饮品 */
    {"water", "水"}, {"milk", "牛奶"}, {"rice", "米饭"}, {"bread", "面包"},
    {"egg", "鸡蛋"}, {"food", "食物"},
    /* 颜色 */
    {"red", "红色"}, {"blue", "蓝色"}, {"green", "绿色"}, {"yellow", "黄色"},
    {"black", "黑色"}, {"white", "白色"},
    /* 自然 */
    {"sun", "太阳"}, {"moon", "月亮"}, {"star", "星星"}, {"sky", "天空"},
    {"tree", "树"}, {"flower", "花"}, {"grass", "草"}, {"rain", "雨"},
    {"wind", "风"},
    /* 身体 */
    {"hand", "手"}, {"foot", "脚"}, {"head", "头"}, {"eye", "眼睛"},
    {"mouth", "嘴"}, {"nose", "鼻子"}, {"ear", "耳朵"},
    /* 家庭 */
    {"mother", "妈妈"}, {"father", "爸爸"}, {"family", "家庭"},
};

#define BANK_COUNT ((int)(sizeof(s_bank) / sizeof(s_bank[0])))

/* ═══════════════════════════════════════════════
   内部状态
   ═══════════════════════════════════════════════ */
static word_game_state_t s_state;

/* 当前题目 */
static int         s_word_index  = 0;                     /* 当前单词在词库中的下标 */
static int         s_last_index  = -1;                    /* 上一题单词，避免连续重复 */
static const char *s_options[WORD_KING_OPTION_COUNT];     /* 四个中文释义 */
static int         s_answer_index = 0;                    /* 正确释义下标 */
static bool        s_seeded = false;

/* ── 前向声明 ── */
static void gen_question(void);
static bool index_used(const int *chosen, const bool *filled, int didx);

/* ═══════════════════════════════════════════════
   公共 API
   ═══════════════════════════════════════════════ */

void word_king_init(void)
{
    ESP_LOGI(TAG, "word_king_init() — 词库 %d 词", BANK_COUNT);
    memset(&s_state, 0, sizeof(s_state));
    s_state.level = 1;
    s_last_index  = -1;
}

void word_king_start(void)
{
    if (!s_seeded) {
        srand((unsigned)esp_timer_get_time());
        s_seeded = true;
    }

    ESP_LOGI(TAG, "word_king_start()");
    memset(&s_state, 0, sizeof(s_state));
    s_state.running = true;
    s_state.level   = 1;
    s_last_index    = -1;

    gen_question();
    ESP_LOGI(TAG, "word_king_start() done: word=%s ans_idx=%d",
             s_bank[s_word_index].word, s_answer_index);
}

void word_king_next(void)
{
    gen_question();
}

bool word_king_submit(int option_index)
{
    if (option_index < 0 || option_index >= WORD_KING_OPTION_COUNT) {
        return false;
    }

    bool correct = (option_index == s_answer_index);

    if (correct) {
        s_state.score   += 10;
        s_state.correct += 1;
        s_state.streak  += 1;
        s_state.level   += 1;   /* 进入下一关 */
        ESP_LOGI(TAG, "Correct! word=%s score=%d streak=%d",
                 s_bank[s_word_index].word, s_state.score, s_state.streak);
    } else {
        s_state.wrong  += 1;
        s_state.streak  = 0;    /* 连击清零，关卡不变（重新挑战） */
        ESP_LOGI(TAG, "Wrong. word=%s correct=%s chosen=%s",
                 s_bank[s_word_index].word, s_options[s_answer_index],
                 s_options[option_index]);
    }

    return correct;
}

const word_game_state_t *word_king_get_state(void) { return &s_state; }

const char *word_king_get_word(void)
{
    return s_bank[s_word_index].word;
}

const char *word_king_get_option(int index)
{
    if (index < 0 || index >= WORD_KING_OPTION_COUNT) return "";
    return s_options[index] ? s_options[index] : "";
}

int word_king_get_answer_index(void) { return s_answer_index; }

int word_king_get_word_count(void) { return BANK_COUNT; }

/* ═══════════════════════════════════════════════
   内部：题目 / 选项生成
   ═══════════════════════════════════════════════ */

/* 判断词库下标 didx 是否已被已填充选项占用 */
static bool index_used(const int *chosen, const bool *filled, int didx)
{
    for (int j = 0; j < WORD_KING_OPTION_COUNT; j++) {
        if (filled[j] && chosen[j] == didx) return true;
    }
    return false;
}

/*
 * 生成一题：随机选单词（避免与上题重复），
 * 正确释义随机落位，其余用互异的其它单词释义填充。
 * 两级硬上限保证绝不发生无限循环。
 */
static void gen_question(void)
{
    const int n = BANK_COUNT;

    /* ── 选单词（尽量不与上题相同） ── */
    int widx = 0;
    for (int tries = 0; tries < 8; tries++) {
        widx = rand() % n;
        if (widx != s_last_index) break;
    }
    s_last_index = widx;
    s_word_index = widx;

    /* ── 四个选项（词库下标） ── */
    int  chosen[WORD_KING_OPTION_COUNT];
    bool filled[WORD_KING_OPTION_COUNT] = { false };

    int ans_idx = rand() % WORD_KING_OPTION_COUNT;
    s_answer_index      = ans_idx;
    chosen[ans_idx]     = widx;
    filled[ans_idx]     = true;
    s_options[ans_idx]  = s_bank[widx].meaning;

    for (int i = 0; i < WORD_KING_OPTION_COUNT; i++) {
        if (filled[i]) continue;

        int  didx = 0;
        bool ok   = false;

        /* ① 随机尝试（硬上限 40 次） */
        for (int tries = 0; tries < 40 && !ok; tries++) {
            didx = rand() % n;
            ok = !index_used(chosen, filled, didx);
        }

        /* ② 兜底：确定性线性探测（硬上限 n 次，绝不无限循环） */
        if (!ok) {
            didx = (widx + 1) % n;
            for (int guard = 0; guard < n; guard++) {
                if (!index_used(chosen, filled, didx)) { ok = true; break; }
                didx = (didx + 1) % n;
            }
            ESP_LOGW(TAG, "gen_question[%d] used fallback probe -> didx=%d", i, didx);
        }

        chosen[i]    = didx;
        filled[i]    = true;
        s_options[i] = s_bank[didx].meaning;
    }

    ESP_LOGI(TAG, "gen_question: word=%s options=[%s,%s,%s,%s] ans_idx=%d",
             s_bank[widx].word, s_options[0], s_options[1],
             s_options[2], s_options[3], s_answer_index);
}
