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

- A PC running 64-bit Windows 10+ or Linux (glibc 2.31+), with OpenGL 2.1+.
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

### Checking your files

These are the files of MAME's sets (the dumps MAME lists as good). At
startup the program checks every file it needs against its CRC32 and names
any that is missing or different. Files are matched by CRC first, so the
names inside a zip don't matter (merged, split and non-merged sets all
work). To check them yourself, use a ROM manager, or `sha1sum` (Linux) /
`certutil -hashfile FILE SHA1` (Windows) on the unpacked files.

RecompLauncher recognises MAME's merged `primrage.zip` as TorrentZip'd by
current ROM managers (56 files, all three versions): 29,743,358 bytes,
CRC32 `83ff8613`, SHA-1 `a1da01e5c93f3a1183b5d0ede990bdc444b1dd25`. Other
packings of the same files work just as well; they only miss the
"verified" badge there.

| Set | File | Size | CRC32 | SHA-1 |
|---|---|---|---|---|
| `tms32031` (newer MAME: `tms320c31`) | `c31boot.bin` | 16,384 | `bddc2763` | `96b2170ecee5bec5abaa1741bb2d3b6096ecc262` |

<details><summary>`primrage` (2.3, Jan 1995): 37 files</summary>

| File | What | Size | CRC32 | SHA-1 |
|---|---|---|---|---|
| `136102-0332.mol1.0` | sprites | 1,048,576 | `610cfcb4` | `bed1bd0d11c0a7cc48d020fc0acec34daf48c5ac` |
| `136102-0333.moh1.0` | sprites | 1,048,576 | `3320448e` | `aef42328bf72fca5c04bfed1ea41100bb5aafeaa` |
| `136102-0334.mol1.1` | sprites | 1,048,576 | `be3acb6f` | `664cb4cd4d325577ab0cbe0cf48870a9f4706573` |
| `136102-0335.moh1.1` | sprites | 1,048,576 | `e4f6e87a` | `2a3f8ff46b289c25cd4ca2a1369b14613f48e964` |
| `136102-0336.mol1.2` | sprites | 1,048,576 | `a78a8525` | `69c3da4d45b0f09f5bdabcedd238b82efab48a70` |
| `136102-0337.moh1.2` | sprites | 1,048,576 | `73fdd050` | `63c67187953d2dab93a260e548ef5965e7cba4e8` |
| `136102-0338.mol1.3` | sprites | 1,048,576 | `fa19cae6` | `7d0560516971f32835329a17450c7561631a27d1` |
| `136102-0339.moh1.3` | sprites | 1,048,576 | `e0cd1393` | `0de59d04165d64320512936c194db19cca6455fd` |
| `136102-0316.mol0.0` | sprites | 1,048,576 | `9301c672` | `a8971049c857ae283a95b257dd0d6aaff6d787cd` |
| `136102-0317.moh0.0` | sprites | 1,048,576 | `9e3b831a` | `b799e57bea9522cb83f9aa7ea38a17b1d8273b8d` |
| `136102-0318.mol0.1` | sprites | 1,048,576 | `8523db5d` | `f2476aa26b1a7cbe7510994d92eb209fda65593d` |
| `136102-0319.moh0.1` | sprites | 1,048,576 | `42f22e4b` | `2a1a6f0a7aca7b7b64bce0bd54eb4cb23a2336b1` |
| `136102-0320.mol0.2` | sprites | 1,048,576 | `21369d13` | `28e03595c098fd9bec6f7316180d17905a51a51b` |
| `136102-0321.moh0.2` | sprites | 1,048,576 | `3b7d498a` | `804e9e1567bf97e8dae3b9444428254ced8b60da` |
| `136102-0322.mol0.3` | sprites | 1,048,576 | `05e9f407` | `fa25a893d4cb805df02d7d12df4dbabefb3114a2` |
| `136102-0323.moh0.3` | sprites | 1,048,576 | `603f3bb6` | `d7c22dc900d9edc36d8f211df67a206d14637fab` |
| `136102-0324.mol0.4` | sprites | 1,048,576 | `3c37769f` | `ca0306a439949d2a0305cc0cf05808a58e84084c` |
| `136102-0325.moh0.4` | sprites | 1,048,576 | `f43321e3` | `8bb4dd4a5d5400b17052d50dca9078211dc6b861` |
| `136102-0326.mol0.5` | sprites | 1,048,576 | `63d4ccea` | `340fced6998a8ae6fd285d8fe666f5f1e4b6bfaf` |
| `136102-0327.moh0.5` | sprites | 1,048,576 | `9f4806d5` | `76e9f1a47e7fa45e834fa8739528f1e3c54b14dc` |
| `136102-0328.mol0.6` | sprites | 1,048,576 | `a08d73e1` | `25a58777f15e9550111447b47a98762fd6bb498d` |
| `136102-0329.moh0.6` | sprites | 1,048,576 | `eff3d2cd` | `8532568b5fd91d2b738947e9cd575a4eb2a03be2` |
| `136102-0330.mol0.7` | sprites | 1,048,576 | `7bf6bb8f` | `f34bd8a9c7f95436a1c816badc59673cd2a6969a` |
| `136102-0331.moh0.7` | sprites | 1,048,576 | `c6a64dad` | `ee54514463ab61cbaef70da064cf5de591e5861f` |
| `rage_136102-2044a_pgmuu.29l` | program | 524,288 | `85556b91` | `5f4f5d0bf68bd17b7bff230b521a5dcfff414a50` |
| `rage_136102-2043a_pgmum.28l` | program | 524,288 | `4d3414d0` | `b6465c0fbee4e67f74185e9ea048e40f4f443efa` |
| `rage_136102-2042a_pgmlm.26l` | program | 524,288 | `6b91e8ea` | `c44642a0f59284e35ee20fbedc3934e96f15b37d` |
| `rage_136102-2041a_pgmll.24l` | program | 524,288 | `9958f715` | `c5b8fb1ef6346563edca94db3f53737489069ebd` |
| `rage_136102-1045b_dspsub.11a` | sound program | 524,288 | `0656435f` | `f8e498171e754eb8703dad6b2351509bbb27e06b` |
| `136102-0075_dspsub.11c` | sound data | 2,097,152 | `02448be6` | `cc82ececfa739600136b330df90edcc07dff9296` |
| `136102-0077_dspsub.11e` | sound data | 2,097,152 | `057aff9a` | `5d654baafb8395c059613069592ec392d1096870` |
| `rage_136102-0050a_pf0l.25n` | playfield tiles | 524,288 | `66896e8f` | `7675b24c15ca0608f11f2a7b8d70717adb10924c` |
| `rage_136102-0051a_pf0m.27n` | playfield tiles | 524,288 | `fb5b3e7b` | `f43fe4b5c4bbea10da46b60c644f586fb391355d` |
| `rage_136102-0052a_pf0h.28n` | playfield tiles | 524,288 | `cbe38670` | `0780e599007851f6d37cdd8c701d01cb1ae48b9d` |
| `rage_136102-1078a_alpha.22p` | text tiles | 131,072 | `1d3260bf` | `85d9db8499cbe180c8d52710f3cfe64453a530ff` |
| `136102-2100a_mol0.2v` | sprites | 524,288 | `433d4925` | `57e8ea9a8cb4319668f1f76e9042d9d30d130833` |
| `136102-2101a_moh0.2w` | sprites | 524,288 | `78c7bb2b` | `a09e95b2560d0810eb4cb2666ffec428e158c669` |

