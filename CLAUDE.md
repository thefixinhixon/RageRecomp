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
| EEPROM 2816 (kept across power cycles; `saves/<set>.eeprom`); chip bytes at even addresses | `eeprom.c/h`, `mem.c`, `main.c` |
| Operator settings in the EEPROM (record code, option fields), Game page rows | `adjust.c/h` |
| MAME set loading (.zip via miniz, .7z via the LZMA SDK; told apart by content), interleave, CRC checks, missing-file UX | `roms.c/h`, `third_party/lzma/` |
| Save states | `state.c/h` |
| Window, menu, pacing, input, audio (adapted from UltraCombo) | `frontend.c`, `osd.c/h`, `config.c/h`, `controller_labels.c/h`, `screen.h` (336x240) |
| Video, OpenGL, shader presets | `video.c/h`, `shaders.c/h`, `gl_funcs.h` |
| Icon (drawn in code) and PNG writer | `icon.c/h`, `png.c/h` |
| Welcome screen (no game files) and message window | `picker.c/h` |
| Program entry, CLI, game version, NVRAM, headless runs and test tools | `main.c`, `app.h` |
| Cabinet controls to ports per set (frontend, scripts, netplay) | `controls.c/h` |
| Training mode, health/credits from game RAM (`game.h` locations) | `training.c/h` |
| Rollback netplay over UDP (EEPROM synced instead of KI's disk) | `netplay.c/h`, `net.c/h` |
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
- Each CPU's overrun past its scanline slice is carried into the next
  slice, as MAME's scheduler does. The DSP speedup (writes to its idle
  word burn 100 cycles) overruns by ~31 cycles a line; dropping that ran
  the sound board 3% fast (music tempo and pitch). The DSP's 250 Hz tick
  count now tracks MAME's to within a few ticks over 40 s.
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
  straight away, unless a compiler its `tried:` line doesn't name has
  turned up since, e.g. Zig copied in: then it rebuilds once) → interpreter.
  TinyCC's code is much slower: on a Celeron J4125 it misses 60 fps where
  Zig's holds it (~11 ms a frame in fights; Zig's first build there: 597 s). One module holds both CPUs, cached as
  `cache/rec-<key>.so|.dll` with its sources in `cache/src-<key>/`. All
  must give the same behaviour (verified: gcc, clang, Zig, TinyCC on
  Linux, TinyCC on Windows under Wine; the bundled Zig also builds and
  plays on real Windows, v1.0.0, reported by the user).
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
- Without tms32031.zip the sound program can't boot: the module then has
  an empty DSP half (the DSP stays on the interpreter) and the 68K is
  still translated. The boot ROM is in the cache key, so adding it later
  rebuilds with the DSP.
- The cache key also hashes the embedded headers (instruction semantics),
  so fixing an instruction rebuilds every player's cached module.
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
  settings (coinage, free play, difficulty, round time, gore, attract
  sound) stay in the
  cabinet's EEPROM: the menu edits them there the way the game's test menu
  stores them (`adjust.c`), held until the next power-on; not in the ini.
  The rest stays in the game's test mode (the test-switch hotkey, F2).
  `recomplauncher.json` (shipped in the packages; RecompLauncher level 3)
  shows ini settings (`Section/Key`) and the operator settings (as
  `--operator` arguments) in RecompLauncher: change it too when one of
  those is renamed or its values change, and check it against
  https://recomplauncher.com/schema/recomplauncher.json. Its `prepare`
  step is `--prepare` (no window; `progress F TEXT` lines on stdout; one
  child process per version present; exit 3 = no game files);
  `package.sh` puts the release version in `prepare.version`.
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

`make` links `shaders/` into `build-lnx/` (and `make windows` into
`build-win/`): the menu only lists presets in a `shaders/` folder beside
the program.

Regression test: a fixed 3600-frame boot, coin-up and fight, printing a
state hash:

    tools/regress.sh PROGRAM SET [RUN_DIR] [WORK_DIR]   # SET: primrage, primrageo or primrage20

