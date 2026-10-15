/* Sprinkle Trivia native UI, mirroring https://matteo.merola.co/sprinkle-trivia/
 * Layout, copy, colors (Tailwind dark palette) and behavior follow index.html.
 * SPDX-License-Identifier: MIT */
#include "app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cui/cui_components.h"

/* ---- Tailwind dark palette used by the web app ---- */
#define SLATE_900 (cui_color){15, 23, 42, 255}
#define SLATE_800 (cui_color){30, 41, 59, 255}
#define SLATE_700 (cui_color){51, 65, 85, 255}
#define SLATE_500 (cui_color){100, 116, 139, 255}
#define GRAY_100 (cui_color){243, 244, 246, 255}
#define GRAY_300 (cui_color){209, 213, 219, 255}
/* Title gradient from-indigo-300 to-fuchsia-600 (midpoint) */
#define TITLE_GRADIENT (cui_color){178, 109, 231, 255}
/* Results gradient from-fuchsia-700 to-indigo-900 (midpoint) */
#define RESULT_GRADIENT (cui_color){105, 37, 152, 255}
#define INDIGO_300 (cui_color){165, 180, 252, 255}
#define INDIGO_500 (cui_color){99, 102, 241, 255}
#define INDIGO_600 (cui_color){79, 70, 229, 255}
#define INDIGO_700 (cui_color){67, 56, 202, 255}
#define FUCHSIA_500 (cui_color){217, 70, 239, 255}
#define FUCHSIA_600 (cui_color){192, 38, 211, 255}
#define FUCHSIA_700 (cui_color){162, 28, 175, 255}
#define INDIGO_900 (cui_color){49, 46, 129, 255}
#define RED_700 (cui_color){185, 28, 28, 255}
#define YELLOW_400 (cui_color){250, 204, 21, 255}
#define WHITE (cui_color){255, 255, 255, 255}

static cui_id id(cui_context *ui, const char *name) { return cui_id_from(ui, name); }

static void text(cui_context *ui, const char *s, float size, cui_color color, bool bold) {
    cui_text_ex(ui, cui_string(s), size, color, 0,
                (cui_text_style){.weight = bold ? CUI_TEXT_WEIGHT_BOLD : CUI_TEXT_WEIGHT_NORMAL});
}

static void start_game(app_state *a) {
    /* Web: restart() is only reachable when the loader overlay is gone and a
     * player name is set. */
    if (a->game.loading || a->fetch || !a->game.player[0])
        return;
    a->game.loading = true;
    a->error[0] = '\0';
    a->fetch = trivia_fetch_start(trivia_calc_qq(&a->game));
    if (!a->fetch) {
        a->game.loading = false;
        snprintf(a->error, sizeof a->error, "cannot start download thread");
    }
}

static void new_game(app_state *a) {
    /* Web newGame(): full reset including player and difficulty. */
    trivia_new_game(&a->game);
    a->diff_f = 50;
    const size_t len = strlen(a->game.player);
    if (len)
        cui_edit_replace_range(a->player_edit, 0, len, cui_string(""));
    a->fetch = NULL;
}

static void update(app_state *a, float dt_ms) {
    a->game.diff = (int)(a->diff_f + 0.5f);
    if (a->fetch && a->fetch->done) {
        trivia_fetch *job = a->fetch;
        a->fetch = NULL;
        a->game.loading = false;
        if (job->ok)
            trivia_begin_game(&a->game, job->qq, job->count);
        else
            snprintf(a->error, sizeof a->error, "%.*s", (int)sizeof a->error - 1,
                     job->error);
        trivia_fetch_free(job);
    }
    trivia_tick(&a->game, (long)dt_ms);
    /* Test hook: answer the first option shortly after each question starts. */
    static bool autoanswer;
    static bool probed;
    if (!probed) {
        autoanswer = getenv("TRIVIA_AUTOANSWER") != NULL;
        probed = true;
    }
    if (autoanswer && a->game.stage == TRIVIA_STAGE_GAME)
        trivia_answer(&a->game, a->game.qq[a->game.cur].opts[0]);
}

/* ---- views ---- */

