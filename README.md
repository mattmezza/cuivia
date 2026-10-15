# Sprinkle Trivia — native C edition

A native rebuild of [sprinkle-trivia](https://matteo.merola.co/sprinkle-trivia/)
([web source](https://github.com/mattmezza/sprinkle-trivia)) in C11, using the
[cui](https://github.com/mattmezza/cui) UI framework and the same backend API
([opentdb.com](https://opentdb.com)). Same game rules: pick a name, set the
difficulty (question count 3–10, time 5–30 s per question, streak multiplier
×2/×3/×4), answer fast for more points, lose the multiplier on a wrong answer.

## Build

Prerequisites: `cc` (C11), GNU `make`, `ar`, `pkg-config`, `curl`, `cmake`
+ `ninja`, and SDL3/FreeType/HarfBuzz dev packages for the dev target only.

```sh
make            # fully static executable: build/sprinkle-trivia (zero .so deps)
make dev        # quick dynamic build: build/sprinkle-trivia-dev
make vendored   # download + build vendored SDL3/FreeType/HarfBuzz/mbedTLS (one-time)
make check      # logic tests (game rules, JSON parser, entity decoding)
```

The static binary links everything except glibc. SDL opens the display stack
(libX11/libwayland) via `dlopen` at runtime — standard for Linux GUI apps.

## Run

```sh
./build/sprinkle-trivia
```

The Inter Regular/Bold fonts are embedded in the executable (linked via
`.incbin`, served to FreeType through memfds), so a single binary file is a
complete, sendable artifact. Explicit font files remain available via
`--font PATH --bold-font PATH`.

Test hooks (used by CI-style captures, harmless otherwise):

```sh
TRIVIA_AUTOPLAY=Name            # pre-fill the name and start
TRIVIA_AUTOANSWER=1             # answer with the first option automatically
./build/sprinkle-trivia --frames N --capture frame.bmp   # headless capture
```

## Build notes

Everything compiles for the generic x86-64 baseline. The static binary keeps
two deliberate runtime dlopen paths: the display stack (libX11/libwayland,
standard for Linux GUI apps) and nothing else. DNS resolves via protocol-level
`res_query` to avoid NSS dlopen crashes in static binaries.

## Layout

| Path | Contents |
| --- | --- |
| `main.c` | SDL3 host + frame loop (clock, input, rendering) |
| `app.c/h` | CUI screens: intro, game, results, loader overlay |
| `game.c`, `trivia.h` | Game rules ported 1:1 from the web app |
| `json.c/h` | Minimal opentdb JSON reader + HTML entity decoder |
| `net.c/h` | HTTPS fetch on a worker thread (mbedTLS; protocol-level DNS) |
| `embedded_fonts.c/.S` | Inter OTFs linked into the binary via `.incbin` |
| `assets/fonts` | Inter Regular/Bold (SIL OFL); build-time source of the embedded blobs |
| `vendor/` | CUI sources (layout, components, SDL3 adapter, fonts pipeline) |
| `third_party/` | Vendored SDL3 3.4.18, FreeType 2.14.3, HarfBuzz 14.6.0, mbedTLS 3.6.4 |

All code lives in this directory; nothing is reused from outside (besides the
system toolchain).