The hash is the same for translated and interpreted runs, for every
compiler and for linked-in code; current value (primrage, STATE_FORMAT 8): `39e17cc18b311d06`. The state no longer folds in the
translator version (translated = interpreted), and leaves out the DAC
output queue (host audio, not emulated state), so playing or recording
sound doesn't change it. Rewind, run-ahead and netplay rollback
(`state_checkpoint`/`state_rewind`/`state_rollback`) diff the serialized
state per 4 KB page; `--rewind-test N` and `--runahead N` check them, and
`--net-host`/`--net-join` run two-process netplay tests.
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
- As UltraCombo's `KI2_*`: `RR_DUMP_AT=F[,F...]` (RAM dumps: 68K RAM,
  bank, and the sound DSP's 64K-word RAM as `dsp_NNNNN.bin`), `RR_POKE=
  F:ADDR:VAL`, `RR_WAV`, `RR_SPEED`, `RR_HISTORY`, `RR_RESET_TEST=N` (a
  reset after N frames must equal a fresh power-on), `RR_DESYNC_AT`
  (netplay), `RR_DUMP_ON_EXC`, `RR_COVER` (interpreter coverage),
  `RR_TRAINING=F` (windowed program's headless mode), `RR_ADJUST=
  difficulty=16,free_play=1,round_time=80,game_cost=2,gore=1,attract_sound=3` (operator
  settings into the EEPROM image before power-on; it must hold the game's
  settings already: boot once with `--nvram` for 1500 frames).
- `RR_LOG_FRAME=2`: 68K position after every scanline (for bisecting a
  divergence between two runs); `RR_DUMP_DSP=file`: the DSP image the
  translator planned from.

Packages (no game files, no game code):

    make packages        # dist/RageRecomp-vVER-Windows-x64.zip, -Linux-x64.tar.xz, -Linux-x64.AppImage (+ .zsync)

Packages are made only from committed work: the version is `git describe`,
and with uncommitted changes it ends in `-dirty`, so the package targets
stop (`ALLOW_DIRTY=1` for a throwaway test build). They always include the
GPL extra shaders (`make shaders-extra` runs if `shaders/extra` is
missing), as the release workflow does: 30 presets, `--test-shaders`
loads every one. Pushing a version tag builds them on GitHub Actions
(`.github/workflows/release.yml`) into a draft release, published as a
full release unless it's a test build. Release notes follow UltraCombo's
layout: what's new (with thanks to reporters), save-state and netplay
compatibility, "No game files are included", steps per platform and
RecompLauncher, the trademark line.
`tools/fetch-deps.sh` fills `deps/` from pinned, checksummed downloads.
See README.md, "Release packages". The Linux program targets glibc 2.31.

Windows: `make windows` cross-compiles with MinGW (the windowed program
needs `deps/sdl2-mingw`). Test the Windows module path with
`RR_BUILD_WITH=tcc` under Wine (`tools/tcc/libtcc.dll` and `chkstk.S`
beside the program). Zig fails under Wine (its cache needs file locking
Wine lacks) but works on real Windows; a system cc on Windows is untested.

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
  modes, exceptions, autovector interrupts. The translator finds 81,923
  instructions in primrage (80,535 in primrageo, 75,814 in primrage20; all
  code below 0x50000); a 3600-frame coin-up and fight runs with zero
  interpreted instructions in each.
- CAGE DSP interpreter + translator: complete C31 ISA incl. parallel
  ops, delay slots, DMA/timers/serial/host comm; float ALU verified
  bit-identical to MAME's integer model on probed vectors. LDIcond/LDFcond
  and LDI||STI/LDF||STF leave the flags alone (TI's guide, MAME); until
  STATE_FORMAT 7 they set them, which stalled the music sequencer
  (`$2833`: CMPI, LDIU 0, LDIGE 1), so no music played. The translator
  finds 13,143 instructions (RAM, IRAM and the boot ROM's routines); zero
  interpreted instructions in the same run.
- Parity: translated, partly translated and fully interpreted runs give
  the same regression hash; so do gcc, clang, Zig and TinyCC modules,
  TinyCC under Wine, and the linked-in build.
- GT video, CAGE audio path, protection, EEPROM (persisted per set): from
  MAME. Stage scenery (ice cliffs, Stonehenge's stones) is large RLE
  motion objects; until STATE_FORMAT 8 any object starting left of the
  screen was dropped (a pointer offset by `(uint32_t)sx`). Checked against
  MAME: same object lists at a stage edge, same picture. Attract frames
  match MAME pixel for pixel until the demo fights diverge (frame ~2070). Music and effects checked against MAME 0.289 (flatpak; its DSP
  boot set is named `tms320c31.zip`) on a scripted coin-up: same command
  stream from the 68K, same music cues and timing, similar levels. The
  attract is silent in both: the operator setting "attract volume"
  defaults to mute.
  The EEPROM sits on the even byte lane (MAME's `umask32(0xff00ff00)`);
  until STATE_FORMAT 6 it was on the odd one, so the game read zeros
  (which pass its check) and its writes were lost.
- Operator settings (`adjust.c`): the game's numbered values packed as
  nibbles in one EEPROM record (two copies; RAM shadow 0xFFFF81DC),
  stored with the driver's code (data at non-power-of-two indices, parity
  bytes at powers of two and 0). Coin options are value 0x28 (0x26 on
  v2.0), game options 0x29 (0x27); fields from the test menu's own tables.
  The attract volume (0x2a, 0x28 on v2.0) is the nibble after the game
  options: 8 mute (the default), 9 a third, 10 two thirds, 11 full, found
  by saving it on the test menu's Adjust Volume page.
  Verified: the game keeps edited records, its GAME OPTIONS page shows
  them, free play starts a game, attract sound plays at the chosen level,
  on all three versions.
- Frontend (from UltraCombo, feature for feature): menu, ragerecomp.ini,
  OpenGL with filters and libretro presets, gamepads with assignment,
  labels and rumble, save states, rewind, run-ahead, boot skip, auto-coin,
  training mode with dummy record/play, input display, rollback netplay,
  screenshots, pacing, welcome screen, launcher paths, operator settings
  (UltraCombo's DIP/difficulty rows: difficulty, round time, free play,
  coinage, gore, attract sound). Not applicable to this hardware:
  finishing-move switch, disk speed, cabinet volume buttons, service
  credit.
- Game RAM (traced with RR_DUMP_AT; `game.c`): credits, round clock,
  fighter records (damage taken at +0x5A, 8.8, KO at 0x7800), players
  bitmask (Jan only).
- Verified: rewind-and-replay and 2-frame run-ahead match the plain run;
  netplay between two processes (40 +-15 ms, 5% loss) and two windows
  stays in sync; reset after play equals a fresh power-on apart from what
  the game itself wrote to its EEPROM; training
  refills health and holds the clock; auto-coin starts a game; save/load
  round-trips; controller-label test passes (93 checks).
- Game versions: all three played to a fight (primrageo/primrage20 start
  with button 1), each with zero interpreted instructions and translated
  = interpreted hashes over the scripted 3600 frames (`--script` as in
  regress.sh; primrageo `c3bebbd4cfffbd2a`, primrage20 `c78de1915c906f64`
  at STATE_FORMAT 8; `tools/regress.sh` runs all three). Switching versions
  in the menu restarts the program on the new set. ROM files are found by
  name with the right CRC, else by CRC (merged sets), else by name.
- Save states: a file must be exactly one state with this build's
  format and ROM (else "another version"), and the values that index
  tables (CAGE FIFO positions, scanline, per-line colour/tile banks, the
  partial motion-object line, the DSP's delayed-branch count) in range
  (else "damaged"), checked before anything is restored (`t_state`), as
  UltraCombo's v1.0.4 (#3). Rewind, run-ahead and rollback restore through
  the same checks, so a limit that's too tight breaks them: v1.1.1 allowed
  the partial line only below 240, but it is the last MO-control scanline
  (up to 261), so 98% of restores were refused. Check `--runahead 2` and
  `--rewind-test` against the plain hash after touching them.
- The window title, the welcome screen, the menu title, the log's first
  line, `--version` and the Windows file properties show the program's
  version (as UltraCombo's v1.2.7, #14).
- UltraCombo parity checked against its releases up to v1.2.8; what it
  has that this hardware lacks (game picker, disk speed and disk-wait,
  blood colour, finishing moves, breaker reward, two-bar health) is left
  out on purpose. `.recomp.json` describes the project for recomp.fyi.
- Verified: 338 unit tests pass on Linux and under Wine; Linux + Windows
  builds clean; `make packages` built and the AppImage (Zig, 83 s first
  launch) and Windows zip (under Wine, TinyCC) both start and play.
- CI (Linux + Windows under Wine) and the release workflow run on GitHub
  Actions; v1.0.0 was built there, and its downloaded AppImage (Zig) and
  Windows zip (Wine, TinyCC) both gave the regression hash.

## Style

Match the surrounding code: C11, 4-space indent, short functions with short
comments that say why, British spelling in prose ("licence", "colour"). The
`-Wextra` warnings already in the build are known. Don't add new ones.
