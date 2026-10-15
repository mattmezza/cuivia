/* Headless tests for the ported game logic and the JSON/entity decoder.
 * Exits nonzero on the first failed expectation.
 * SPDX-License-Identifier: MIT */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../json.h"
#include "../trivia.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void test_calc(void) {
    trivia_game g;
    trivia_new_game(&g);
    CHECK(g.diff == 50 && g.mul == 1 && g.stage == TRIVIA_STAGE_INTRO);
    CHECK(g.time_ms == (long)trivia_calc_time(&g) * 1000L);

    static const struct {
        int diff, time_secs, qq, mul;
    } cases[] = {
        {0, 30, 3, 2},  {32, 22, 5, 2},  {33, 22, 5, 3},  {50, 18, 6, 3},
        {65, 14, 7, 3}, {66, 14, 7, 4},  {100, 5, 10, 4},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        g.diff = cases[i].diff;
        CHECK(trivia_calc_time(&g) == cases[i].time_secs);
        CHECK(trivia_calc_qq(&g) == cases[i].qq);
        CHECK(trivia_calc_mul(&g) == cases[i].mul);
    }

    /* Full time left on an 18s question scores exactly 100 per multiplier. */
    g.diff = 50;
    g.mul = 3;
    trivia_start_timer(&g);
    CHECK(trivia_calc_pts(&g, 18) == 100 * 3);
    /* Half the time gone: ceil((1 - sqrt(0.5)) * 100) = 30 */
    g.time_ms = 9000;
    CHECK(trivia_calc_pts(&g, 18) == 30 * 3);
}

static void test_answer_flow(void) {
    trivia_game g;
    trivia_new_game(&g);
    g.diff = 50;
    static const char *wrong[] = {"B", "C", "D"};
    trivia_question qs[2];
    memset(qs, 0, sizeof qs);
    snprintf(qs[0].question, sizeof qs[0].question, "q1");
    snprintf(qs[0].correct, sizeof qs[0].correct, "A");
    for (int i = 0; i < 3; i++)
        snprintf(qs[0].opts[i], sizeof qs[0].opts[i], "%s", wrong[i]);
    snprintf(qs[0].opts[3], sizeof qs[0].opts[3], "A");
    qs[0].opt_count = 4;
    snprintf(qs[1].question, sizeof qs[1].question, "q2");
    snprintf(qs[1].correct, sizeof qs[1].correct, "X");
    snprintf(qs[1].opts[0], sizeof qs[1].opts[0], "Y");
    snprintf(qs[1].opts[1], sizeof qs[1].opts[1], "X");
    qs[1].opt_count = 2;
    trivia_begin_game(&g, qs, 2);
    CHECK(g.stage == TRIVIA_STAGE_GAME && g.cur == 0 && g.qq_count == 2);

    CHECK(!trivia_is_correct(&g, "B"));
    CHECK(trivia_is_correct(&g, "A"));
    trivia_answer(&g, "A"); /* correct: 100 pts, mul 1 -> 1*3 = 3 */
    CHECK(g.pts == 100 * 1 && g.mul == 3 && g.cur == 1);
    trivia_answer(&g, "Y"); /* wrong: mul resets to 1 */
    CHECK(g.mul == 1 && g.stage == TRIVIA_STAGE_RESULTS);
}

static void test_shuffle(void) {
    trivia_question q;
    memset(&q, 0, sizeof q);
    snprintf(q.correct, sizeof q.correct, "R");
    for (int i = 0; i < 3; i++)
        snprintf(q.opts[i], sizeof q.opts[i], "W%d", i);
    snprintf(q.opts[3], sizeof q.opts[3], "R");
    q.opt_count = 4;
    unsigned seed = 12345;
    trivia_shuffle_options(&q, &seed);
    /* All four original options must still be present exactly once. */
    int seen_R = 0, seen_W[3] = {0, 0, 0};
    for (int i = 0; i < 4; i++) {
        if (!strcmp(q.opts[i], "R"))
            seen_R++;
        for (int j = 0; j < 3; j++) {
            char buf[16];
            snprintf(buf, sizeof buf, "W%d", j);
            if (!strcmp(q.opts[i], buf))
                seen_W[j]++;
        }
    }
    CHECK(seen_R == 1);
    for (int j = 0; j < 3; j++)
        CHECK(seen_W[j] == 1);
}

static void test_json(void) {
    const char *payload =
        "{\"response_code\":0,\"results\":[{"
        "\"category\":\"Mythology\",\"type\":\"multiple\",\"difficulty\":\"medium\","
        "\"question\":\"Hera is god of...\\\"?\","
        "\"correct_answer\":\"Marriage\",\"incorrect_answers\":[\"War\",\"Sea\"]},"
        "{\"category\":\"General\",\"type\":\"boolean\",\"difficulty\":\"easy\","
        "\"question\":\"&#39;quoted&#x27; & &eacute; \","
        "\"correct_answer\":\"False\","
        "\"incorrect_answers\":[\"True\"]}]}";
    trivia_question qs[4];
    const int n = json_parse_questions(payload, strlen(payload), qs, 4);
    CHECK(n == 2);
    if (n == 2) {
        CHECK(!strcmp(qs[0].category, "Mythology"));
    CHECK(!strcmp(qs[0].question, "Hera is god of...\"?"));
        CHECK(!strcmp(qs[0].correct, "Marriage"));
        CHECK(qs[0].opt_count == 3);
        CHECK(!strcmp(qs[1].question, "'quoted' & é "));
        CHECK(qs[1].opt_count == 2);
    }
    char out[128];
    json_decode_entities("a < b && c &#x263A; &unknown;", out, sizeof out);
    CHECK(!strcmp(out, "a < b && c \xE2\x98\xBA &unknown;"));
    /* malformed payloads fail cleanly */
    CHECK(json_parse_questions("{\"response_code\":1,\"results\":[]}",
                               strlen("{\"response_code\":1,\"results\":[]}"), qs, 4) == 0);
    CHECK(json_parse_questions("not json", 8, qs, 4) == -1);
}

int main(void) {
    test_calc();
    test_answer_flow();
    test_shuffle();
    test_json();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("all logic tests passed");
    return 0;
}