</details>

<details><summary>`primrageo` (2.3, Dec 1994): 13 files</summary>

| File | What | Size | CRC32 | SHA-1 |
|---|---|---|---|---|
| `136102-1044b.29l` | program | 524,288 | `35c9c34b` | `4bd1d35cc7c68574819afd648405eedb8db25b4c` |
| `136102-1043b.28l` | program | 524,288 | `86322829` | `e0e72888def0931d078921f099bae6788738a291` |
| `136102-1042b.27l` | program | 524,288 | `750e8095` | `4660637136b1a25169d8c43646c8b87081763987` |
| `136102-1041b.25l` | program | 524,288 | `6a90d283` | `7c18c97cb5e5cdd26a52cd6bc099fbce87055311` |
| `136102-1078a.11a` | sound program | 524,288 | `0656435f` | `f8e498171e754eb8703dad6b2351509bbb27e06b` |
| `136102-0075` | sound data | 2,097,152 | `b685a88e` | `998b8fe54971f6cd96e4c22b19e3831f29d8172d` |
| `136102-0077` | sound data | 2,097,152 | `3283cea8` | `fb7333ca951053a56c501f2ce0eb197c8fcafaf7` |
| `136102-0050a.25n` | playfield tiles | 524,288 | `66896e8f` | `7675b24c15ca0608f11f2a7b8d70717adb10924c` |
| `136102-0051a.27n` | playfield tiles | 524,288 | `fb5b3e7b` | `f43fe4b5c4bbea10da46b60c644f586fb391355d` |
| `136102-0052a.28n` | playfield tiles | 524,288 | `cbe38670` | `0780e599007851f6d37cdd8c701d01cb1ae48b9d` |
| `136102-1045b.23p` | text tiles | 131,072 | `1d3260bf` | `85d9db8499cbe180c8d52710f3cfe64453a530ff` |
| `136102-1100a.2v` | sprites | 524,288 | `6e9c80b5` | `ec724011527dd8707c733211b1a6c51b22f580c7` |
| `136102-1101a.2w` | sprites | 524,288 | `bb7ee624` | `0de6385aee7d25b41fd5bf232e44e5da536504ac` |

