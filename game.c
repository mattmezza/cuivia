/* SPDX-License-Identifier: MIT */
#include "trivia.h"

#include <math.h>
#include <string.h>

/* Small xorshift32 PRNG so we avoid POSIX rand_r feature macros; equivalent
 * role to the web app's Math.random(). */
static unsigned int next_random(unsigned int *state) {
    unsigned int x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void swap_opts(trivia_question *q, int a, int b) {
    char tmp[256];
    memcpy(tmp, q->opts[a], sizeof tmp);
    memcpy(q->opts[a], q->opts[b], sizeof tmp);
    memcpy(q->opts[b], tmp, sizeof tmp);
}

/* const shuffle = a => {
 *   let idx = a.length
 *   while (idx != 0) {
 *     const rand = Math.floor(Math.random() * idx)
 *     idx--
 *     [a[idx], a[rand]] = [a[rand], a[idx]]
 *   }
 *   return a
 * } */
void trivia_shuffle_options(trivia_question *q, unsigned int *seed) {
    int idx = q->opt_count;
    while (idx != 0) {
        const int rand_index = (int)(next_random(seed) % (unsigned int)idx);
        idx--;
        swap_opts(q, idx, rand_index);
    }
}

void trivia_new_game(trivia_game *g) {
    memset(g, 0, sizeof *g);
    g->diff = 50;
    g->mul = 1;
    g->stage = TRIVIA_STAGE_INTRO;
    g->time_ms = trivia_calc_time(g) * 1000L;
}

/* const calcTime = () => Math.ceil(SECS_MAX - ((SECS_MAX - SECS_MIN) / 100 * state.diff)) */
int trivia_calc_time(const trivia_game *g) {
    return (int)ceil(TRIVIA_SECS_MAX - ((double)(TRIVIA_SECS_MAX - TRIVIA_SECS_MIN) / 100.0 *
                                        (double)g->diff));
}

/* const calcQQ = () => Math.floor(QQ_MIN + ((QQ_MAX - QQ_MIN) / 100 * state.diff)) */
int trivia_calc_qq(const trivia_game *g) {
    return (int)floor((double)TRIVIA_QQ_MIN +
                      ((double)(TRIVIA_QQ_MAX - TRIVIA_QQ_MIN) / 100.0 * (double)g->diff));
}

/* const calcMul = () => state.diff < 66 ? state.diff < 33 ? 2 : 3 : 4 */
int trivia_calc_mul(const trivia_game *g) {
    if (g->diff < 66)
        return g->diff < 33 ? 2 : 3;
    return 4;
}

/* const calcPts = avail => {
 *   const tot = avail * 1000
 *   const elapsed = tot - state.time
 *   return Math.ceil((1 - Math.pow(elapsed / tot, .5)) * 100) * state.mul
 * } */
long trivia_calc_pts(const trivia_game *g, int avail_secs) {
    const double tot = (double)avail_secs * 1000.0;
    const double elapsed = tot - (double)g->time_ms;
    if (tot <= 0.0)
        return 0;
    double ratio = elapsed / tot;
    if (ratio < 0.0)
        ratio = 0.0;
    if (ratio > 1.0)
        ratio = 1.0;
    return (long)ceil((1.0 - sqrt(ratio)) * 100.0) * (long)g->mul;
}

/* const next = () => {
 *   if (state.cur + 1 < state.qq.length) {
 *     state.cur = state.cur + 1
 *     state.q = state.qq[state.cur]
 *     return true
 *   } else {
 *     state.stage = 'results'
 *     return false
 *   }
 * } */
bool trivia_next(trivia_game *g) {
    if (g->cur + 1 < g->qq_count) {
        g->cur = g->cur + 1;
        return true;
    }
    g->stage = TRIVIA_STAGE_RESULTS;
    return false;
}

bool trivia_is_correct(const trivia_game *g, const char *opt) {
    const trivia_question *q = &g->qq[g->cur];
    return opt && strcmp(q->correct, opt) == 0;
}

/* const answer = (mul, maxTime) => opt => {
 *   clearInterval(ticker)
 *   if (isCorrect(opt)) {
 *     state.pts += calcPts(maxTime)
 *     state.mul *= mul
 *   } else {
 *     state.mul = 1
 *   }
 *   if (next()) startTimer()
 * } */
void trivia_answer(trivia_game *g, const char *opt) {
    if (trivia_is_correct(g, opt)) {
        g->pts += trivia_calc_pts(g, trivia_calc_time(g));
        g->mul *= trivia_calc_mul(g);
    } else {
        g->mul = 1;
    }
    if (trivia_next(g))
        trivia_start_timer(g);
}

void trivia_start_timer(trivia_game *g) { g->time_ms = trivia_calc_time(g) * 1000L; }

/* The web app ticks every 10 ms; when the remainder reaches zero the game ends. */
void trivia_tick(trivia_game *g, long delta_ms) {
    if (g->stage != TRIVIA_STAGE_GAME)
        return;
    g->time_ms -= delta_ms;
    if (g->time_ms <= 0) {
        g->time_ms = 0;
        g->stage = TRIVIA_STAGE_RESULTS;
    }
}

/* const restart = async () => {
 *   await fetchQuestions()
 *   state.cur = 0; state.q = state.qq[0]; state.pts = 0; state.mul = 1;
 *   state.stage = 'game'; startTimer()
 * } */
void trivia_begin_game(trivia_game *g, const trivia_question *qq, int count) {
    g->qq_count = 0;
    if (count > 0) {
        const int n = count < TRIVIA_MAX_QUESTIONS ? count : TRIVIA_MAX_QUESTIONS;
        memcpy(g->qq, qq, (size_t)n * sizeof(trivia_question));
        g->qq_count = n;
    }
    g->cur = 0;
    g->pts = 0;
    g->mul = 1;
    g->stage = TRIVIA_STAGE_GAME;
    trivia_start_timer(g);
}
