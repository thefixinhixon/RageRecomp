#!/bin/sh
# Assemble a distributable package: the program, the compilers it uses to
# build the game code on the player's machine, shaders and licences.
# No game files and no game code go in.
#
#   tools/package.sh linux    PROGRAM libSDL2-2.0.so.0 -> dist/RageRecomp-Linux/
#   tools/package.sh windows  PROGRAM SDL2.dll         -> dist/RageRecomp-Windows/
#   tools/package.sh appimage RUNTIME                  -> $APPIMAGE
#     (from dist/RageRecomp-Linux; RUNTIME is AppImage's type2-runtime)
#     and its .zsync, which goes in the release beside it
# RR_VERSION (the release's version) goes in recomplauncher.json's
# prepare.version, so RecompLauncher sets the game up again for each
# release (the step builds the game code).
set -e
target=$1 prog=$2
sdl_licence=deps/sdl2-lnx/SDL2-2.32.10/LICENSE.txt
case $target in
linux)   out=dist/RageRecomp-Linux   zig=deps/zig-linux   tcc=deps/tcc-lnx231 exe=ragerecomp ;;
windows) out=dist/RageRecomp-Windows zig=deps/zig-windows tcc=deps/tcc-win    exe=RageRecomp.exe ;;
appimage)
    # an AppImage is the runtime followed by a squashfs of the program's folder
    app=dist/RageRecomp.AppDir image=${APPIMAGE:-dist/RageRecomp-x86_64.AppImage}
    rm -rf "$app" "$image"
    cp -r dist/RageRecomp-Linux "$app"
    rm -rf "$app/run"                   # game files go beside the AppImage
    ln -s ragerecomp "$app/AppRun"
    "$app/ragerecomp" --write-icon "$app/ragerecomp.png"
    ln -s ragerecomp.png "$app/.DirIcon"
    cp tools/ragerecomp.desktop "$app/"
    zsyncmake=$(command -v zsyncmake || echo "$PWD/deps/zsync/usr/bin/zsyncmake")
    [ -x "$zsyncmake" ] || { echo "zsyncmake not found (package zsync): needed for the AppImage's .zsync" >&2; exit 1; }
    mksquashfs "$app" dist/appimage.sqfs -root-owned -noappend -quiet -comp zstd -Xcompression-level 19 -b 1M
    # update information for AppImageUpdate, in the runtime's .upd_info
    # section: the newest GitHub release and the .zsync published beside it
    upd="gh-releases-zsync|thefixinhixon|RageRecomp|latest-all|RageRecomp-*-Linux-x64.AppImage.zsync"
    cp "$prog" dist/runtime.tmp
    set -- $(readelf -SW dist/runtime.tmp | sed -n 's/.*\] \.upd_info *[A-Z]* *[0-9a-f]* \([0-9a-f]*\) \([0-9a-f]*\).*/\1 \2/p')
    [ $# = 2 ] && [ ${#upd} -lt $((0x$2)) ] || { echo "no room for update information in $prog" >&2; exit 1; }
    printf '%s' "$upd" | dd of=dist/runtime.tmp bs=1 seek=$((0x$1)) conv=notrunc status=none
    cat dist/runtime.tmp dist/appimage.sqfs > "$image"
    chmod +x "$image"
    rm -f dist/appimage.sqfs dist/runtime.tmp
    name=$(basename "$image")
    (cd "$(dirname "$image")" && rm -f "$name.zsync" && "$zsyncmake" -u "$name" "$name")
    echo "packaged $image ($(du -h "$image" | cut -f1)) and its .zsync"
    exit 0 ;;
*) echo "usage: $0 linux|windows|appimage ..." >&2; exit 1 ;;
esac

