# RageRecomp

<a href="https://recomplauncher.com"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

A native Windows and Linux port of the arcade game **Primal Rage** (Atari
Games, 1994, Atari GT hardware). It runs the game's own Motorola 68EC020 and
TMS320C31 (CAGE sound) code, translated to C and compiled on your machine at
first launch, with the arcade hardware around it emulated. You supply the
game files from your own MAME ROM set; this program contains no game code or
game data.

Modelled on [UltraCombo](https://github.com/thefixinhixon/UltraCombo)'s
engineering, and sharing its frontend: menu, settings file, OpenGL with
libretro shaders, gamepads, save states, and the same "never ship anything
made from the game" discipline.

## What you need

- A PC running 64-bit Windows 10+ or Linux (glibc 2.29+), with OpenGL 2.1+.
- Your own MAME files, as zips, the way MAME keeps them:
  - `primrage.zip`: Primal Rage version 2.3 (Jan 1995), MAME's parent set;
  - `tms32031.zip`: the TMS320C31 boot ROM (`c31boot.bin`); the sound DSP
    needs it, and without it the game runs silent;
  - optionally `primrageo.zip` (2.3, Dec 1994) and `primrage20.zip` (2.0),
    which take their shared graphics and sound from `primrage.zip`. A
    merged `primrage.zip` holding all three works too: as in MAME, files
    are matched by CRC, whatever name a set stores them under.

## Quick start

1. Download a release package (or build it, below) and unpack it.
2. Put `primrage.zip` and `tms32031.zip` in its `run` folder.
3. Start `RageRecomp.exe` (Windows) or `./ragerecomp` (Linux).

With the AppImage, make a `run` folder beside the `.AppImage` file. If the
files aren't found the program opens a welcome screen that says where to put
them (O opens the folder, Enter checks again).

### First launch: "Preparing the game for this PC"

The first time (and for each game version), the program boots the sound
program to read its code, translates the 68020 program and the DSP program
to C, and compiles that into a module for your machine, kept in `cache/`.
It uses, in order: the Zig compiler bundled in `tools/zig`, a C compiler on
your system (gcc or clang), or TinyCC (`tools/tcc`, loaded as a library,
compiles in a few seconds). Expect a minute or so with Zig, a few seconds
with TinyCC. If none works the game still runs, on the built-in
interpreters, and a message says why (`cache/src-*/build.log`).

Translated and interpreted code behave identically, to the bit: the
translated code only removes the decoding and runs each instruction through
the same semantics.

## Controls

| Cabinet | Keyboard | Gamepad |
|---|---|---|
| P1 joystick | Arrows | Left stick / D-pad |
| P1 buttons 1-4 | Z X C V | X Y (top row), A B (bottom row) |
| P1 start | 1 | Start |
| P1 coin | 5 | Back |
| P2 joystick | Keypad 8 5 4 6 | Second pad |
| P2 buttons 1-4 | N M , . | Second pad |
| P2 start | 2 | Second pad Start |
| P2 coin | 6 | Second pad Back |

The cabinet's four attack buttons are numbered as in MAME. On the older
versions (`primrageo`, `primrage20`) the cabinet had no start buttons: Start
presses button 1, which starts a game there.

Everything can be rebound in the menu (Controls), and pads can be assigned
to players there.

| Hotkey | Default |
|---|---|
| Menu | Esc or F1 (gamepad: Guide, or Back+Start) |
| Operator menu (the cabinet's test switch, hold) | F2 |
| Pause / frame advance | P / `/` |
| Fast-forward (hold) | Tab |
| Save state / load state / next slot | F5 / F9 / F6 |
| Fullscreen | F11 or Alt+Enter |
| Screenshot | F12 |
| Volume up / down / mute | `=` / `-` / M |
| Performance overlay | F10 |
| Quit | the menu, or hold Back+Start on a pad for 2 seconds |

Started by a launcher with a game path, Esc quits (as in MAME and RetroArch)
and F1 opens the menu.

## The game's own settings

Coinage, free play, difficulty, rounds and the like are the cabinet's
operator settings: hold F2 for the game's test mode and change them there,
as an operator would. They are kept in the cabinet's EEPROM, saved per
version as `saves/<set>.eeprom`, along with the high scores. "Reset game"
power-cycles the cabinet and keeps them, as the real board does.

## Menu and settings

The menu has save states, Graphics (fullscreen, display mode, window size,
scaling, shader and its parameters, frame pacing, performance overlay),
Controls (keys, gamepad buttons, hotkeys, which pad each player uses, stick
deadzone, button labels), and Game & system (volume, background behaviour,
game version).

Settings live in `ragerecomp.ini`, written with comments on first run. Any
of them can be given once on the command line: `--set section.key=value`,
e.g. `--set video.filter=crt`, and `--fullscreen` / `--windowed`.

Frame pacing: on a ~60 Hz display the game runs one frame per refresh with
vsync (smooth; the arcade's 59.92 Hz becomes 60, 0.1% fast). On other
displays, or if vsync doesn't work, the sound card times it at the arcade's
own rate.

## Shaders

The Shader option cycles through the built-in filters (sharp pixels, smooth,
scanlines, CRT) and any libretro `.glslp` preset under `shaders/`. Bundled:
crt-lottes, crt-hyllian, crt-1tap/2tap, xBR, ScaleFX, and Lola Retro's
arcade-15khz (all permissively licensed; see `shaders/README.txt`).
`make shaders-extra` fetches GPL presets (crt-royale, crt-guest, xBRZ, ...)
from libretro/glsl-shaders into `shaders/extra`. Parameters are adjustable in
the menu and saved.

## Files and folders

Everything the program writes goes in its data folder: `--data-dir` if
given, else the program's folder if it's writable, else beside the AppImage
if a `run` folder is there, else `%APPDATA%\RageRecomp` (Windows) or
`~/.local/share/RageRecomp` (Linux).

- `ragerecomp.ini`: settings.
- `ragerecomp.log`: the last run's log (Windows; on Linux it goes to the
  terminal). `RR_CONSOLE=1` keeps it on the console on Windows.
- `saves/<set>.eeprom`: the cabinet's operator settings and high scores.
- `saves/<set>-slotN.rrs`: save states.
- `screenshots/`: PNGs.
- `cache/`: the translated game code. It is made from your game files:
  never share it.

The game files are looked for in a launcher's folder, the `[game] roms`
setting (or `--roms DIR`), beside the program and its `run/`, `run/` beside
an AppImage, then `run/` in the data folder. At startup every file is
checked against MAME's CRCs, and the program says exactly which are missing
or different.

## Command line

```
ragerecomp [options] [PATH]
  PATH                 a set's zip or a folder of zips (as launchers pass it)
  --roms DIR           folder holding the zips
  --game-version SET   primrage, primrageo or primrage20
  --data-dir DIR       keep everything the program writes in DIR
  --fullscreen, --windowed, --set SECTION.KEY=VALUE
  --version, --help
```

Headless runs (no window or audio device), used by the regression test:

```
ragerecomp-headless --roms run --frames 3600 \
    --script "coin1 600, coin1 620, start1 700, start1 1000" --hash
tools/regress.sh build-lnx/ragerecomp-headless primrage   # boot, coin-up, fight; prints the hash
```

`--shot`, `--wav`, `--save-at`, `--load` and `--nvram` are in `--help`.
Translated and interpreted runs (`RR_NOREC=1 RR_NODSP=1`) give the same
hash. CLAUDE.md lists the debugging variables.

## Building

Linux needs gcc, SDL2 development files and python3:

```
make            # build-lnx/ragerecomp and build-lnx/ragerecomp-headless
make tests      # unit tests
make windows    # MinGW cross build (the windowed program needs deps/sdl2-mingw)
```

`make recomp ROMS=run` is a developer build with the game code linked in,
from your own ROMs: never share it.

## Release packages

`tools/fetch-deps.sh` fills `deps/` from pinned, checksummed downloads
(Zig, SDL2 built against glibc 2.31 and for MinGW, TinyCC, AppImage's
runtime). Then `make packages` builds `dist/RageRecomp-Windows.zip`,
`dist/RageRecomp-Linux.tar.xz` and `dist/RageRecomp-x86_64.AppImage` (with
its `.zsync`). They contain no game files and no game code. Pushing a version
tag builds them on GitHub Actions into a draft release.

## Status and known issues

- No rewind, run-ahead or netplay.
- What is emulated, translated and verified is tracked in CLAUDE.md
  ("Status"). If the game desyncs, mis-renders or goes silent, the log and
  the regression hash are the first things to compare.

Trademarks: "Primal Rage" and "Atari" belong to their owners and are used
here only to say what this program plays. This project is not affiliated
with or endorsed by them.
