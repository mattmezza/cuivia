# Sprinkle Trivia — native C edition

A native rebuild of [sprinkle-trivia](https://matteo.merola.co/sprinkle-trivia/)
([web source](https://github.com/mattmezza/sprinkle-trivia)) in C11, using the
[cui](https://github.com/mattmezza/cui) UI framework and the same backend API
([opentdb.com](https://opentdb.com)). Same game rules: pick a name, set the
difficulty (question count 3–10, time 5–30 s per question, streak multiplier
×2/×3/×4), answer fast for more points, lose the multiplier on a wrong answer.

## Build

Prerequisites: `cc` (C11), GNU `make`, `ar`, `pkg-config`, `curl`, `cmake`
and SDL3/FreeType/HarfBuzz dev packages for the dev and macOS targets.

```sh
make            # dynamic development executable: build/sprinkle-trivia-dev
make static     # Linux fully static executable: build/sprinkle-trivia
make dev        # quick dynamic build: build/sprinkle-trivia-dev
make vendored   # download + build vendored SDL3/FreeType/HarfBuzz/mbedTLS (one-time)
make check      # logic tests (game rules, JSON parser, entity decoding)
```

On Linux, the static binary links everything except glibc. SDL opens the display stack
(libX11/libwayland) via `dlopen` at runtime — standard for Linux GUI apps.

### macOS app

Install Apple's Command Line Tools and the build dependencies:

```sh
xcode-select --install
brew install cmake pkg-config sdl3 freetype harfbuzz
make macos-app -j8
open "build/Sprinkle Trivia.app"
```

The app embeds its fonts and copies all non-system dynamic libraries into
`Contents/Frameworks`, rewrites their load paths, and applies an ad-hoc signature.
It runs without Homebrew on a compatible Mac of the same architecture. Building
on Apple Silicon produces arm64; building on Intel produces x86_64. Distribution
with the standard verified-developer launch experience requires Developer ID
signing and notarization.
The first build downloads and compiles pinned mbedTLS sources using CMake.
Python 3 is required for packaging. `make dev` also works on macOS.

### Sharing the macOS app

Send the entire `.app` bundle, preferably zipped to preserve its structure:

```sh
ditto -c -k --sequesterRsrc --keepParent \
  "build/Sprinkle Trivia.app" "build/Sprinkle Trivia-macOS.zip"
```

The recipient can unzip it and move the app to Applications. No Homebrew or
separate font installation is needed. Internet access is required to load trivia.

Compatibility depends on both CPU architecture and the minimum macOS versions
of the executable and its bundled libraries. The current Apple Silicon build
requires **macOS 26 or later** and does not run on Intel Macs. Other builds may
have different requirements; merely lowering the app's deployment target does
not lower the requirements of prebuilt Homebrew libraries.

Because the app is ad-hoc signed and not notarized, a downloaded copy may be
blocked by Gatekeeper. After attempting to open it, a recipient who trusts the
app may be able to allow it through **System Settings → Privacy & Security →
Open Anyway**. See [Apple's instructions](https://support.apple.com/en-gb/102445).
For normal distribution without this manual exception, sign with Developer ID
and notarize the app.

## Run

```sh
./build/sprinkle-trivia-dev
```

The Inter Regular/Bold fonts are embedded in the executable (linked via
`.incbin`, served to FreeType through file descriptors), so the Linux static executable needs no external font files.
For macOS, share the entire app bundle; the development executable alone needs
its dynamic libraries. Explicit font files remain available via
`--font PATH --bold-font PATH`.

Test hooks (used by CI-style captures, harmless otherwise):

```sh
TRIVIA_AUTOPLAY=Name            # pre-fill the name and start
TRIVIA_AUTOANSWER=1             # answer with the first option automatically
./build/sprinkle-trivia --frames N --capture frame.bmp   # headless capture
```

## Build notes

Linux builds use a generic x86-64 baseline; macOS builds use the host architecture. The static binary keeps
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

## Vendored CUI

CUI is pinned in `vendor/UPSTREAM` (upstream commit
`30ec050b784c8901bc1f37a098b48132656eb2fb`). The native bundle includes its
third-party headers, sources, and licenses; `upstream-dependencies.json` records
those dependencies. To refresh, generate a bundle with upstream's
`python3 scripts/native_bundle.py`, then replace this project's `vendor/`,
`sources.mk`, and `upstream-dependencies.json` from that bundle, preserving the
app sources and updating `vendor/UPSTREAM` and `vendor/LICENSE`.
