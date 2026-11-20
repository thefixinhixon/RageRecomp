# RageRecomp (Primal Rage static recompilation) build.
#
#   make                  Linux: ragerecomp (window) + ragerecomp-headless
#   make tests            build and run the unit tests
#   make recomp           developer build with the game code linked in
#                         (from your own ROMs: never share it)
#   make windows          Windows x64 cross build (MinGW)
#   make packages         release packages (see tools/package.sh)
#   make clean
#
# No game code or data is built in: the programs load the player's MAME
# zips at run time and translate the game code on the player's machine.

OUTDIR ?= build-lnx
CC ?= gcc
CFLAGS ?= -O2
EXE ?=
export TMPDIR ?= $(CURDIR)/$(OUTDIR)/tmp
$(shell mkdir -p $(CURDIR)/$(OUTDIR)/tmp)
WFLAGS = -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
         -D_POSIX_C_SOURCE=200809L
INCS = -Isrc -I$(OUTDIR)/gen
SYSLIBS ?= -lm -ldl

CORE_SRCS = \
    src/machine.c src/mem.c src/m68k.c src/c31.c src/cage.c \
    src/rec.c src/rec_build.c src/rec_none.c src/roms.c src/game.c \
    src/eeprom.c src/xga.c src/gtvid.c src/state.c \
    src/m68kdis.c src/c31dis.c src/m68krec.c src/c31rec.c \
    src/paths.c src/launch.c \
    src/third_party/miniz/miniz.c \
    src/third_party/miniz/miniz_tdef.c \
    src/third_party/miniz/miniz_tinfl.c \
    src/third_party/miniz/miniz_zip.c \
    $(OUTDIR)/gen/rec_headers.c

GEN_C = $(OUTDIR)/gen/rec_headers.c
GEN_O = $(OUTDIR)/gen/rec_headers.o $(OUTDIR)/gen/version.o
CORE_OBJS = $(patsubst %.c,$(OUTDIR)/%.o,$(filter-out $(GEN_C),$(CORE_SRCS))) $(GEN_O)

.DEFAULT_GOAL := all

# The program's version: the tag being released on GitHub Actions, else
# `git describe`, else "dev". Rewritten only when it changes.
RR_VERSION ?= $(patsubst v%,%,$(or $(filter v%,$(GITHUB_REF_NAME)),$(shell git describe --tags --always --dirty 2>/dev/null),dev))

$(OUTDIR)/gen/version.c: FORCE
	@mkdir -p $(OUTDIR)/gen
	@echo 'const char rr_version[] = "$(RR_VERSION)";' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

$(OUTDIR)/gen/version.o: $(OUTDIR)/gen/version.c
	$(CC) $(CFLAGS) -c $< -o $@

FORCE:
TESTS = t_rmw t_ea t_an t_mem t_cage t_dsp_boot t_dsp_alu
TEST_BINS = $(addprefix $(OUTDIR)/,$(addsuffix $(EXE),$(TESTS)))

HAVE_SDL ?= $(shell pkg-config --exists sdl2 2>/dev/null && echo yes)
SDL_CFLAGS ?= $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS ?= $(shell pkg-config --libs sdl2 2>/dev/null)

# The windowed frontend (SDL2 + OpenGL), adapted from UltraCombo's.
FRONT_SRCS = src/frontend.c src/config.c src/osd.c src/video.c \
    src/shaders.c src/png.c src/icon.c src/controller_labels.c src/picker.c
FRONT_OBJS = $(patsubst %.c,$(OUTDIR)/%.o,$(FRONT_SRCS))

