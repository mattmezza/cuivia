/* SPDX-License-Identifier: MIT */
#ifndef TRIVIA_APP_H
#define TRIVIA_APP_H

#include <stdbool.h>

#include "cui/cui_components.h"
#include "net.h"
#include "trivia.h"

typedef struct {
    trivia_game game;
    cui_edit *player_edit;      /* owns no buffer; edits game.player directly */
    cui_input_state name_input; /* stable input widget state */
    float diff_f;               /* slider value 0..100 (web state.diff) */
    trivia_fetch *fetch;        /* in-flight backend request */
    char error[256];            /* last fetch failure, shown on the intro screen */
    double now_ms;              /* host clock for animations */
} app_state;

/* Creates the app state (input model). Returns false on allocation failure. */
bool app_init(app_state *a);
void app_destroy(app_state *a);
/* One UI frame: dt_ms drives the countdown clock; width/height the viewport. */
void app_frame(cui_context *ui, const cui_theme *theme, app_state *a, float dt_ms, float width,
               float height);

#endif
