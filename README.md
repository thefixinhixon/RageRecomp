# RageRecomp

A native Windows and Linux port of the arcade game **Primal Rage** (Atari GT
hardware, 1994). It runs the game's own Motorola 68EC020 and TMS320C31 code,
statically translated to C on your machine at first launch, with the arcade
hardware around it emulated. You supply the game files from your own MAME
ROM set; this repository contains no game code or game data.

Modelled on [UltraCombo](https://github.com/thefixinhixon/UltraCombo)'s
engineering: platform-agnostic game/CPU state, an interpreter plus a
runtime-built translated module, libretro-style shaders, save states, and
the same "never commit game-derived files" discipline.

## What you need

- A PC running 64-bit Windows 10+ or Linux (glibc 2.31+), with OpenGL 2.1+.
- Your own `primrage.zip` MAME ROM set (version 2.3, Jan 1995). The older
  `primrageo` (2.3, Dec 1994) and `primrage20` (2.0) sets work too where the
  files are present, including split sets that borrow shared ROMs from
  `primrage.zip`.
- No game files are shipped here and none are needed to build the program.

## Quick start

1. Build the program (needs gcc, SDL2, python3):
   ```
   make
   ```
2. Put `primrage.zip` in `run/` next to the program (or anywhere and pass
   `--romdir`, or pass the zip directly with `--rom`).
3. Run it:
   ```
   ./ragerecomp
   ```

At first launch it checks your ROMs, translates the game code to C, compiles
it with a compiler on your machine (cc/gcc/clang, else TinyCC, else the
interpreter), caches the result in `cache/`, and starts the game. Later
launches reuse the cache.

## Controls

| Arcade | Keyboard | Gamepad |
|---|---|---|
| P1 stick | Arrows | Left stick / D-pad |
| P1 buttons 1-4 | Z X C V | A B X Y (face buttons) |
| P1 start (v2.3 Jan 95) | 1 | Start |
| P2 stick | numpad 8456 | Second pad |
| P2 buttons 1-4 | N M , . | Second pad face buttons |
| P2 start | 2 | Second pad Start |
| Coin | 5 / 6 | Back / Guide |
| Test menu | F2 | — |
| Menu | Esc / F1 | — |

Buttons can be rebound in the menu (Input). On `primrageo`/`primrage20` one
of the four action buttons doubles as start, as on the cabinet.

## Menu and settings

Press Esc for the menu: video (shader, scale, fullscreen), audio volume,
input bindings, dip-switch free play, save states (F5 save, F9 load),
and reset. Settings live in `ragerecomp.ini` beside the program; every
setting can also be given once on the command line as
`--set section.key=value` (see `--help`).

## Files and folders

The program writes only into its data folder: `--data-dir` if given, else
the folder beside the program if that is writable, else `RageRecomp` in
your user data folder. Writable files:

- `ragerecomp.ini` — settings.
- `ragerecomp.log` — last run's log (console with `RR_CONSOLE=1` on Windows).
- `saves/*.eeprom` — cabinet settings (one per game version).
- `saves/slotN.rrs` — save states.
- `cache/` — unpacked ROMs and translated-code builds (game-derived; the
  program rebuilds them, never ship them).

## ROM check and missing files

At startup the program lists every ROM it needs, with CRC32s, and tells you
exactly which files are missing or have bad dumps before it starts. Shared
graphics/sound ROMs are found in `primrage.zip` even when you run an older
set, like MAME's split/merged sets. Offsets and CRCs follow MAME's
`atarigt.cpp` driver.

## Shaders

`shaders/` holds display presets: `none` (sharp pixels), a bundled CRT
(`crt/crt-rage`), and slots for libretro `.glslp` presets you add yourself
(`make shaders-extra` fetches a GPL set from libretro/glsl-shaders for
local use only; they are never shipped).

## Headless and regression use

```
./ragerecomp --headless 3600 --script "coin 60, start1 120, ..."  # fixed run
tools/regress.sh ./ragerecomp primrage   # boot + attract + fight, prints hash
```

Useful environment variables (`RR_NOREC=1` interpreter only,
`RR_NODSP=1` DSP interpreter, `RR_WAV=out.wav` record audio,
`RR_DUMP_AT=frame` snapshot RAM, `RR_DISPLAY_HZ=N` pacing tests).
See CLAUDE.md for the full list.

## Release packages

`make packages` builds `dist/RageRecomp-Windows.zip`,
`dist/RageRecomp-Linux.tar.xz` and the AppImage. They contain no game
files and no game code. Pushing a version tag builds them on GitHub Actions.

## Known issues

- Audio is currently silent: the DSP sound program crashes a few seconds
  after boot (frame ~283). The mechanism is mapped (voice-mix buffer
  overrun into the task stack, then a wild jump to the boot stub) but
  the scheduling trigger is still under investigation. Video, input,
  coins, character select and fights all work.
- Attract mode crashes with a 68K address error after ~40 seconds.

## Status and honesty

This is a clean-room static recompilation. What is emulated vs translated,
and what remains unverified, is tracked in CLAUDE.md ("Status"). If the
game desyncs, mis-renders or goes silent, the log (`--verbose`) and the
regression hash are the first things to compare.

Trademarks: "Primal Rage" and "Atari" belong to their owners and are used
here only to say what this program plays.
