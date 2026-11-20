# RageRecomp

Native port of arcade Primal Rage (Atari GT: 68EC020 + TMS320C31 CAGE),
C11 + SDL2 + OpenGL for Linux and Windows. It runs the game's own CPU/DSP
code, statically translated to C, with the hardware around it emulated.
Players supply their own MAME ROMs. README.md is the user manual; this
file is for working on the code. Conventions follow UltraCombo.

## Hard rules: game data and game code

The game's ROMs and code are copyrighted. This repo is public.

- Never commit or ship anything made from the game files:
  - ROM data, RAM snapshots (`build/snap/`), translated code (`build/rec/`,
    any `rec_*.c`, `cache/`);
  - audio recordings (`RR_WAV`), screenshots or frame dumps, save states;
  - any binary built after `make recomp`, which links the game code in.
- Distributable programs contain no game code. They build it on the
  player's machine at first launch (`rec_build.c`). Release packages come
  only from `make packages`.
- Never `git add -A` / `git add .`: the working folder holds builds with
  game code and test output. Add files by name and read `git status` first.
- Name the product RageRecomp. Use "Primal Rage" only to say what it
  plays, never as the product's name. It is a trademark.

## Layout

| Area | Files |
|---|---|
| What differs per set: names, ROM tables, CRCs, start-button layout, DSP speedup | `game.h`, `game.c` |
| 68EC020: decode, interpreter, translator | `m68k.h`, `m68k_core.h`, `m68k.c`, `m68kdis.c`, `m68krec.h`, `m68krec.c` |
| TMS320C31: decode, interpreter, translator | `c31.h`, `c31_core.h`, `c31.c`, `c31dis.c`, `c31rec.h`, `c31rec.c` |
| Contract between program and translated code | `rec_abi.h`, `rec_gen.h` |
| Translated code: dispatcher, maps | `rec.c/h` |
| First-run build and cache | `rec_build.c/h`; `rec_none.c` = no linked-in code |
| Memory map and I/O | `mem.c/h`, `machine.h` |
| CAGE sound board: DMA, timers, serial, host comm, DAC mix | `cage.c/h` |
| GT video: tiles, alpha, RLE motion objects, composite | `gtvid.c/h` |
| Protection (XGA 136094-0004A) | `xga.c/h` |
| EEPROM 2816 (kept across power cycles; `saves/<set>.eeprom`) | `eeprom.c/h`, `main.c` |
| MAME zip loading, interleave, CRC checks, missing-file UX | `roms.c/h` |
| Save states | `state.c/h` |
| Window, menu, pacing, input, audio (adapted from UltraCombo) | `frontend.c`, `osd.c/h`, `config.c/h`, `controller_labels.c/h`, `screen.h` (336x240) |
| Video, OpenGL, shader presets | `video.c/h`, `shaders.c/h`, `gl_funcs.h` |
| Icon (drawn in code) and PNG writer | `icon.c/h`, `png.c/h` |
| Welcome screen (no game files) and message window | `picker.c/h` |
| Program entry, CLI, game version, NVRAM, headless runs | `main.c`, `app.h` |
| `base_dir()`/`data_dir()`/`find_data()` (no SDL: the headless program too) | `paths.c/h` |
| Started with a game path (a set's zip or a folder): its folder is searched first | `launch.c/h` |
| Packaging | `tools/package.sh`, `tools/fetch-deps.sh`, `src/ragerecomp.rc`, `.github/workflows/release.yml` |

## Hardware model (from MAME's atarigt/cage drivers; see NOTICE)

- Main CPU 68EC020 @ 25 MHz. IRQ6 = 250 Hz scanline tick, IRQ4 = vblank,
  IRQ3 = CAGE sound. Autovectors.
- Map: ROM 0x000000-0x1FFFFF; CAGE 0xC00000; EEPROM 0xD20000 (+unlock at
  0xD40000); work RAM 0xD70000-0xD7FFFF (playfield tilemap at 0xD72000,
  alpha at 0xD76000, MO params at 0xD78000, MO command at 0xD7A200);
  colour RAM + XGA overlay 0xD80000-0xDFFFFF; latches 0xE08xxx; inputs
  0xE80000/0xE8200x; main RAM 0xF80000-0xFFFFFF.
- Video 336x240 @ ~59.92 Hz. 6bpp playfield (128x64, row scrolls from
  alpha RAM), 4bpp alpha (64x32), RLE motion objects into two 16-bit
  VRAMs, CRAM+MRAM composite with the lightning LCR override.
- CAGE: TMS320C31 @ 33.8688 MHz (MCBL boot from boot ROM), 64K-word RAM,
  DMA into the serial port feeding 4 DAC channels @ ~22050 Hz, mixed to
  stereo (L = ch1+ch2, R = ch0+ch3). No subwoofer (not in MAME either).

## CPU timing

- The 68K runs cycle-counted; `machine_frame` interleaves CPU and DSP at
  scanline granularity and takes the picture at vblank, as MAME draws it.
- 68K counts follow MAME/Musashi's 020 table (`m68k_in.lst`, cache-hit
  flavour): ALU ea-to-Dn 2, ALU/Dn-to-ea mem-dst 4, EOR Dn,Dm 2,
  MOVE/ADDQ reg 2, LSR/LSL reg 4, LINK.W 5, RTS 10, MOVEM base 4/8 plus
  4/2 per L/W register. Pinned by cycle asserts in `tests/t_ea.c`.
  Shifting any hot-op cost by a cycle moves boot by frames.
