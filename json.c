/* SPDX-License-Identifier: MIT */
#include "json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- HTML entity decoding ---------- */

typedef struct {
    const char *name;
    unsigned int cp;
} named_entity;

/* Common entities seen in OpenTDB trivia text. */
static const named_entity k_entities[] = {
    {"amp", '&'},      {"AMP", '&'},      {"quot", '"'},     {"QUOT", '"'},
    {"apos", '\''},    {"lt", '<'},       {"gt", '>'},       {"nbsp", 160},
    {"shy", 173},      {"iexcl", 161},    {"pound", 163},    {"curren", 164},
    {"yen", 165},      {"copy", 169},     {"reg", 174},      {"deg", 176},
    {"plusmn", 177},   {"sup2", 178},     {"sup3", 179},     {"micro", 181},
    {"middot", 183},   {"frac14", 188},   {"frac12", 189},   {"frac34", 190},
    {"ldquo", 8220},   {"rdquo", 8221},   {"lsquo", 8216},   {"rsquo", 8217},
    {"sbquo", 8218},   {"ldquor", 8222},  {"laquo", 171},    {"raquo", 187},
    {"hellip", 8230},  {"mdash", 8212},   {"ndash", 8211},   {"hyphen", 8209},
    {"eacute", 233},   {"Eacute", 201},   {"egrave", 232},   {"Egrave", 200},
    {"ecirc", 234},    {"agrave", 224},   {"agrave", 192},   {"ccedil", 231},
    {"Ccedil", 199},   {"auml", 228},     {"Auml", 196},     {"ouml", 246},
    {"Ouml", 214},     {"uuml", 252},     {"Uuml", 220},     {"szlig", 223},
    {"ntilde", 241},   {"Ntilde", 209},   {"aacute", 225},   {"Aacute", 193},
    {"euml", 235},     {"iuml", 239},     {"ocirc", 244},    {"ucirc", 251},
    {"aring", 229},    {"oslash", 248},   {"aelig", 230},    {"sect", 167},
    {"para", 182},     {"bull", 8226},    {"dagger", 8224},  {"trade", 8482},
    {"euro", 8364},    {"deg?", 0},       /* sentinel guard */
};
#define K_ENTITIES_COUNT ((int)(sizeof k_entities / sizeof k_entities[0]) - 1)

