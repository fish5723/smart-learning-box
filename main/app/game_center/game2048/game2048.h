/**
 * @file game2048.h
 * @brief Math Adventure 2048 - game logic API
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAME2048_SIZE 4

/** @brief Swipe direction */
typedef enum {
    GAME2048_UP    = 0,
    GAME2048_RIGHT = 1,
    GAME2048_DOWN  = 2,
    GAME2048_LEFT  = 3,
} game2048_direction_t;

/** @brief Game state (shared with UI, accessed via game2048_get_state) */
typedef struct {
    int  board[GAME2048_SIZE][GAME2048_SIZE];
    int  score;
    int  best_score;
    bool game_over;
    bool won;
} game_state_t;

/**
 * @brief Initialize the 2048 module
 */
void game2048_init(void);

/**
 * @brief Start a new game (spawns 2 initial tiles)
 */
void game2048_start(void);

/**
 * @brief Restart the game (reset board + score, keep best_score)
 */
void game2048_restart(void);

/**
 * @brief Move tiles in the given direction.
 *
 * @param dir  Direction of the move.
 * @return true   Valid move executed,
 * @return false  No tiles moved (blocked).
 */
bool game2048_move(game2048_direction_t dir);

/**
 * @brief Get read-only pointer to the current game state.
 */
const game_state_t *game2048_get_state(void);

/**
 * @brief Get the all-time best score.
 */
int game2048_get_best_score(void);

#ifdef __cplusplus
}
#endif
