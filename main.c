/* Sprinkle Trivia native host: SDL3 + CUI frame loop.
 * SPDX-License-Identifier: MIT */
#include "cui/cui_components.h"
#include "cui/cui_sdl3.h"
#include <SDL3/SDL.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "embedded_fonts.h"

/* Fonts are embedded in the executable (see embedded_fonts.c); --font flags
 * still allow explicit overrides. */
static bool frame_limit(const char *text, unsigned *out) {
    if (!text[0])
        return false;
    for (const char *p = text; *p; ++p)
        if (*p < '0' || *p > '9')
            return false;
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (errno || *end || !value || value > UINT_MAX)
        return false;
    *out = (unsigned)value;
    return true;
}
/* Called synchronously at failure sites; never reenter the context here. */
static void diagnostic(void *user, const cui_diagnostic *failure) {
    (void)user;
    fprintf(stderr,
            "CUI %s/%s: %s (error=%d detail=%d id=%llu frame=%llu "
            "used=%zu requested=%zu limit=%zu)\n",
            failure->subsystem, failure->operation, failure->message, (int)failure->error,
            (int)failure->detail, (unsigned long long)failure->id,
            (unsigned long long)failure->frame_sequence, failure->used, failure->requested,
            failure->limit);
}
int main(int argc, char **argv) {
    const char *regular = NULL, *bold = NULL, *capture = NULL;
    unsigned limit = 0;
    bool debug_layout = false;
    cui_debug_layout_format debug_format = CUI_DEBUG_LAYOUT_TEXT;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--font") && i + 1 < argc)
            regular = argv[++i];
        else if (!strcmp(argv[i], "--bold-font") && i + 1 < argc)
            bold = argv[++i];
        else if (!strcmp(argv[i], "--capture") && i + 1 < argc)
            capture = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            if (!frame_limit(argv[++i], &limit)) {
                fprintf(stderr, "--frames requires an integer from 1 to %u.\n", UINT_MAX);
                return 1;
            }
        } else if (!strcmp(argv[i], "--debug-layout") && i + 1 < argc) {
            const char *format = argv[++i];
            if (strcmp(format, "text") && strcmp(format, "json")) {
                fprintf(stderr, "--debug-layout requires text or json.\n");
                return 1;
            }
            debug_layout = true;
            debug_format = !strcmp(format, "json") ? CUI_DEBUG_LAYOUT_JSON : CUI_DEBUG_LAYOUT_TEXT;
        } else {
            fprintf(stderr, "Usage: sprinkle-trivia [--font PATH] [--bold-font PATH] "
                            "[--frames N] [--capture BMP] [--debug-layout text|json]\n");
            return 1;
        }
    }
    if ((regular && !regular[0]) || (bold && !bold[0]) || (capture && (!capture[0] || !limit))) {
        fprintf(stderr, "Font paths must be nonempty; --capture requires a path and --frames.\n");
        return 1;
    }
    char *default_regular = regular ? NULL : (char *)embedded_font_path(0);
    char *default_bold = bold ? NULL : (char *)embedded_font_path(1);
    regular = regular ? regular : default_regular;
    bold = bold ? bold : default_bold;
    cui_font *font = regular ? cui_font_open(regular) : NULL;
    int result = 2;
    cui_sdl3 *host = NULL;
    cui_context *ui = NULL;
    app_state app;
    bool app_ready = false;
    if (!font) {
        fprintf(stderr, "Cannot load regular font: %s\n",
                regular ? regular : "embedded font unavailable");
        goto cleanup;
    }
    if (!bold ||
        !cui_font_register_style(font, (cui_text_style){.weight = CUI_TEXT_WEIGHT_BOLD}, bold)) {
        fprintf(stderr, "Cannot load bold font: %s\n", bold ? bold : "embedded font unavailable");
        goto cleanup;
    }
    host = cui_sdl3_create("Sprinkle Trivia", 1024, 768, font);
    if (!host) {
        fprintf(stderr, "Cannot start SDL3 host: %s\n", cui_sdl3_error());
        goto cleanup;
    }
    ui = cui_create((cui_config){.width = 1024,
                                 .height = 768,
                                 .measure = cui_font_measure,
                                 .measure_styled = cui_font_measure_styled,
                                 .text_geometry = cui_font_text_geometry,
                                 .text_geometry_line = cui_font_text_geometry_line,
                                 .font_metrics = cui_font_get_metrics,
                                 .measure_user = font});
    if (!ui) {
        fprintf(stderr, "Cannot create CUI context.\n");
        goto cleanup;
    }
    cui_set_diagnostic_callback(ui, diagnostic, NULL);
    if (!app_init(&app))
        goto cleanup;
    app_ready = true;
    cui_theme theme = cui_theme_dark();
    /* Tailwind dark palette overrides on top of the CUI dark theme. */
    theme.background = (cui_color){15, 23, 42, 255};
    theme.text = (cui_color){243, 244, 246, 255};
    theme.input_background = (cui_color){30, 41, 59, 255};
    theme.input = (cui_color){51, 65, 85, 255};
    theme.radius = 8;
    theme.buttons[CUI_THEME_BUTTON_PRIMARY] = (cui_theme_button_colors){
        .normal = {99, 102, 241, 255},
        .hover = {79, 70, 229, 255},
        .active = {67, 56, 202, 255},
        .foreground = {255, 255, 255, 255},
    };
    unsigned rendered = 0;
    Uint64 last_ticks = SDL_GetTicks();
    result = 0;
    while (!limit || rendered < limit) {
        if (!cui_sdl3_poll(host, ui)) {
            if (limit) {
                fprintf(stderr, "Window closed after %u of %u requested frames.\n", rendered,
                        limit);
                result = 3;
            }
            break;
        }
        const Uint64 now_ticks = SDL_GetTicks();
        const float dt_ms = (float)(now_ticks - last_ticks);
        last_ticks = now_ticks;
        app.now_ms = (double)now_ticks;
        float width, height;
        cui_sdl3_size(host, &width, &height);
        cui_begin_frame(ui, width, height, dt_ms / 1000.0f);
        app_frame(ui, &theme, &app, dt_ms, width, height);
        cui_draw_list list = cui_end_frame(ui);
        if (cui_last_error(ui) != CUI_OK) {
            fprintf(stderr, "CUI layout failed (error %d).\n", (int)cui_last_error(ui));
            result = 3;
            break;
        }
        if (debug_layout) {
            size_t required = 0;
            (void)cui_debug_layout_export(ui, debug_format, NULL, 0, &required);
            char *dump = required ? malloc(required) : NULL;
            if (!dump || !cui_debug_layout_export(ui, debug_format, dump, required, &required)) {
                free(dump);
                fprintf(stderr, "Cannot export completed layout.\n");
                result = 3;
                break;
            }
            puts(dump);
            free(dump);
            debug_layout = false; /* Print the first completed frame once. */
        }
        (void)cui_sdl3_set_cursor(host, cui_pointer_cursor(ui));
        bool painted = capture && rendered + 1 == limit
                           ? cui_sdl3_render_capture(host, &list, theme.background, capture)
                           : cui_sdl3_render(host, &list, theme.background);
        if (!painted) {
            fprintf(stderr, "Cannot render%s: %s\n", capture ? " or capture frame" : " frame",
                    cui_sdl3_error());
            result = 3;
            break;
        }
        ++rendered; /* Only successful renders count toward the requested bound. */
        if (!limit || rendered < limit)
            cui_sdl3_wait(16);
    }
cleanup:
    if (app_ready)
        app_destroy(&app);
    cui_destroy(ui);
    cui_sdl3_destroy(host);
    cui_font_close(font);
    free(default_regular);
    free(default_bold);
    return result;
}
