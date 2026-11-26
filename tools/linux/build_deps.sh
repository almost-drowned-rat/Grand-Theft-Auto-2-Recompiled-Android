#!/usr/bin/env bash
# build_deps.sh - the Linux release's own builds of its dependencies, against the glibc 2.35 sysroot (release_toolchain.cmake):
#   zlib 1.3.1 (static, only for unshield), unshield 1.6.2 (MIT; the player's first start unpacks the GTA 2 freeware
#   installer's data1.cab with it - zlib linked in, glibc 2.35 only), SDL3 3.4.18 (zlib licence; shared: libSDL3.so.0,
#   shipped beside the game).
#
#   bash tools/linux/build_deps.sh --llvm <LLVM release> --sysroot <dev sysroot> --out <prefix folder>
#        [--zlib <zlib-1.3.1 source>] [--unshield <unshield 1.6.2 source>] [--sdl3 <SDL 3.4.18 source>]
#
# Defaults for the sources: the workspace's deps/ (deps/linux/src/zlib-1.3.1, deps/android/src/{unshield,SDL3}).
# Out: <prefix>/{lib,include,bin/unshield,lib/libSDL3.so.0,lib/cmake/SDL3}.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PORT="$(cd "$HERE/../.." && pwd)"
WS="$(cd "$PORT/../.." && pwd)"
LLVM= SYSROOT= OUT=
ZLIB="$WS/deps/linux/src/zlib-1.3.1" UNSHIELD="$WS/deps/android/src/unshield" SDL3="$WS/deps/android/src/SDL3"
while [ $# -gt 0 ]; do
    case "$1" in
        --llvm) LLVM="$2"; shift 2 ;;
        --sysroot) SYSROOT="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --zlib) ZLIB="$2"; shift 2 ;;
        --unshield) UNSHIELD="$2"; shift 2 ;;
        --sdl3) SDL3="$2"; shift 2 ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
done
[ -n "$LLVM" ] && [ -n "$SYSROOT" ] && [ -n "$OUT" ] || { echo "see the header" >&2; exit 2; }
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
TC=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$HERE/release_toolchain.cmake"
    -DGTA2_LINUX_LLVM="$LLVM" -DGTA2_LINUX_SYSROOT="$SYSROOT" -DGTA2_LINUX_PREFIX="$OUT" -DCMAKE_INSTALL_PREFIX="$OUT"
    -DCMAKE_INSTALL_LIBDIR=lib)
B="$OUT/_build"
mkdir -p "$B"

echo "== zlib (static)"
cmake -S "$ZLIB" -B "$B/zlib" "${TC[@]}" -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DZLIB_BUILD_EXAMPLES=OFF > "$B/zlib.log"
ninja -C "$B/zlib" install >> "$B/zlib.log"
rm -f "$OUT"/lib/libz.so*                       # (unshield takes the static one)

echo "== unshield (zlib linked in)"
cmake -S "$UNSHIELD" -B "$B/unshield" "${TC[@]}" -DBUILD_STATIC=ON -DUSE_OUR_OWN_MD5=ON -DBUILD_TESTING=OFF \
    -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON -DZLIB_ROOT="$OUT" -DZLIB_LIBRARY="$OUT/lib/libz.a" \
    -DZLIB_INCLUDE_DIR="$OUT/include" > "$B/unshield.log"
ninja -C "$B/unshield" >> "$B/unshield.log"
mkdir -p "$OUT/bin"
cp "$B/unshield/src/unshield" "$OUT/bin/unshield"
"$LLVM/bin/llvm-strip" "$OUT/bin/unshield"
cp "$UNSHIELD/LICENSE" "$OUT/unshield-LICENSE.txt"
cp "$ZLIB/LICENSE" "$OUT/zlib-LICENSE.txt" 2>/dev/null || sed -n '/Copyright/,$p' "$ZLIB/README" > "$OUT/zlib-LICENSE.txt"

# the Vulkan headers the runtime compiles against (SDL3's own copy; Vulkan is loaded through SDL at run time)
mkdir -p "$OUT/include"
cp -r "$SDL3/src/video/khronos/vulkan" "$SDL3/src/video/khronos/vk_video" "$OUT/include/"

echo "== SDL3 (shared)"
cmake -S "$SDL3" -B "$B/sdl3" "${TC[@]}" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF \
    -DSDL_EXAMPLES=OFF -DSDL_SNDIO=OFF -DSDL_IBUS=OFF > "$B/sdl3.log"
ninja -C "$B/sdl3" install >> "$B/sdl3.log"
cp "$SDL3/LICENSE.txt" "$OUT/SDL3-LICENSE.txt"

max="$(objdump -T "$OUT/bin/unshield" "$OUT"/lib/libSDL3.so.0 | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -uV | tail -1)"
echo "deps in $OUT (glibc $max at most)"
