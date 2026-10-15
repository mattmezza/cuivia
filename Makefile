# Sprinkle Trivia — native C build on top of the vendored CUI bundle.
#
#   make            fully static executable: build/sprinkle-trivia
#                   (vendored SDL3/FreeType/HarfBuzz/mbedTLS, zero .so deps)
#   make dev        quick iteration build against system SDL3/FreeType/HarfBuzz
#                   (still needs third_party mbedTLS; see make vendor-net)
#   make vendor     download + build all third-party static libs
#
# Code lives entirely in this directory (vendor/ and third_party/src included).
.DEFAULT_GOAL := dev
CC ?= cc
AR ?= ar
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -g
# Generic x86-64 baseline: no -march=native, no host tuning.
CFLAGS += -march=x86-64 -mtune=generic
APP_SOURCES := main.c app.c game.c json.c net.c embedded_fonts.c
TARGET_STATIC := build/sprinkle-trivia
TARGET_DEV := build/sprinkle-trivia-dev
EMBED_SOURCES := embedded_fonts_data.S
APP_OBJECTS := $(APP_SOURCES:%.c=build/%.o) $(EMBED_SOURCES:%.S=build/%.o)
CPPFLAGS += -Ivendor/include -Ivendor/third_party/clay
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic

MBEDTLS_LIBS := third_party/build/mbedtls/library/libmbedtls.a \
                third_party/build/mbedtls/library/libmbedx509.a \
                third_party/build/mbedtls/library/libmbedcrypto.a
CPPFLAGS += -Ithird_party/src/mbedtls-3.6.4/include

SDL_STATIC_LIB := third_party/install/sdl/lib/libSDL3.a
FREETYPE_STATIC_LIB := third_party/install/freetype/lib/libfreetype.a
HARFBUZZ_STATIC_LIB := third_party/install/harfbuzz/lib/libharfbuzz.a

# Prefer vendored headers once vendored, so dev and static objects agree.
ifneq ($(wildcard third_party/install/sdl/include/SDL3/SDL.h),)
CPPFLAGS += -Ithird_party/install/sdl/include
endif
ifneq ($(wildcard third_party/install/freetype/include/freetype2/freetype/freetype.h),)
CPPFLAGS += -Ithird_party/install/freetype/include/freetype2
endif
ifneq ($(wildcard third_party/install/harfbuzz/include/harfbuzz/hb.h),)
CPPFLAGS += -Ithird_party/install/harfbuzz/include
endif

# CUI bundle prerequisites (pkg-config is only needed for system dev libraries).
ifeq ($(shell $(PKG_CONFIG) --exists 'sdl3 >= 3.2' freetype2 harfbuzz && echo yes),)
$(error For `make dev` install SDL3 >= 3.2, FreeType and HarfBuzz development packages)
endif
DEV_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl3 freetype2 harfbuzz)
DEV_LDLIBS := $(shell $(PKG_CONFIG) --libs sdl3 freetype2 harfbuzz) -lm

include sources.mk
PORTABLE_OBJECTS := $(PORTABLE_SOURCES:%.c=build/%.o)
TEXT_OBJECTS := $(TEXT_SOURCES:%.c=build/%.o)
BIDI_OBJECTS := $(BIDI_SOURCES:%.c=build/%.o) $(SHEENBIDI_SOURCES:%.c=build/%.o)
SDL3_OBJECTS := $(SDL3_SOURCES:%.c=build/%.o)
OBJECTS := $(PORTABLE_OBJECTS) $(TEXT_OBJECTS) $(BIDI_OBJECTS) $(SDL3_OBJECTS)
LIBRARIES := build/vendor/libcui_sdl3.a build/vendor/libcui_text.a build/vendor/libcui_bidi.a build/vendor/libcui.a

dev: $(TARGET_DEV)
static: $(TARGET_STATIC)
all: static

check: build/test_logic
	./build/test_logic

build/test_logic: tests/test_logic.c game.c json.c trivia.h json.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tests/test_logic.c game.c json.c -lm -o $@

