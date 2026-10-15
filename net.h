/* HTTPS question fetch for Sprinkle Trivia, talking to the same backend as the
 * web app: https://opentdb.com/api.php?amount=N
 * Runs on a background thread so the UI keeps animating the loader.
 * SPDX-License-Identifier: MIT */
#ifndef TRIVIA_NET_H
#define TRIVIA_NET_H

#include <stdbool.h>

#include "trivia.h"

typedef struct {
    /* Written by the worker, read by the UI thread; one-shot latch. */
    volatile bool done;
    bool ok;
    char error[256];
    trivia_question qq[TRIVIA_MAX_QUESTIONS];
    int count;
    /* Worker thread handle bookkeeping */
    void *thread;
    bool joined;
} trivia_fetch;

/* Starts fetching `amount` questions. Returns NULL on thread creation failure. */
trivia_fetch *trivia_fetch_start(int amount);
/* Frees a finished job (call only after done). */
void trivia_fetch_free(trivia_fetch *job);

#endif