- The C31 charges MAME's counts (1 base, +3 branches/calls/rets/RPTS,
  +4 trap). DSP DMA/timer periods are in DSP cycles off the same
  16.9344 MHz base as MAME's H1-derived attotime values; verified equal.
- Translated and interpreted runs are bit-identical: translated code runs
  each instruction exactly as the interpreter's step does (shared
  semantics, cycles, then a check for slice end, faults and interrupts;
  on the DSP also repeat blocks, delay slots and the per-instruction
  interrupt check). So the regression hash is the same with or without
  `RR_NOREC`/`RR_NODSP`, and any difference is a bug.

## Determinism

- Emulation must be bit-identical across compilers (gcc, clang, MinGW,
  TinyCC modules) and across linked-in vs run-time-built code.
- `memset` any local struct before copying it into a state blob.
- After any change to emulation, run the regression test; the hash must
  not change. If a change is meant to alter emulation, say so and record
  the new hash. `state_compat_id()` must differ between builds that
  emulate differently.

## Translated-code modules

- `rec_abi.h`, `rec_gen.h`, `m68k_core.h`, `c31_core.h` are compiled into
  the module **without the C library** (`REC_MODULE`, `-nostdlib`). Keep
  them free of libc headers and calls, and make everything work under
  TinyCC too.
- They are embedded into the program (`tools/embed.py` →
  `build/rec_headers.c`), so rebuilding the program picks up changes.
- Bump `REC_ABI_VERSION` (`rec_abi.h`) when the program/module contract
  changes, and `RECOMP_VERSION` (`rec_build.c`) when the generated C
  changes. Both are in the cache key.
- Compiler order at first launch: bundled Zig (`tools/zig`) → system
  cc/gcc/clang → TinyCC loaded as a library (`tools/tcc`, compiles into
  memory in seconds; a `use-tinycc` marker makes later launches use it
  straight away) → interpreter. One module holds both CPUs, cached as
  `cache/rec-<key>.so|.dll` with its sources in `cache/src-<key>/`. All
  must give the same behaviour (verified: gcc, clang, Zig, TinyCC on
  Linux, TinyCC on Windows under Wine).
- Keep instruction semantics in `m68k_core.h` / `c31_core.h` only; the
  interpreters and the translators share them.
- Every instruction start is an entry label; the dispatchers (`rec.c`)
  map a pc straight to its function and fall back to the interpreter only
  where nothing was translated (`RR_INTERP_HIST=file` lists where, with
  counts; it should stay empty).
- Code discovery: descent from the vectors, compiled switch tables, then
  pointer candidates (operands, every long in ROM / word in DSP RAM) that
  decode plausibly. Guesses may overlap; the emitters jump explicitly
  wherever the next emitted instruction is not the real successor.
- Translated DSP code runs only while DSP memory still holds the words it
  was translated from: a write changing a code word bumps `dsp_gen`, and
  the dispatcher re-checks each function against the module's image.

## Product rules

- Enhancements are options on top of authentic defaults. Keep the boot and
  attract intros: they are part of the game.
- Graphics stay on OpenGL; no Vulkan.
- No "resume where you left off" on launch.
- Settings live in the table in `config.c` (`MAX_OPTS`), and
  `ragerecomp.ini` is written from it with comments. The game's own
  settings (coinage, free play, difficulty) stay in the cabinet's EEPROM,
  changed through its test mode (the test-switch hotkey, F2).
  `recomplauncher.json` (shipped in the packages) shows some of them in
  RecompLauncher: change it too when one of those is renamed or its values
  change.
- Writable files go in `data_dir()`: `--data-dir` if given, else beside
  the program if that's writable, else the user data folder `RageRecomp`.
  Nothing is written anywhere else.
- Every setting can be given on the command line (`--set section.key=value`,
  `--fullscreen`, `--windowed`) for one session.
- Bundled shaders in `shaders/` must be permissively licensed. GPL presets
  are only fetched by `make shaders-extra`.

## Workflows

Build and run (needs gcc, SDL2, python3; game files in `run/`):

    make                 # build-lnx/ragerecomp (+ ragerecomp-headless); game code is built at first launch
    make recomp ROMS=run # developer build with the game code linked in (never share it)
    build-lnx/ragerecomp

Regression test: a fixed 3600-frame boot, coin-up and fight, printing a
state hash:

    tools/regress.sh PROGRAM SET [RUN_DIR] [WORK_DIR]

