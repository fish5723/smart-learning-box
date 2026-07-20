/**
 * @file game2048.c
 * @brief Math Adventure 2048 - game logic implementation
 *
 * Core 2048 mechanics: slide, merge, spawn, win/lose detection.
 * Runs in the main logic task; state is read by the UI task via
 * game2048_get_state() and updated via lv_async_call notifications.
 */

#include "game2048.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "GAME2048";

/* ═══════════════════════════════════════════════
   Internal game state (owned by logic module)
   ═══════════════════════════════════════════════ */
static game_state_t s_state;

/* ── Forward declarations ── */
static void add_random_tile(void);
static bool can_move(void);
static bool slide_row(int row[4], int *score_add);

/* ═══════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════ */

void game2048_init(void)
{
    ESP_LOGI(TAG, "game2048_init()");
    memset(&s_state, 0, sizeof(s_state));
    s_state.best_score = 0;
}

void game2048_start(void)
{
    ESP_LOGI(TAG, "game2048_start()");
    memset(&s_state, 0, sizeof(s_state));
    s_state.game_over = false;
    s_state.won       = false;

    add_random_tile();
    add_random_tile();
}

void game2048_restart(void)
{
    ESP_LOGI(TAG, "game2048_restart()");
    int best = s_state.best_score;
    memset(&s_state, 0, sizeof(s_state));
    s_state.best_score = best;
    s_state.game_over  = false;
    s_state.won        = false;

    add_random_tile();
    add_random_tile();
}

bool game2048_move(game2048_direction_t dir)
{
    if (s_state.game_over) return false;

    int  new_board[4][4];
    memcpy(new_board, s_state.board, sizeof(new_board));

    bool moved     = false;
    int  score_add = 0;

    for (int i = 0; i < 4; i++) {
        int row[4];
        int r, c;

        switch (dir) {
        case GAME2048_UP:
            row[0] = s_state.board[0][i]; row[1] = s_state.board[1][i];
            row[2] = s_state.board[2][i]; row[3] = s_state.board[3][i];
            if (slide_row(row, &score_add)) {
                moved = true;
                for (r = 0; r < 4; r++) new_board[r][i] = row[r];
            }
            break;

        case GAME2048_DOWN:
            row[0] = s_state.board[3][i]; row[1] = s_state.board[2][i];
            row[2] = s_state.board[1][i]; row[3] = s_state.board[0][i];
            if (slide_row(row, &score_add)) {
                moved = true;
                for (r = 0; r < 4; r++) new_board[3 - r][i] = row[r];
            }
            break;

        case GAME2048_LEFT:
            for (c = 0; c < 4; c++) row[c] = s_state.board[i][c];
            if (slide_row(row, &score_add)) {
                moved = true;
                for (c = 0; c < 4; c++) new_board[i][c] = row[c];
            }
            break;

        case GAME2048_RIGHT:
            for (c = 0; c < 4; c++) row[c] = s_state.board[i][3 - c];
            if (slide_row(row, &score_add)) {
                moved = true;
                for (c = 0; c < 4; c++) new_board[i][3 - c] = row[c];
            }
            break;
        }
    }

    if (moved) {
        memcpy(s_state.board, new_board, sizeof(s_state.board));
        s_state.score += score_add;

        /* Track best score */
        if (s_state.score > s_state.best_score) {
            s_state.best_score = s_state.score;
        }

        /* Check for 2048 win */
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                if (s_state.board[r][c] >= 2048) {
                    s_state.won = true;
                }
            }
        }

        add_random_tile();

        if (!can_move()) {
            s_state.game_over = true;
            ESP_LOGI(TAG, "Game over! Final score: %d", s_state.score);
        }
    }

    return moved;
}

const game_state_t *game2048_get_state(void)
{
    return &s_state;
}

int game2048_get_best_score(void)
{
    return s_state.best_score;
}

/* ═══════════════════════════════════════════════
   Internal helpers
   ═══════════════════════════════════════════════ */

static bool slide_row(int row[4], int *score_add)
{
    /* Step 1: compact non-zero values */
    int temp[4] = {0};
    int idx = 0;
    for (int i = 0; i < 4; i++) {
        if (row[i] != 0) temp[idx++] = row[i];
    }

    /* Step 2: merge adjacent equal tiles */
    for (int i = 0; i < 3; i++) {
        if (temp[i] != 0 && temp[i] == temp[i + 1]) {
            temp[i] *= 2;
            *score_add += temp[i];
            temp[i + 1] = 0;
            i++; /* skip the zeroed tile */
        }
    }

    /* Step 3: compact again after merge */
    int result[4] = {0};
    idx = 0;
    for (int i = 0; i < 4; i++) {
        if (temp[i] != 0) result[idx++] = temp[i];
    }

    /* Step 4: detect change */
    bool changed = false;
    for (int i = 0; i < 4; i++) {
        if (row[i] != result[i]) {
            changed = true;
            row[i]  = result[i];
        }
    }

    return changed;
}

static void add_random_tile(void)
{
    int empty[16][2];
    int count = 0;

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (s_state.board[r][c] == 0) {
                empty[count][0] = r;
                empty[count][1] = c;
                count++;
            }
        }
    }

    if (count > 0) {
        int idx = rand() % count;
        int r   = empty[idx][0];
        int c   = empty[idx][1];
        /* 90% chance of 2, 10% chance of 4 */
        s_state.board[r][c] = (rand() % 10 < 9) ? 2 : 4;
    }
}

static bool can_move(void)
{
    /* Has empty cell? */
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (s_state.board[r][c] == 0) return true;
        }
    }

    /* Has adjacent equal tiles? */
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int val = s_state.board[r][c];
            if (c < 3 && s_state.board[r][c + 1] == val) return true;
            if (r < 3 && s_state.board[r + 1][c] == val) return true;
        }
    }

    return false;
}