static void intro_view(cui_context *ui, const cui_theme *theme, app_state *a) {
    cui_begin_box(ui, id(ui, "intro"), (cui_box){.gap = 32, .align_x = CUI_ALIGN_CENTER});
    cui_begin_box(ui, id(ui, "introHeader"),
                  (cui_box){.gap = 16, .align_x = CUI_ALIGN_CENTER});
    text(ui, "Play a trivia game!", 72, TITLE_GRADIENT, true);
    text(ui, "Answer as many questions you can, as fast as you can!", 20, GRAY_100, true);
    cui_end_box(ui);

    cui_input_result input = cui_input_ex(
        ui, id(ui, "player"), theme, cui_string("Player name"), a->player_edit,
        &a->name_input,
        (cui_input_options){.width = 340,
                            .placeholder = cui_string("Enter your name"),
                            .text_align = CUI_ALIGN_CENTER});
    if (input.submitted && a->game.player[0])
        start_game(a);

    cui_begin_box(ui, id(ui, "difficultyGroup"),
                  (cui_box){.gap = 16, .align_x = CUI_ALIGN_CENTER, .width = 448});
    text(ui, "DIFFICULTY", 14, GRAY_300, true);
    cui_slider(ui, id(ui, "difficulty"), theme, cui_string(""), &a->diff_f, 0, 100, 1, false);
    char desc[96];
    snprintf(desc, sizeof desc, "%s · %d SECONDS - %d QUESTIONS",
             a->game.diff < 33   ? "EASY"
             : a->game.diff < 66 ? "MEDIUM"
                                 : "HARD",
             trivia_calc_time(&a->game), trivia_calc_qq(&a->game));
    text(ui, desc, 18, GRAY_100, true);
    cui_end_box(ui);

    cui_button_options play_options = {.width = 150,
                                       .height = 48,
                                       .font_size = 16,
                                       .variant = CUI_BUTTON_PRIMARY,
                                       .disabled = !a->game.player[0] || a->game.loading};
    if (cui_button_ex(ui, id(ui, "startBtn"), theme, cui_string("Play"), play_options))
        start_game(a);
    if (a->error[0])
        text(ui, a->error, 16, RED_700, false);
    cui_end_box(ui);
}

/* The time value paints red below 5s (with a pulse), yellow below 10s. */
static cui_color time_color(const app_state *a) {
    const long t = a->game.time_ms;
    if (t < 5000) {
        /* animate-pulse: opacity oscillates 50%..100% over 2s */
        const double phase = fmod(a->now_ms, 2000.0) / 2000.0;
        const unsigned char alpha =
            (unsigned char)(128 + 127 * (0.5 + 0.5 * cos(phase * 6.28318530718)));
        cui_color c = RED_700;
        c.a = alpha;
        return c;
    }
    if (t < 10000)
        return YELLOW_400;
    return WHITE;
}

static void game_view(cui_context *ui, const cui_theme *theme, app_state *a) {
    const trivia_game *g = &a->game;
    const trivia_question *q = &g->qq[g->cur];
    cui_begin_box(ui, id(ui, "game"),
                  (cui_box){.gap = 8, .align_x = CUI_ALIGN_CENTER, .width = -1});
    char header[64];
    snprintf(header, sizeof header, "QUESTION #%d", g->cur + 1);
    text(ui, header, 18, GRAY_100, true);
    text(ui, q->question, 36, GRAY_100, true);

    cui_begin_box(ui, id(ui, "meta"),
                  (cui_box){.row = true,
                            .gap = 4,
                            .align_x = CUI_ALIGN_CENTER,
                            .align_y = CUI_ALIGN_CENTER,
                            .custom_padding = true,
                            .padding_edges = {.top = 8}});
    text(ui, "player:", 16, SLATE_500, false);
    text(ui, g->player, 16, WHITE, true);
    text(ui, "· points:", 16, SLATE_500, false);
    char pts[32];
    snprintf(pts, sizeof pts, "%ld", g->pts);
    text(ui, pts, 16, WHITE, true);
    text(ui, "· time:", 16, SLATE_500, false);
    char t[32];
    snprintf(t, sizeof t, "%ld", g->time_ms / 10);
    text(ui, t, 16, time_color(a), true);
    cui_end_box(ui);

    cui_begin_box(ui, id(ui, "opts"),
                  (cui_box){.gap = 4,
                            .align_x = CUI_ALIGN_CENTER,
                            .custom_padding = true,
                            .padding_edges = {.top = 24}});
    for (int i = 0; i < q->opt_count; i++) {
        char key[16];
        snprintf(key, sizeof key, "opt%d", i);
        cui_button_options options = {.width = 448,
                                      .height = 56,
                                      .font_size = 20,
                                      .variant = CUI_BUTTON_CUSTOM,
                                      .custom_colors = true,
                                      .colors = {.normal = FUCHSIA_700,
                                                 .hover = FUCHSIA_600,
                                                 .active = FUCHSIA_500,
                                                 .foreground = WHITE},
                                      .rounding = CUI_BUTTON_ROUNDED_CUSTOM,
                                      .radius = 4};
        if (cui_button_ex(ui, id(ui, key), theme, cui_string(q->opts[i]), options))
            trivia_answer(&a->game, q->opts[i]);
    }
    cui_end_box(ui);
    cui_end_box(ui);
}