rm -rf "$out"
mkdir -p "$out/run" "$out/tools/zig/lib" "$out/tools/tcc" "$out/licenses"
cp "$prog" "$out/$exe"
cp README.md NOTICE "$out/"
# RecompLauncher reads it from the top of the release zip
sed "s/\"version\": \"dev\"/\"version\": \"${RR_VERSION:-dev}\"/" recomplauncher.json > "$out/recomplauncher.json"
cp LICENSE "$out/licenses/RageRecomp-LICENSE.txt"
cp tools/dist-readme.txt "$out/READ-ME-FIRST.txt"
cp tools/run-readme.txt "$out/run/PUT-GAME-FILES-HERE.txt"

# Zig: only what "zig cc" needs to build a module without the C library.
# Windows modules still link against MinGW's runtime, so keep that there.
if [ $target = windows ]; then
    cp "$zig/zig.exe" "$out/tools/zig/"
    mkdir -p "$out/tools/zig/lib/libc/include"
    cp -r "$zig/lib/libc/mingw" "$out/tools/zig/lib/libc/"
    cp -r "$zig/lib/libc/include/any-windows-any" "$out/tools/zig/lib/libc/include/"
else
    cp "$zig/zig" "$out/tools/zig/"
fi
cp -r "$zig/lib/std" "$zig/lib/compiler_rt" "$zig/lib/include" "$zig/lib/c" "$out/tools/zig/lib/"
cp "$zig"/lib/*.zig "$out/tools/zig/lib/"
cp "$zig/LICENSE" "$out/licenses/Zig-LICENSE.txt"

# TinyCC, loaded as a separate library (LGPL)
if [ $target = windows ]; then
    cp "$tcc/libtcc.dll" "$tcc/win32/lib/chkstk.S" "$out/tools/tcc/"
else
    cp "$tcc/libtcc.so" "$out/tools/tcc/"
fi
cp "$tcc/COPYING" "$out/licenses/TinyCC-COPYING.txt"
echo "TinyCC source: https://repo.or.cz/tinycc.git, commit e23dabe (built unmodified)" > "$out/tools/tcc/SOURCE.txt"

# miniz (reads the MAME zips), compiled into the program
cp src/third_party/miniz/LICENSE "$out/licenses/miniz-LICENSE.txt"
# the LZMA SDK's 7z reader (reads .7z sets), compiled in; public domain
cp src/third_party/lzma/README.txt "$out/licenses/LZMA-SDK-public-domain.txt"

# shaders; the GPL extras are shipped as the source files they are
cp -r shaders "$out/"
if [ -d shaders/extra ]; then
    cp tools/gpl-2.0.txt "$out/licenses/GPL-2.0-shaders-extra.txt"   # crt-guest, crt-royale, ...
    cp tools/gpl-3.0.txt "$out/licenses/GPL-3.0-shaders-extra.txt"   # xBRZ
fi

# SDL2: beside RageRecomp.exe; in lib/ on Linux (the program's rpath is $ORIGIN/lib)
cp "$sdl_licence" "$out/licenses/SDL2-LICENSE.txt"
if [ $target = linux ]; then
    mkdir -p "$out/lib"
    cp -L "$3" "$out/lib/libSDL2-2.0.so.0"
    strip --strip-unneeded "$out/lib/libSDL2-2.0.so.0"
    # runs on glibc 2.31 and later (Steam Deck, older distributions): nothing
    # may need a newer glibc symbol than 2.29
    for f in "$out/ragerecomp" "$out/lib/libSDL2-2.0.so.0" "$out/tools/tcc/libtcc.so"; do
        v=$(objdump -T "$f" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)
        [ "$(printf '%s\n2.29\n' "$v" | sort -V | tail -1)" = 2.29 ] || { echo "$f needs glibc $v (at most 2.29 allowed)" >&2; exit 1; }
        echo "$(basename "$f"): needs glibc $v"
    done
fi
if [ $target = windows ]; then
    cp "$3" "$out/"
    # Windows text files want CRLF
    for f in "$out"/READ-ME-FIRST.txt "$out"/run/PUT-GAME-FILES-HERE.txt; do
        sed -i 's/$/\r/' "$f"
    done
fi
echo "packaged $out ($(du -sh "$out" | cut -f1))"