</details>

<details><summary>`primrage20` (2.0): 13 files</summary>

| File | What | Size | CRC32 | SHA-1 |
|---|---|---|---|---|
| `136102-0044b.29l` | program | 524,288 | `26139575` | `22e59ab621d58e56969b64701fc59aec085193dd` |
| `136102-0043b.28l` | program | 524,288 | `928d2447` | `9bbbdbf056a7b986d985d79be889b9876a710631` |
| `136102-0042b.27l` | program | 524,288 | `cd6062b9` | `2973fb561ab68cd48ec132b6720c04d10bedfd19` |
| `136102-0041b.25l` | program | 524,288 | `3008f6f0` | `45aac457b4584ee3bd3561e3b2e34e49aa61fbc5` |
| `136102-0078a.11a` | sound program | 524,288 | `91df8d8f` | `6d361f88de604b8f11dd9bfe85ff18bcd322862d` |
| `136102-0075` | sound data | 2,097,152 | `b685a88e` | `998b8fe54971f6cd96e4c22b19e3831f29d8172d` |
| `136102-0077` | sound data | 2,097,152 | `3283cea8` | `fb7333ca951053a56c501f2ce0eb197c8fcafaf7` |
| `136102-0050a.25n` | playfield tiles | 524,288 | `66896e8f` | `7675b24c15ca0608f11f2a7b8d70717adb10924c` |
| `136102-0051a.27n` | playfield tiles | 524,288 | `fb5b3e7b` | `f43fe4b5c4bbea10da46b60c644f586fb391355d` |
| `136102-0052a.28n` | playfield tiles | 524,288 | `cbe38670` | `0780e599007851f6d37cdd8c701d01cb1ae48b9d` |
| `136102-0045a.23p` | text tiles | 131,072 | `c8b39b1c` | `836c0ccf96b2beccacf6d8ac23981fc2d1f09803` |
| `136102-0100a.2v` | sprites | 524,288 | `5299fb2a` | `791378215ab6ffff3ab2ae7192ce9f88dae4090d` |
| `136102-0101a.2w` | sprites | 524,288 | `3e234711` | `6a9f19db2b4c8c34d3d7b4984206e3d5c4398d7f` |

</details>

The older versions take the sprites (`136102-03xx`) from `primrage.zip`,
as in MAME.

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

## Starting from a launcher

Launchers (LaunchBox and Big Box, Playnite, ES-DE, Pegasus, Attract-Mode,
Steam) start the program with a game's path, and it plays straight from
your MAME folder:

    RageRecomp.exe "D:\MAME\roms\primrage.zip"
    ./RageRecomp-v1.1.0-Linux-x64.AppImage ~/mame/roms/primrage.zip

