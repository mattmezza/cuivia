/* Sprinkle Trivia - game logic, a faithful C port of the sprinkle-js web app.
 * SPDX-License-Identifier: MIT */
#ifndef TRIVIA_H
#define TRIVIA_H

#include <stdbool.h>
#include <stddef.h>

/* Constants straight from index.html */
#define TRIVIA_SECS_DEF 10
#define TRIVIA_SECS_MAX 30
#define TRIVIA_SECS_MIN 5
#define TRIVIA_QQ_MAX 10
#define TRIVIA_QQ_MIN 3
#define TRIVIA_MAX_QUESTIONS TRIVIA_QQ_MAX

typedef struct {
    char category[128];
    char type[16];
    char difficulty[16];
    char question[1024];
    char correct[256];
    /* opts mirrors the web app: shuffled incorrect_answers + correct_answer */
    char opts[4][256];
    int opt_count;
} trivia_question;

typedef enum { TRIVIA_STAGE_INTRO, TRIVIA_STAGE_GAME, TRIVIA_STAGE_RESULTS } trivia_stage;

typedef struct {
    trivia_question qq[TRIVIA_MAX_QUESTIONS];
    int qq_count;
    int cur;
    bool loading;
    long pts;
    int mul;
    int diff; /* 0..100 */
    char player[64];
    trivia_stage stage;
    long time_ms; /* remaining time in ms (web state.time) */
} trivia_game;

void trivia_new_game(trivia_game *g);
/* Seconds allowed per question for the current difficulty. */
int trivia_calc_time(const trivia_game *g);
/* Number of questions for the current difficulty. */
int trivia_calc_qq(const trivia_game *g);
/* Streak multiplier gained on a correct answer (2, 3 or 4). */
int trivia_calc_mul(const trivia_game *g);
/* Points for answering with g->time_ms remaining out of avail_secs. */
long trivia_calc_pts(const trivia_game *g, int avail_secs);
/* Advance to the next question; false when the game is over. */
bool trivia_next(trivia_game *g);
bool trivia_is_correct(const trivia_game *g, const char *opt);
/* Mirrors the web answer(opt) closure: scores, updates the multiplier,
 * advances and restarts the timer when another question follows. */
void trivia_answer(trivia_game *g, const char *opt);
void trivia_start_timer(trivia_game *g);
/* Advance the countdown by delta_ms; moves to results on expiry. */
void trivia_tick(trivia_game *g, long delta_ms);
/* Sets up a fresh game with already fetched questions (web restart()). */
void trivia_begin_game(trivia_game *g, const trivia_question *qq, int count);
/* Fisher-Yates exactly like the web shuffle() helper. */
void trivia_shuffle_options(trivia_question *q, unsigned int *seed);

#endif