$(TARGET_DEV): $(APP_OBJECTS) $(LIBRARIES) $(MBEDTLS_LIBS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(APP_OBJECTS) $(LIBRARIES) $(MBEDTLS_LIBS) $(DEV_LDLIBS) -lpthread -o $@

# Fully static link: vendored SDL3 (dlopen-based X11/Wayland at runtime),
# FreeType, HarfBuzz and mbedTLS. No .so dependencies remain in the binary.
STATIC_LDLIBS := -lstdc++ -lm -lpthread -ldl -lrt
# net.c: avoid getaddrinfo/NSS dlopen paths (safe in static binaries).
CPPFLAGS += -DTRIVIA_STATIC
$(TARGET_STATIC): $(APP_OBJECTS) $(LIBRARIES) $(SDL_STATIC_LIB) $(FREETYPE_STATIC_LIB) \
                  $(HARFBUZZ_STATIC_LIB) $(MBEDTLS_LIBS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -static $(APP_OBJECTS) $(LIBRARIES) $(SDL_STATIC_LIB) \
	  $(FREETYPE_STATIC_LIB) $(HARFBUZZ_STATIC_LIB) $(MBEDTLS_LIBS) $(STATIC_LDLIBS) -o $@

build/vendor/libcui.a: $(PORTABLE_OBJECTS)
	$(AR) rcs $@ $^
build/vendor/libcui_text.a: $(TEXT_OBJECTS)
	$(AR) rcs $@ $^
build/vendor/libcui_bidi.a: $(BIDI_OBJECTS)
	$(AR) rcs $@ $^
build/vendor/libcui_sdl3.a: $(SDL3_OBJECTS)
	$(AR) rcs $@ $^

$(TEXT_OBJECTS): CPPFLAGS += -DCUI_TEXT_BIDI
$(BIDI_OBJECTS): CPPFLAGS += -Ivendor/third_party/sheenbidi/Headers
$(SHEENBIDI_SOURCES:%.c=build/%.o): CPPFLAGS += -DSB_CONFIG_UNITY -Ivendor/third_party/sheenbidi/Source
# Narrow suppressions match CMake for unmodified pinned upstream SheenBidi.
$(SHEENBIDI_SOURCES:%.c=build/%.o): UPSTREAM_WARNINGS := -Wno-unused-parameter -Wno-unused-value -Wno-unused-function -Wno-implicit-fallthrough

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(DEV_CFLAGS) $(CFLAGS) $(UPSTREAM_WARNINGS) -MMD -MP -c $< -o $@

build/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ---- third-party vendoring (sources stay inside third_party/) ----
THIRD_PARTY := third_party
vendored: $(SDL_STATIC_LIB) $(FREETYPE_STATIC_LIB) $(HARFBUZZ_STATIC_LIB) $(MBEDTLS_LIBS)

$(MBEDTLS_LIBS): third_party/src/mbedtls-3.6.4
	cmake -S third_party/src/mbedtls-3.6.4 -B third_party/build/mbedtls -G Ninja \
	  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-O2 -march=x86-64" \
	  -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DUSE_SHARED_MBEDTLS=OFF
	cmake --build third_party/build/mbedtls

third_party/src/mbedtls-3.6.4:
	mkdir -p third_party/src
	curl -sL -o third_party/mbedtls.tar.bz2 \
	  https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.4/mbedtls-3.6.4.tar.bz2
	tar -xjf third_party/mbedtls.tar.bz2 -C third_party/src
	rm third_party/mbedtls.tar.bz2

$(SDL_STATIC_LIB): third_party/src/sdl
	cmake -S third_party/src/sdl -B third_party/build/sdl -G Ninja \
	  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-O2 -march=x86-64" \
	  -DCMAKE_INSTALL_PREFIX=third_party/install/sdl \
	  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF \
	  -DSDL_EXAMPLES=OFF \
	  -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF -DSDL_RENDER_VULKAN=OFF \
	  -DSDL_RENDER_GPU=OFF
	cmake --build third_party/build/sdl
	cmake --install third_party/build/sdl

third_party/src/sdl:
	mkdir -p third_party/src
	curl -sL -o third_party/sdl.tar.gz \
	  https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
	tar -xzf third_party/sdl.tar.gz -C third_party/src
	rm third_party/sdl.tar.gz
	mv third_party/src/SDL3-3.4.18 third_party/src/sdl

$(FREETYPE_STATIC_LIB): third_party/src/freetype
	cmake -S third_party/src/freetype -B third_party/build/freetype -G Ninja \
	  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-O2 -march=x86-64" \
	  -DCMAKE_INSTALL_PREFIX=third_party/install/freetype \
	  -DBUILD_SHARED_LIBS=OFF \
	  -DFT_DISABLE_ZLIB=TRUE -DFT_DISABLE_BZIP2=TRUE -DFT_DISABLE_PNG=TRUE \
	  -DFT_DISABLE_BROTLI=TRUE -DFT_DISABLE_HARFBUZZ=TRUE
	cmake --build third_party/build/freetype
	cmake --install third_party/build/freetype

third_party/src/freetype:
	mkdir -p third_party/src
	curl -sL -o third_party/freetype.tar.xz \
	  https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz
	tar -xJf third_party/freetype.tar.xz -C third_party/src
	rm third_party/freetype.tar.xz
	mv third_party/src/freetype-2.14.3 third_party/src/freetype

$(HARFBUZZ_STATIC_LIB): third_party/src/harfbuzz $(FREETYPE_STATIC_LIB)
	rm -rf third_party/build/harfbuzz third_party/install/harfbuzz
	PKG_CONFIG_PATH="$(CURDIR)/third_party/install/freetype/lib/pkgconfig" \
	  cmake -S third_party/src/harfbuzz -B third_party/build/harfbuzz -G Ninja \
	  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O2 -march=x86-64" \
	  -DCMAKE_C_FLAGS="-O2 -march=x86-64" \
	  -DCMAKE_INSTALL_PREFIX=third_party/install/harfbuzz \
	  -DCMAKE_PREFIX_PATH="$(CURDIR)/third_party/install/freetype" \
	  -DBUILD_SHARED_LIBS=OFF -DHB_BUILD_SHARED=OFF \
	  -DHB_HAVE_FREETYPE=ON -DHB_HAVE_GLIB=OFF -DHB_HAVE_ICU=OFF \
	  -DHB_BUILD_SUBSET=OFF -DHB_BUILD_TESTS=OFF -DHB_BUILD_UTILS=OFF \
	  -DHB_HAVE_GOBJECT=OFF
	cmake --build third_party/build/harfbuzz
	cmake --install third_party/build/harfbuzz

third_party/src/harfbuzz:
	mkdir -p third_party/src
	curl -sL -o third_party/harfbuzz.tar.xz \
	  https://github.com/harfbuzz/harfbuzz/releases/download/14.6.0/harfbuzz-14.6.0.tar.xz
	tar -xJf third_party/harfbuzz.tar.xz -C third_party/src
	rm third_party/harfbuzz.tar.xz
	mv third_party/src/harfbuzz-14.6.0 third_party/src/harfbuzz

.PHONY: all dev static check vendored
-include $(OBJECTS:.o=.d) $(APP_OBJECTS:.o=.d)