# Linked-in game code (make recomp): build/rec/*.c from your own ROMs.
# Without it, rec_none.c: the code is built at first launch.
REC_SRCS = $(wildcard build/rec/*.c)
REC_OBJS = $(patsubst build/rec/%.c,$(OUTDIR)/rec/%.o,$(REC_SRCS))

ifeq ($(REC_LINKED),yes)
CORE_OBJS := $(filter-out $(OUTDIR)/src/rec_none.o,$(CORE_OBJS)) $(REC_OBJS)
endif

.PHONY: all tests windows clean recomp FORCE package-linux package-appimage package-windows packages shaders-extra shaders-lola

all: $(OUTDIR)/ragerecomp-headless$(EXE) $(if $(filter yes,$(HAVE_SDL)),$(OUTDIR)/ragerecomp$(EXE))
	@echo "build done in $(OUTDIR)/"

$(OUTDIR)/gen/rec_headers.c $(OUTDIR)/gen/rec_headers.h: tools/embed.py $(wildcard src/*.h)
	@mkdir -p $(OUTDIR)/gen
	python3 tools/embed.py src $(OUTDIR)/gen

$(OUTDIR)/%.o: %.c $(OUTDIR)/gen/rec_headers.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WFLAGS) $(INCS) -c $< -o $@

$(OUTDIR)/gen/rec_headers.o: $(GEN_C)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WFLAGS) $(INCS) -c $< -o $@

$(OUTDIR)/ragerecomp-headless$(EXE): $(CORE_OBJS) $(OUTDIR)/src/main.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(SYSLIBS)

$(OUTDIR)/ragerecomp$(EXE): $(CORE_OBJS) $(FRONT_OBJS) $(OUTDIR)/src/main_sdl.o $(RES_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(SDL_LIBS) $(SYSLIBS)

$(FRONT_OBJS): $(OUTDIR)/%.o: %.c $(wildcard src/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WFLAGS) $(INCS) -DRR_SDL $(SDL_CFLAGS) -c $< -o $@

$(OUTDIR)/src/main.o: src/main.c $(OUTDIR)/gen/rec_headers.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WFLAGS) $(INCS) -c $< -o $@

$(OUTDIR)/src/main_sdl.o: src/main.c $(OUTDIR)/gen/rec_headers.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WFLAGS) $(INCS) -DRR_SDL $(SDL_CFLAGS) -c $< -o $@

$(OUTDIR)/t_%$(EXE): tests/t_%.c $(CORE_OBJS) | $(OUTDIR)/gen/rec_headers.h
	$(CC) $(CFLAGS) $(WFLAGS) -Itests $(INCS) $< $(CORE_OBJS) -o $@ $(LDFLAGS) $(SYSLIBS)

tests: $(TEST_BINS)
	@fail=0; for t in $(TEST_BINS); do \
	    echo "== $$t"; \
	    (cd $(CURDIR) && ./$$t) || fail=1; \
	done; exit $$fail

# Developer build with the game code linked in: translate your own ROMs
# into build/rec (game-derived: never commit or share it), then link it
# instead of rec_none.c. ROMS: the folder holding the zips.
ROMS ?= run
recomp: $(OUTDIR)/ragerecomp-headless$(EXE)
	rm -rf build/rec
	$(OUTDIR)/ragerecomp-headless$(EXE) --roms $(ROMS) --emit-linked build/rec
	$(MAKE) REC_LINKED=yes all

$(OUTDIR)/rec/%.o: build/rec/%.c
	@mkdir -p $(dir $@)
	$(CC) -O1 -w -fno-strict-aliasing -Ibuild/rec -c $< -o $@

# Windows x64 cross build with MinGW. The windowed program needs the SDL2
# MinGW development package in deps/sdl2-mingw (tools/fetch-deps.sh);
# without it only the headless program and the tests are built.
WIN_SDL ?= $(wildcard deps/sdl2-mingw/SDL2-*/x86_64-w64-mingw32)
windows:
	$(MAKE) OUTDIR=build-win CC=x86_64-w64-mingw32-gcc EXE=.exe \
	    SYSLIBS="-lm" HAVE_SDL=$(if $(WIN_SDL),yes,) \
	    SDL_CFLAGS="-I$(WIN_SDL)/include -I$(WIN_SDL)/include/SDL2 -Dmain=SDL_main" \
	    SDL_LIBS="-L$(WIN_SDL)/lib -lmingw32 -lSDL2main -lSDL2 -mwindows -static-libgcc" \
	    build-win/ragerecomp-headless.exe $(if $(WIN_SDL),build-win/ragerecomp.exe) \
	    $(addprefix build-win/,$(addsuffix .exe,$(TESTS)))

# ---------------------------------------------------------------- packages
# Release packages, without game files or game code (tools/package.sh).
# Needs deps/ from tools/fetch-deps.sh: Zig, SDL2 for Linux (built against
# glibc 2.31) and MinGW, TinyCC, AppImage's runtime.

RR_VER_RC := $(or $(shell echo '$(RR_VERSION)' | sed -n 's/^\([0-9]*\)\.\([0-9]*\)\.\([0-9]*\).*/\1,\2,\3,0/p'),0,0,0,0)

# Linux: Zig's clang against glibc 2.31, so it runs on older distributions;
# SDL2 from deps/sdl2-lnx goes in lib/ beside it.
LNX_ZIG ?= deps/zig-linux/zig
LNX_SDL ?= deps/sdl2-lnx/build/libSDL2-2.0.so.0
LNX_SDL_INC ?= deps/sdl2-lnx/build/include/SDL2 deps/sdl2-lnx/build/include-config-release/SDL2
PKG_SRCS = $(filter-out $(GEN_C),$(CORE_SRCS)) $(FRONT_SRCS) src/main.c

