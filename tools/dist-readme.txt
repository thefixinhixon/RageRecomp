RageRecomp - an unofficial native port of the arcade game Primal Rage
=====================================================================

This package does NOT contain the game. You need your own copies of the
arcade game's files (MAME's primrage set). Nothing from them (no ROM data,
no game code) is included here.

1. Put your game files in the "run" folder (see run/PUT-GAME-FILES-HERE.txt):
   MAME's zips as they are, no unpacking needed.
2. Start the program (RageRecomp.exe on Windows, ./ragerecomp on Linux).

AppImage (Linux): make a folder named "run" beside the .AppImage file
and put the game files in it; settings and the cache then go beside the
AppImage too. Without that folder, use ~/.local/share/RageRecomp/run/.

First launch: "PREPARING THE GAME FOR THIS PC"
----------------------------------------------
The first time you play (and for each game version), the program translates
the game's own code (the 68020 main program and the sound DSP's program)
into C and compiles it into a native module for your machine. That takes
under a minute and only happens once; the result is kept in the "cache"
folder.

It uses, in order: the Zig compiler bundled in tools/zig, a C compiler
already on your system (gcc or clang), or TinyCC (tools/tcc). If none of
them work the game runs on the slower built-in interpreters, and a warning
tells you why (see build.log in the cache/src-* folder).

Settings (ragerecomp.ini), the cabinet's own saves (operator settings and
high scores, in "saves"), save states, screenshots and the cache go next to
the program if that folder is writable, otherwise into your user data folder:
  Windows: %APPDATA%\RageRecomp\
  Linux:   ~/.local/share/RageRecomp/

Linux: needs glibc 2.31 or newer (distributions from about 2020 on). SDL2
is included, in lib/.

Press F1 (or Esc) in the game for the menu; hold F2 for the cabinet's own
operator menu (coinage, free play, difficulty). README.md is the manual.

Primal Rage is a trademark of its owners. This project is not affiliated
with or endorsed by them.

Licences are in the "licenses" folder: RageRecomp itself (MIT), SDL2
(zlib), miniz (MIT), the LZMA SDK (public domain), Zig (MIT), TinyCC (LGPL 2.1, used as a separate
library; source: see tools/tcc/SOURCE.txt), shaders (licence header in each
file; those in shaders/extra are GPL: xBRZ GPL 3.0, the others GPL 2.0 or
later).
