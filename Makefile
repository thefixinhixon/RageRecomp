# RageRecomp (Primal Rage static recompilation) build.
#
#   make                  Linux: headless + SDL binaries + tests
#   make tests            build and run the unit/regression suite
#   make windows          Windows x64 cross build (mingw, headless + tests)
#   make dist             stage a distributable tree in dist/
#   make clean
#
# ROMs are never built in: the binaries load primrage.zip (+ optional
# tms32031.zip) from a user-supplied directory at runtime.

OUTDIR ?= build-lnx
CC ?= gcc
CFLAGS ?= -O2
EXE ?=
export TMPDIR ?= $(CURDIR)/$(OUTDIR)/tmp
$(shell mkdir -p $(CURDIR)/$(OUTDIR)/tmp)
WFLAGS = -std=c11 -Wall -Wextra -Wno-unused-parameter \
         -D_POSIX_C_SOURCE=200809L
INCS = -Isrc -I$(OUTDIR)/gen
SYSLIBS ?= -lm -ldl

CORE_SRCS = \
    src/machine.c src/mem.c src/m68k.c src/c31.c src/cage.c \
    src/rec.c src/rec_build.c src/rec_none.c src/roms.c src/game.c \
    src/eeprom.c src/xga.c src/gtvid.c src/state.c \
    src/m68kdis.c src/c31dis.c src/m68krec.c src/c31rec.c \
    src/third_party/miniz/miniz.c \
    src/third_party/miniz/miniz_tdef.c \
    src/third_party/miniz/miniz_tinfl.c \
    src/third_party/miniz/miniz_zip.c \
    $(OUTDIR)/gen/rec_headers.c

GEN_C = $(OUTDIR)/gen/rec_headers.c
GEN_O = $(OUTDIR)/gen/rec_headers.o
CORE_OBJS = $(patsubst %.c,$(OUTDIR)/%.o,$(filter-out $(GEN_C),$(CORE_SRCS))) $(GEN_O)
TESTS = t_rmw t_ea t_an t_mem t_cage t_dsp_boot t_dsp_alu
TEST_BINS = $(addprefix $(OUTDIR)/,$(addsuffix $(EXE),$(TESTS)))

HAVE_SDL := $(shell pkg-config --exists sdl2 2>/dev/null && echo yes)
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)

.PHONY: all tests windows dist clean

all: $(OUTDIR)/ragerecomp-headless$(EXE) $(if $(filter yes,$(HAVE_SDL)),$(OUTDIR)/ragerecomp$(EXE))
	@echo "build done in $(OUTDIR)/"

$(OUTDIR)/gen/rec_headers.c $(OUTDIR)/gen/rec_headers.h: tools/embed.py $(wildcard src/*.h)
	@mkdir -p $(OUTDIR)/gen
	python3 tools/embed.py src $(OUTDIR)/gen

$(OUTDIR)/%.o: %.c $(OUTDIR)/gen/rec_headers.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WFLAGS) $(INCS) -c $< -o $@

$(GEN_O): $(GEN_C)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WFLAGS) $(INCS) -c $< -o $@

$(OUTDIR)/ragerecomp-headless$(EXE): $(CORE_OBJS) $(OUTDIR)/src/main.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(SYSLIBS)

$(OUTDIR)/ragerecomp$(EXE): $(CORE_OBJS) $(OUTDIR)/src/main_sdl.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(SDL_LIBS) $(SYSLIBS)

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

# Windows x64 cross build (core + headless + tests; SDL needs
# mingw-compatible SDL2 dev files, so the SDL frontend is Linux-only
# until those are provided).
windows:
	$(MAKE) OUTDIR=build-win CC=x86_64-w64-mingw32-gcc EXE=.exe \
	    SYSLIBS="-lm" HAVE_SDL= build-win/ragerecomp-headless.exe \
	    $(addprefix build-win/,$(addsuffix .exe,$(TESTS)))

dist: all
	@mkdir -p dist
	cp $(OUTDIR)/ragerecomp-headless dist/ 2>/dev/null || true
	cp $(OUTDIR)/ragerecomp dist/ 2>/dev/null || true
	cp README.md NOTICE dist/ 2>/dev/null || true
	cp recomplauncher.json dist/ 2>/dev/null || true
	@echo "staged in dist/ (add your own primrage.zip + tms32031.zip)"

clean:
	rm -rf build-lnx build-win dist