static int encode_utf8(unsigned int cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

void json_decode_entities(const char *src, char *dst, size_t cap) {
    size_t w = 0;
    const char *p = src;
    while (*p && w + 8 < cap) {
        if (*p != '&') {
            dst[w++] = *p++;
            continue;
        }
        /* numeric forms */
        if (p[1] == '#') {
            const char *q = p + 2;
            int hex = 0;
            unsigned int cp = 0;
            if (*q == 'x' || *q == 'X') {
                hex = 1;
                q++;
            }
            int digits = 0;
            while (*q && digits < 8) {
                int d = hex ? hex_digit(*q) : (*q >= '0' && *q <= '9' ? *q - '0' : -1);
                if (d < 0)
                    break;
                cp = cp * (hex ? 16u : 10u) + (unsigned int)d;
                q++;
                digits++;
            }
            if (digits > 0 && *q == ';') {
                w += (size_t)encode_utf8(cp, dst + w);
                p = q + 1;
                continue;
            }
        }
        /* named forms */
        {
            const char *q = p + 1;
            const char *semi = NULL;
            for (const char *s = q; *s && s - q < 32; s++) {
                if (*s == ';') {
                    semi = s;
                    break;
                }
                if (!isalnum((unsigned char)*s))
                    break;
            }
            if (semi) {
                const size_t nlen = (size_t)(semi - q);
                bool matched = false;
                for (int i = 0; i < K_ENTITIES_COUNT; i++) {
                    if (strlen(k_entities[i].name) == nlen &&
                        memcmp(k_entities[i].name, q, nlen) == 0) {
                        w += (size_t)encode_utf8(k_entities[i].cp, dst + w);
                        matched = true;
                        break;
                    }
                }
                if (matched) {
                    p = semi + 1;
                    continue;
                }
            }
        }
        /* unknown entity: copy the '&' literally */
        dst[w++] = *p++;
    }
    /* copy any remainder when close to capacity */
    for (const char *r = p; *r && w + 1 < cap; r++)
        dst[w++] = *r;
    dst[w] = '\0';
}

/* ---------- JSON ---------- */

typedef struct {
    const char *p;
    const char *end;
    bool failed;
} jp;

static void skip_ws(jp *s) {
    while (s->p < s->end && (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r'))
        s->p++;
}

static void fail(jp *s) { s->failed = true; }

static bool expect(jp *s, char c) {
    skip_ws(s);
    if (s->p < s->end && *s->p == c) {
        s->p++;
        return true;
    }
    fail(s);
    return false;
}

/* Parses a JSON string (handling escapes) into tmp, then entity-decodes into out. */
static bool parse_string_value(jp *s, char *out, size_t cap) {
    if (!expect(s, '"'))
        return false;
    char tmp[2048];
    size_t w = 0;
    while (s->p < s->end && *s->p != '"') {
        if (w + 8 >= sizeof tmp) {
            fail(s);
            return false;
        }
        char c = *s->p++;
        if (c == '\\') {
            if (s->p >= s->end) {
                fail(s);
                return false;
            }
            char e = *s->p++;
            switch (e) {
            case '"': tmp[w++] = '"'; break;
            case '\\': tmp[w++] = '\\'; break;
            case '/': tmp[w++] = '/'; break;
            case 'b': tmp[w++] = '\b'; break;
            case 'f': tmp[w++] = '\f'; break;
            case 'n': tmp[w++] = '\n'; break;
            case 'r': tmp[w++] = '\r'; break;
            case 't': tmp[w++] = '\t'; break;
            case 'u': {
                if (s->end - s->p < 4) {
                    fail(s);
                    return false;
                }
                unsigned int cp = 0;
                for (int i = 0; i < 4; i++) {
                    const int d = hex_digit(s->p[i]);
                    if (d < 0) {
                        fail(s);
                        return false;
                    }
                    cp = cp * 16u + (unsigned int)d;
                }
                s->p += 4;
                /* surrogate pair */
                if (cp >= 0xD800u && cp <= 0xDBFFu && s->end - s->p >= 6 && s->p[0] == '\\' &&
                    s->p[1] == 'u') {
                    unsigned int lo = 0;
                    bool ok = true;
                    for (int i = 0; i < 4; i++) {
                        const int d = hex_digit(s->p[2 + i]);
                        if (d < 0) {
                            ok = false;
                            break;
                        }
                        lo = lo * 16u + (unsigned int)d;
                    }
                    if (ok && lo >= 0xDC00u && lo <= 0xDFFFu) {
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                        s->p += 6;
                    }
                }
                w += (size_t)encode_utf8(cp, tmp + w);
                break;
            }
            default:
                fail(s);
                return false;
            }
        } else {
            tmp[w++] = c;
        }
    }
    if (!expect(s, '"'))
        return false;
    tmp[w] = '\0';
    json_decode_entities(tmp, out, cap);
    return !s->failed;
}

static bool skip_value(jp *s);

static bool skip_object(jp *s) {
    if (!expect(s, '{'))
        return false;
    skip_ws(s);
    if (s->p < s->end && *s->p == '}') {
        s->p++;
        return true;
    }
    for (;;) {
        if (!parse_string_value(s, (char[8]){0}, 8) && s->failed)
            return false;
        if (!expect(s, ':'))
            return false;
        if (!skip_value(s))
            return false;
        skip_ws(s);
        if (s->p < s->end && *s->p == ',') {
            s->p++;
            continue;
        }
        if (s->p < s->end && *s->p == '}') {
            s->p++;
            return true;
        }
        fail(s);
        return false;
    }
}

static bool skip_array(jp *s) {
    if (!expect(s, '['))
        return false;
    skip_ws(s);
    if (s->p < s->end && *s->p == ']') {
        s->p++;
        return true;
    }
    for (;;) {
        if (!skip_value(s))
            return false;
        skip_ws(s);
        if (s->p < s->end && *s->p == ',') {
            s->p++;
            continue;
        }
        if (s->p < s->end && *s->p == ']') {
            s->p++;
            return true;
        }
        fail(s);
        return false;
    }
}

static bool skip_value(jp *s) {
    skip_ws(s);
    if (s->p >= s->end) {
        fail(s);
        return false;
    }
    if (*s->p == '"') {
        char scratch[8];
        return parse_string_value(s, scratch, sizeof scratch);
    }
    if (*s->p == '{')
        return skip_object(s);
    if (*s->p == '[')
        return skip_array(s);
    if (*s->p == 't' && s->end - s->p >= 4 && !memcmp(s->p, "true", 4)) {
        s->p += 4;
        return true;
    }
    if (*s->p == 'f' && s->end - s->p >= 5 && !memcmp(s->p, "false", 5)) {
        s->p += 5;
        return true;
    }
    if (*s->p == 'n' && s->end - s->p >= 4 && !memcmp(s->p, "null", 4)) {
        s->p += 4;
        return true;
    }
    /* number */
    const char *start = s->p;
    if (*s->p == '-' || *s->p == '+')
        s->p++;
    while (s->p < s->end &&
           (isdigit((unsigned char)*s->p) || *s->p == '.' || *s->p == 'e' || *s->p == 'E' ||
            *s->p == '+' || *s->p == '-'))
        s->p++;
    if (s->p == start) {
        fail(s);
        return false;
    }
    return true;
}

/* Reads the current object's known fields. Wrong key order and unknown fields
 * are tolerated. */
static bool parse_question_object(jp *s, trivia_question *q) {
    memset(q, 0, sizeof *q);
    if (!expect(s, '{'))
        return false;
    skip_ws(s);
    if (s->p < s->end && *s->p == '}') {
        s->p++;
        return false; /* empty object: not a question */
    }
    for (;;) {
        char key[64];
        if (!parse_string_value(s, key, sizeof key))
            return false;
        if (!expect(s, ':'))
            return false;
        if (!strcmp(key, "category"))
            parse_string_value(s, q->category, sizeof q->category);
        else if (!strcmp(key, "type"))
            parse_string_value(s, q->type, sizeof q->type);
        else if (!strcmp(key, "difficulty"))
            parse_string_value(s, q->difficulty, sizeof q->difficulty);
        else if (!strcmp(key, "question"))
            parse_string_value(s, q->question, sizeof q->question);
        else if (!strcmp(key, "correct_answer"))
            parse_string_value(s, q->correct, sizeof q->correct);
        else if (!strcmp(key, "incorrect_answers")) {
            if (!expect(s, '['))
                return false;
            q->opt_count = 0;
            skip_ws(s);
            if (s->p < s->end && *s->p == ']') {
                s->p++;
            } else {
                for (;;) {
                    char wrong[256];
                    if (!parse_string_value(s, wrong, sizeof wrong))
                        return false;
                    if (q->opt_count < 3) {
                        memcpy(q->opts[q->opt_count], wrong, sizeof wrong);
                        q->opt_count++;
                    }
                    skip_ws(s);
                    if (s->p < s->end && *s->p == ',') {
                        s->p++;
                        continue;
                    }
                    if (s->p < s->end && *s->p == ']') {
                        s->p++;
                        break;
                    }
                    fail(s);
                    return false;
                }
            }
        } else if (!skip_value(s)) {
            return false;
        }
        skip_ws(s);
        if (s->p < s->end && *s->p == ',') {
            s->p++;
            continue;
        }
        if (s->p < s->end && *s->p == '}') {
            s->p++;
            break;
        }
        fail(s);
        return false;
    }
    if (!q->question[0] || !q->correct[0] || q->opt_count == 0)
        return false;
    memcpy(q->opts[q->opt_count], q->correct, sizeof q->opts[q->opt_count]);
    q->opt_count++;
    return true;
}

int json_parse_questions(const char *text, size_t length, trivia_question *out, int max) {
    jp s = {.p = text, .end = text + length, .failed = false};
    if (!expect(&s, '{'))
        return -1;
    bool in_results = false;
    int count = 0;
    skip_ws(&s);
    if (s.p < s.end && *s.p == '}') {
        s.p++;
        return 0;
    }
    for (;;) {
        char key[64];
        if (!parse_string_value(&s, key, sizeof key))
            return -1;
        if (!expect(&s, ':'))
            return -1;
        if (!strcmp(key, "results")) {
            if (!expect(&s, '['))
                return -1;
            skip_ws(&s);
            if (s.p < s.end && *s.p == ']') {
                s.p++;
                in_results = true;
                break;
            }
            for (;;) {
                if (count >= max) {
                    if (!skip_value(&s))
                        return -1;
                } else {
                    if (!parse_question_object(&s, &out[count]))
                        return -1;
                    count++;
                }
                skip_ws(&s);
                if (s.p < s.end && *s.p == ',') {
                    s.p++;
                    continue;
                }
                if (s.p < s.end && *s.p == ']') {
                    s.p++;
                    in_results = true;
                    break;
                }
                return -1;
            }
            break;
        } else if (!skip_value(&s)) {
            return -1;
        }
        skip_ws(&s);
        if (s.p < s.end && *s.p == ',') {
            s.p++;
            continue;
        }
        if (s.p < s.end && *s.p == '}') {
            s.p++;
            break;
        }
        return -1;
    }
    return in_results ? count : -1;
}