The hash is the same for translated and interpreted runs, for every
compiler and for linked-in code; current value (primrage, RECOMP_VERSION
6, STATE_FORMAT 3): `4dbfad98fe3f0aac`.
Useful environment variables:

- `RR_NOREC=1`: 68K on the interpreter; `RR_NODSP=1`: DSP on the
  interpreter. Same hash either way.
- `RR_INTERP_HIST=file`: where the interpreters ran while translated code
  was active (missing entry points), plus instruction totals.
- `RR_LOAD_MODULE=path.so`: use a given module.
- `RR_BUILD=1 RR_BUILD_WITH=zig|cc|gcc|clang|tcc`: rebuild the module now
  with that compiler (the cache is in the data folder; `--data-dir`).
- `RR_DISPLAY_HZ=N`: pretend the display refreshes at N Hz, to test frame
  pacing (under Xvfb vsync never waits, so ~60 Hz falls back to audio).
- `RR_VIRTUAL_PADS="Name,?Name"`: attach SDL virtual gamepads (testing).
- `RR_NO_GL=1`: the SDL renderer instead of OpenGL. `RR_CONSOLE=1`: on
  Windows, keep the log on the console instead of `ragerecomp.log`.
- `RR_LOG_FRAME=2`: 68K position after every scanline (for bisecting a
  divergence between two runs); `RR_DUMP_DSP=file`: the DSP image the
  translator planned from.

Packages (no game files, no game code):

    make packages        # dist/RageRecomp-Windows.zip, -Linux.tar.xz, -x86_64.AppImage (+ .zsync)

Pushing a version tag builds them on GitHub Actions
(`.github/workflows/release.yml`) into a draft release.
`tools/fetch-deps.sh` fills `deps/` from pinned, checksummed downloads.
See README.md, "Release packages". The Linux program targets glibc 2.31.

Windows: `make windows` cross-compiles with MinGW (the windowed program
needs `deps/sdl2-mingw`). Test the Windows module path with
`RR_BUILD_WITH=tcc` under Wine (`tools/tcc/libtcc.dll` and `chkstk.S`
beside the program); the Zig and cc paths need real Windows.

Windowed testing without a display: run it under `Xvfb :99` with
`SDL_AUDIODRIVER=dummy`, and send keys with `xdotool keydown` / `keyup`
held a few frames, because input is sampled once per frame and quick `key`
taps get missed. Run a copy of the program from a scratch folder so it gets
its own `ragerecomp.ini`.

Headless switches: see `usage()` in `main.c` (`--headless`/`--frames`,
`--script`, `--hash`, `--shot`, `--wav`, `--save-at`, `--load`, `--nvram`,
`--emit-linked`). Headless runs start from a blank EEPROM unless `--nvram`
is given, so they are reproducible. Debug environment variables are all
read with `getenv` in `src/` (grep them for details).

## Status (kept honest: update when it changes)

- 68EC020 interpreter + translator: complete ISA incl. 020 addressing
  modes, exceptions, autovector interrupts. The translator finds 81,915
  instructions (all code below 0x50000); a 3600-frame coin-up and fight
  runs with zero interpreted instructions.
- CAGE DSP interpreter + translator: complete C31 ISA incl. parallel
  ops, delay slots, DMA/timers/serial/host comm; float ALU verified
  bit-identical to MAME's integer model on probed vectors. The translator
  finds 13,143 instructions (RAM, IRAM and the boot ROM's routines); zero
  interpreted instructions in the same run.
- Parity: translated, partly translated and fully interpreted runs give
  the same regression hash; so do gcc, clang, Zig and TinyCC modules,
  TinyCC under Wine, and the linked-in build.
- GT video, CAGE audio path (music and effects play; the opening attract
  is quiet in MAME too), protection, EEPROM (persisted per set): from MAME.
- Frontend (from UltraCombo): menu, ragerecomp.ini, OpenGL with built-in
  filters and libretro presets, gamepads with assignment and labels, save
  states, screenshots, pacing, welcome screen, launcher paths. Played
  under Xvfb (menu, filter change, coin-up and a won round from the
  keyboard, save/load state, screenshot, quit). Not here: rewind,
  run-ahead, netplay, auto-coin, rumble, training (UltraCombo has them;
  they need KI-specific RAM knowledge or code not ported).
- Game versions: primrage played; primrageo/primrage20 wired (layouts per
  MAME's input ports) but untested for lack of their zips.
- Verified: 244 unit tests pass on Linux and under Wine; Linux + Windows
  builds clean; `make packages` built and the AppImage (Zig, 83 s first
  launch) and Windows zip (under Wine, TinyCC) both start and play.
- CI and release workflows written but never executed on GitHub Actions.

## Style

Match the surrounding code: C11, 4-space indent, short functions with short
comments that say why, British spelling in prose ("licence", "colour"). The
`-Wextra` warnings already in the build are known. Don't add new ones.