The path can be a set's zip or 7z (`primrageo.zip` and `primrage20.zip`
start those versions, with `primrage.zip` beside them) or a folder holding
them. The DSP boot ROM (`tms32031.zip` or `tms320c31.zip`) is looked for
beside it. Started this way the game starts at once and **Esc quits** (F1
opens the menu).

| Launcher | Set up |
|---|---|
| LaunchBox / Big Box | Add an emulator: application path `RageRecomp.exe`, no parameters (the game's path is passed for you), for the Arcade platform |
| Playnite | Configure Emulators → Add: executable `RageRecomp.exe`, arguments `"{ImagePath}"` |
| ES-DE | A command for the arcade (or mame) system in a custom `es_systems.xml`: `"C:\Games\RageRecomp\RageRecomp.exe" %ROM%` |
| Pegasus | `launch: "C:/Games/RageRecomp/RageRecomp.exe" "{file.path}"` |
| Attract-Mode | Executable `RageRecomp.exe`, arguments `"[rompath][name][romext]"` |
| Steam (and Steam Deck) | Add a Non-Steam Game; launch options: the set's path, or nothing to use the `run` folder |
| RecompLauncher | Nothing to set up: see [RecompLauncher](#recomplauncher) below |

From a controller, hold Back+Start for two seconds to quit, however the game
was started. A `quit` key or pad button can be set in the menu (Controls →
Hotkeys / Gamepad hotkeys) for launchers that close games with their own key.

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

## RecompLauncher

[RecompLauncher](https://recomplauncher.com) installs, sets up and starts
RageRecomp (full setup, its level 3), from `recomplauncher.json` in each
release:

- You give it your `primrage.zip` (or `.7z`), or the folder holding it;
  the merged TorrentZip set gets the "verified" badge.
- Its setup step (`--prepare`) builds the game code for every version in
  your set and does each cabinet's first boot, without a window, so the
  first Play starts at once and the game's operator settings can be
  changed from the start.
- The game's page shows the main settings (window, picture and shaders,
  scaling, game version, volume, boot tests, auto-coin, run-ahead, rumble,
  button labels) and the arcade's own (difficulty, round time, free play,
  coinage, blood, attract sound: "Game's setting" leaves the test menu's).
- Everything stays in the game's folder (`--data-dir`): settings, saves
  (backed up by the launcher), screenshots, the log for crash reports.

## Menu and settings

The menu (Esc, F1, or a pad's Guide button or Back+Start) has save states,
Graphics (fullscreen, display mode, window size, scaling, shader and its
parameters, frame pacing, performance overlay), Controls (keys, gamepad
buttons, hotkeys, which pad each player uses, rumble, stick deadzone,
button labels), Game & system (volume, background behaviour, game version,
the game's own settings, auto-coin, run-ahead, rewind), Practice and
Netplay. Its title shows the program's version, as does the window's.

- **Auto-coin**: Start with no credits drops in coins (the default coinage
  takes two per credit), then starts. Off by itself in free play.
- **Rumble** when your own fighter is hit (not the computer's, and not in
  the attract's demo fights).
- **Which pad each player uses**: Controls → P1 controller / P2
  controller: select one and press a button on the pad you want. It's
  remembered by the pad's name, whatever order the pads connect in.
- **Screenshots**: F12, or a pad button set under Controls → Gamepad
  hotkeys → Screenshot. PNGs go to `screenshots/`.
- `[system] boot_skip_frames` fast-forwards the boot tests at startup.

Settings live in `ragerecomp.ini`, written with comments on first run. Any
of them can be given once on the command line: `--set section.key=value`,
e.g. `--set video.filter=crt`, and `--fullscreen` / `--windowed`.

## Training mode

Menu → Practice. Training mode refills health after each combo and holds
the round clock. With **Health refill** off the clock still stops but
damage adds up. **Player 2 health** can be kept low (20 of 120), to
practise finishing. A player 2 dummy can be recorded with your own controls
(F7) and played back (F8); start a 2-player game for it. The **input
display** (F3) shows both players' recent inputs in numpad notation.

## Netplay

Two players over UDP with rollback, as UltraCombo: one hosts (Menu →
Netplay, or `--host [PORT]`), the other joins (`--join HOST[:PORT]`), with
an input delay of 1-4 frames. Both need the same program and game version;
the host's operator settings (its EEPROM) apply to both. Forward the UDP
port (7845 by default), or use a VPN such as Tailscale. In netplay the
game never pauses: pause, loading states and training are off.

## Save states, rewind and run-ahead

- **Save states**: F5 saves, F9 loads, F6 picks the slot (1-9). They are
  in `saves/<set>-slotN.rrs`, and load only in a build that emulates the
  same way (the release notes say when that changes). A damaged file is
  refused with a message.
- **Rewind**: hold Backspace (or a pad button set in the menu); about
  26 KB per frame of history, 256 MB (about 2.5 minutes) by default.
- **Run-ahead** (0-2 frames): shows the result of your input sooner by
  emulating ahead and rolling back each frame. The arcade game reads input
  the frame it comes, so off is how it played. When a frame is too heavy to
  repeat, it pauses for a second instead of starving the sound.

## Frame pacing

On a ~60 Hz display the game runs one frame per refresh with vsync
(smooth; the arcade's 59.92 Hz becomes 60, 0.1% fast). On other displays
(120/144 Hz), or if vsync turns out not to wait (a driver setting can force
it off; the program notices within a few seconds and says so), the sound
card times it at the arcade's own rate. Graphics → Frame pacing picks one
by hand. The performance overlay (F10) shows the frame rate, the audio
buffer and LOAD: the share of each frame's time spent emulating (under
100% your PC keeps up).

## Shaders

Graphics → Shader cycles through the built-in filters (sharp pixels,
smooth, scanlines, CRT) and every libretro `.glslp` preset under
`shaders/`. The packages include:

- permissively licensed: crt-lottes, crt-hyllian, crt-nobody,
  crt-1tap/2tap, xBR, ScaleFX, and Lola Retro's arcade-15khz (see
  `shaders/README.txt`);
- GPL, in `shaders/extra/` as their source files (licences in
  `licenses/`): crt-royale, crt-guest-dr-venom, crt-guest-sm, crt-geom,
  crt-easymode, zfast-crt, crt-aperture and xBRZ.

Parameters are adjustable in the menu and saved. Any other libretro GLSL
preset works too: put it, with the files it uses, under `shaders/`. Built
from source, `make shaders-extra` fetches the GPL ones from
libretro/glsl-shaders (`make packages` does it for you), and `make`
links `shaders/` into the build folder so `build-lnx/ragerecomp` lists them.

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
  --operator SPEC      the game's operator settings, e.g. difficulty=8,free_play=1
  --prepare            set up without a window: build the game code, first boot
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
runtime). Then `make packages` builds `dist/RageRecomp-vVERSION-Windows-x64.zip`,
`-Linux-x64.tar.xz` and `-Linux-x64.AppImage` (with its `.zsync`), named
by version, system and processor as RecompLauncher expects. They contain no
game files and no game code, and always include the GPL extra shaders
(fetched if missing). The version comes from `git describe`, so packages
are only made from committed work: with uncommitted changes the version
would end in `-dirty`, and `make packages` stops (`ALLOW_DIRTY=1` for a
throwaway test build). Pushing a version tag builds them on GitHub Actions
into a draft release, published as a full release unless it's a test
build.

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

## Thanks

To UltraCombo, whose frontend, module builder, packaging and testing
discipline this project shares, and to everyone whose reports and testing
shaped both; to MAME's developers, whose Atari GT and CAGE drivers are the
reference for the hardware emulated here.

## Licence

RageRecomp is MIT-licensed (`LICENSE`). That covers this project's own
code only: the game, its code and its data belong to their owners, and you
need your own copies. Bundled third-party parts keep their licences:
miniz (MIT), which reads zips; the LZMA SDK's 7z reader (public domain);
the shaders (see `shaders/README.txt`; the extras are GPL); and in the
release packages SDL2 (zlib), Zig (MIT) and TinyCC (LGPL 2.1). NOTICE
credits MAME, whose drivers the hardware model follows.

Trademarks: "Primal Rage" and "Atari" belong to their owners and are used
here only to say what this program plays. This project is not affiliated
with or endorsed by them.