static void results_view(cui_context *ui, const cui_theme *theme, app_state *a) {
    cui_begin_box(ui, id(ui, "results"),
                  (cui_box){.gap = 32, .align_x = CUI_ALIGN_CENTER});
    text(ui, "RESULTS", 20, GRAY_100, true);
    char over[128];
    snprintf(over, sizeof over, "Game over, %s.", a->game.player);
    cui_text_ex(ui, cui_string(over), 84, RESULT_GRADIENT, 0,
                (cui_text_style){.weight = CUI_TEXT_WEIGHT_BOLD});
    char reached[96];
    snprintf(reached, sizeof reached, "YOU REACHED %ld POINTS!", a->game.pts);
    text(ui, reached, 20, GRAY_100, true);
    cui_begin_box(ui, id(ui, "resultButtons"), (cui_box){.row = true, .gap = 32});
    if (cui_button_ex(ui, id(ui, "newGame"), theme, cui_string("New Game"),
                      (cui_button_options){.width = 150, .height = 48}))
        new_game(a);
    if (cui_button_ex(ui, id(ui, "restartBtn"), theme, cui_string("Play again"),
                      (cui_button_options){.width = 150, .height = 48}))
        start_game(a);
    cui_end_box(ui);
    cui_end_box(ui);
}

/* Full-viewport loader drawn in a top-layer portal (web #loader overlay). */
static void loader_view(cui_context *ui, const cui_theme *theme, app_state *a, float width,
                        float height) {
    const double phase = fmod(a->now_ms, 1000.0) / 1000.0;
    const unsigned char alpha = (unsigned char)(128 + 127 * (0.5 + 0.5 * cos(phase * 6.28318530718)));
    cui_begin_portal(ui, id(ui, "loader"),
                     (cui_box){.align_x = CUI_ALIGN_CENTER,
                               .align_y = CUI_ALIGN_CENTER,
                               .background = {15, 23, 42, 190},
                               .inert = true},
                     (cui_rect){0, 0, width, height}, 1);
    cui_text_ex(ui, cui_string("Loading..."), 48, (cui_color){243, 244, 246, alpha}, 0,
                (cui_text_style){.weight = CUI_TEXT_WEIGHT_BOLD});
    (void)theme;
    cui_end_box(ui);
}

void app_frame(cui_context *ui, const cui_theme *theme, app_state *a, float dt_ms, float width,
               float height) {
    update(a, dt_ms);
    cui_begin_box(ui, id(ui, "root"),
                  (cui_box){.width = -1,
                            .height = -1,
                            .padding = 32,
                            .gap = 32,
                            .background = theme->background,
                            .align_x = CUI_ALIGN_CENTER,
                            .align_y = CUI_ALIGN_CENTER});
    switch (a->game.stage) {
    case TRIVIA_STAGE_INTRO:
        intro_view(ui, theme, a);
        break;
    case TRIVIA_STAGE_GAME:
        game_view(ui, theme, a);
        break;
    case TRIVIA_STAGE_RESULTS:
        results_view(ui, theme, a);
        break;
    }
    cui_end_box(ui);
    if (a->game.loading)
        loader_view(ui, theme, a, width, height);
}

bool app_init(app_state *a) {
    memset(a, 0, sizeof *a);
    trivia_new_game(&a->game);
    a->diff_f = 50;
    a->player_edit = cui_edit_create(
        (cui_edit_config){.buffer = a->game.player, .capacity = sizeof a->game.player});
    if (!a->player_edit)
        return false;
    /* Test hook: auto-fill the name and start (same path as the Play button). */
    const char *autoplay = getenv("TRIVIA_AUTOPLAY");
    if (autoplay && autoplay[0]) {
        snprintf(a->game.player, sizeof a->game.player, "%.*s",
                 (int)sizeof a->game.player - 1, autoplay);
        start_game(a);
    }
    return true;
}

void app_destroy(app_state *a) {
    if (a->fetch) {
        trivia_fetch_free(a->fetch);
        a->fetch = NULL;
    }
    if (a->player_edit)
        cui_edit_destroy(a->player_edit);
    a->player_edit = NULL;
}
