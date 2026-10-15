/* Minimal JSON reader for the opentdb api.php response, plus decoding of the
 * HTML entities the API embeds in its strings (the browser does this for free
 * in the web app; we replicate it here).
 * SPDX-License-Identifier: MIT */
#ifndef TRIVIA_JSON_H
#define TRIVIA_JSON_H

#include <stddef.h>

#include "trivia.h"

/* Parses {"response_code":0,"results":[...]} into out. Returns the number of
 * questions parsed, or -1 when the payload is not a valid results array. */
int json_parse_questions(const char *text, size_t length, trivia_question *out, int max);

/* Decodes numeric (&#39;, &#x27;) and common named HTML entities in place-safe
 * src -> dst. Unknown named entities are copied through unchanged. */
void json_decode_entities(const char *src, char *dst, size_t cap);

#endif