build-pkg/ragerecomp: $(PKG_SRCS) src/*.h $(OUTDIR)/gen/rec_headers.c $(OUTDIR)/gen/version.c
	mkdir -p build-pkg/inc/SDL2
	for d in $(LNX_SDL_INC); do ln -sf $(CURDIR)/$$d/*.h build-pkg/inc/SDL2/; done
	$(LNX_ZIG) cc -target x86_64-linux-gnu.2.31 -O2 -w -std=c11 -D_POSIX_C_SOURCE=200809L -DRR_SDL \
	    -Isrc -I$(OUTDIR)/gen -Ibuild-pkg/inc -Ibuild-pkg/inc/SDL2 -o $@ \
	    $(PKG_SRCS) $(OUTDIR)/gen/rec_headers.c $(OUTDIR)/gen/version.c \
	    $(LNX_SDL) -Wl,-rpath,'$$ORIGIN/lib' -lm -ldl

package-linux: build-pkg/ragerecomp
	sh tools/package.sh linux build-pkg/ragerecomp $(LNX_SDL)
	tar -C dist -cJf dist/RageRecomp-Linux.tar.xz RageRecomp-Linux

# AppImage's runtime: github.com/AppImage/type2-runtime (static, needs no libfuse2)
APPIMAGE_RUNTIME ?= deps/dl/appimage-runtime/runtime-x86_64
package-appimage: package-linux
	sh tools/package.sh appimage $(APPIMAGE_RUNTIME)

# Windows: the icon is drawn by the program itself (no game art); any build
# of it for this machine writes the .ico.
ICON_TOOL ?= build-lnx/ragerecomp
build-win/ragerecomp.ico: $(ICON_TOOL)
	@mkdir -p build-win
	$(ICON_TOOL) --write-icon $@

build-win/ragerecomp-res.o: src/ragerecomp.rc build-win/ragerecomp.ico $(OUTDIR)/gen/version.c
	x86_64-w64-mingw32-windres -I build-win -DRR_VERSION_STR='\"$(RR_VERSION)\"' \
	    -DRR_VERSION_RC=$(RR_VER_RC) -o $@ src/ragerecomp.rc

package-windows: build-win/ragerecomp-res.o
	$(MAKE) windows RES_OBJ=build-win/ragerecomp-res.o
	sh tools/package.sh windows build-win/ragerecomp.exe $(WIN_SDL)/bin/SDL2.dll
	cd dist && rm -f RageRecomp-Windows.zip && zip -qr9 RageRecomp-Windows.zip RageRecomp-Windows

packages: package-appimage package-windows

# GPL-licensed libretro shaders (xBRZ, crt-guest, crt-royale, ...) for local
# use: fetched from github.com/libretro/glsl-shaders into deps/.
SHADER_REPO = deps/glsl-shaders
EXTRA_SHADERS = xbrz/xbrz-freescale.glslp xbrz/xbrz-freescale-multipass.glslp xbrz/4xbrz-linear.glslp \
    crt/crt-guest-dr-venom.glslp crt/crt-guest-dr-venom-fast.glslp crt/crt-guest-sm.glslp \
    crt/crt-royale.glslp crt/crt-royale-fake-bloom.glslp crt/crt-geom.glslp crt/crt-easymode.glslp \
    crt/crt-easymode-halation.glslp crt/zfast-crt.glslp crt/crt-aperture.glslp

shaders-extra:
	test -d $(SHADER_REPO) || (git clone -q --depth 1 --filter=blob:none --sparse \
	    https://github.com/libretro/glsl-shaders.git $(SHADER_REPO) && \
	    cd $(SHADER_REPO) && git sparse-checkout set crt xbrz blurs)
	python3 tools/install_shaders.py $(SHADER_REPO) shaders/extra $(EXTRA_SHADERS)

# Lola Retro (MIT): regenerates the bundled shaders/lola-retro from a pinned
# commit of github.com/zoran123456/lola-retro.
LOLA_REPO = deps/lola-retro
LOLA_COMMIT = 330feccbcae72d336479844d22ce156bd390a5bd

shaders-lola:
	test -d $(LOLA_REPO) || git clone -q https://github.com/zoran123456/lola-retro.git $(LOLA_REPO)
	cd $(LOLA_REPO) && (git checkout -q $(LOLA_COMMIT) || (git fetch -q origin && git checkout -q $(LOLA_COMMIT)))
	python3 tools/lola_retro.py $(LOLA_REPO) shaders/lola-retro

clean:
	rm -rf build-lnx build-win build-pkg dist
