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
- Your own MAME files, as zips (or MAME's `.7z` sets), the way MAME keeps
  them:
  - `primrage.zip`: Primal Rage version 2.3 (Jan 1995), MAME's parent set;
  - `tms32031.zip`: the TMS320C31 boot ROM (`c31boot.bin`), a MAME device
    set of its own, not inside `primrage.zip` even when merged (newer MAME
    versions name it `tms320c31.zip`; either name works); the sound DSP
    needs it, and without it the game runs silent;
  - optionally `primrageo.zip` (2.3, Dec 1994) and `primrage20.zip` (2.0),
    which take their shared graphics and sound from `primrage.zip`. A
    merged `primrage.zip` holding all three works too: as in MAME, files
    are matched by CRC, whatever name a set stores them under.

## Quick start

1. Download a release package (or build it, below) and unpack it.
2. Put `primrage.zip` and `tms32031.zip` (or `tms320c31.zip`) in its `run`
   folder.
3. Start `RageRecomp.exe` (Windows) or `./ragerecomp` (Linux).

`.7z` sets work the same way (`primrage.7z`, `tms32031.7z`, ...), and
zip and 7z can be mixed. With the AppImage, make a `run` folder beside the
`.AppImage` file. If the
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
| Fast-forward (hold) / rewind (hold) | Tab / Backspace |
| Save state / load state / next slot | F5 / F9 / F6 |
| Fullscreen | F11 or Alt+Enter |
| Screenshot | F12 |
| Volume up / down / mute | `=` / `-` / M |
| Input display / training mode | F3 / F4 |
| Record dummy / play dummy (training) | F7 / F8 |
| Performance overlay | F10 |
| Quit | the menu, or hold Back+Start on a pad for 2 seconds |

Started by a launcher with a game path, Esc quits (as in MAME and RetroArch)
and F1 opens the menu.

## The game's own settings

Primal Rage has no DIP switches: its settings are the cabinet's operator
settings, kept in its EEPROM (saved per version as `saves/<set>.eeprom`,
with the statistics and high scores). Menu > Game & system changes the
main ones, the same way the game's test menu stores them:

| Setting | Choices (game's default) |
|---|---|
| Difficulty | 1 (easiest) to 16 (7) |
| Round time | 30 to 80 seconds (55) |
| Free play | Off / On (Off) |
| Coinage | 1 to 8 coins per credit (2) |
| Blood | Full gore / No gore (Full gore) |
| Attract sound | Mute, 1/3, 2/3 or full volume during the attract (Mute) |

The game reads them when the cabinet powers on, so a change (marked `*`)
applies on "Reset game", or the next start. On the very first start the
game writes its defaults during its boot, so they can be changed once it
has booted. Everything else (bonus coins, coin mechanisms, continue
discount, language, game volume, statistics, tests) is in the game's own test
menu: hold F2. "Reset game" power-cycles the cabinet and keeps the EEPROM,
as the real board does.

## Menu and settings

The menu has save states, Graphics (fullscreen, display mode, window size,
scaling, shader and its parameters, frame pacing, performance overlay),
Controls (keys, gamepad buttons, hotkeys, which pad each player uses,
rumble, stick deadzone, button labels), Game & system (volume, background
behaviour, game version, auto-coin, run-ahead, rewind), Practice and
Netplay.

- **Rewind**: hold Backspace (or a pad button set in the menu); about 26 KB
  per frame of history, 256 MB by default.
- **Run-ahead** (0-2 frames): shows the result of your input sooner by
  emulating ahead and rolling back each frame.
- **Auto-coin**: Start with no credits drops in coins (the default coinage
  takes two per credit), then starts. Off by itself in free play.
- **Practice**: training mode refills health after each combo and holds the
  round clock; player 2 can be kept low to practise finishing; a player 2
  dummy can be recorded with your own controls and played back; an input
  display shows both players' recent inputs in numpad notation.
- **Netplay**: two players over UDP with rollback (as UltraCombo): one hosts
  (Menu > Netplay, or `--host [PORT]`), the other joins (`--join
  HOST[:PORT]`); input delay 1-4 frames. Both must run the same game
  version; the host's operator settings apply to both. Forward the UDP
  port, or use a VPN such as Tailscale.
- **Rumble** when your fighter is hit.
- `[system] boot_skip_frames` fast-forwards the boot tests at startup.

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

`--shot`, `--wav`, `--save-at`, `--load`, `--nvram`, `--rewind-test`,
`--runahead`, `--shot-every`, `--trace`, `--dis` and the headless netplay
tests (`--net-host`, `--net-join`) are in `--help`.
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

- Builds before this one dropped stage scenery drawn as large sprites
  (ice cliffs, Stonehenge's stones, ...) whenever it reached past the left
  edge of the screen, so it vanished and reappeared as the fight moved.
- Builds before this one played no music (effects and voices only): a
  sound-processor instruction changed flags it shouldn't, which stalled
  the music sequencer. The sound board also ran 3% fast. Both are fixed;
  the first start after updating rebuilds the translated code.
- Builds before this one wired the EEPROM to the wrong byte lane: the game
  read blank settings and nothing it wrote was kept (operator settings,
  statistics, high scores). Their `saves/*.eeprom` files are blank, so
  nothing is lost; the game sets itself up on the next start.
- Training mode holds the round clock by setting it back each second, so
  its last digit blinks once a second.
- Which fighter is the computer's is known on the Jan 1995 version only;
  on the older ones rumble treats both as players.
- What is emulated, translated and verified is tracked in CLAUDE.md
  ("Status"). If the game desyncs, mis-renders or goes silent, the log and
  the regression hash are the first things to compare.

Trademarks: "Primal Rage" and "Atari" belong to their owners and are used
here only to say what this program plays. This project is not affiliated
with or endorsed by them.
