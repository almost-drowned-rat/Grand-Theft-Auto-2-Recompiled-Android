#!/usr/bin/env bash
# release_linux.sh - GTA 2 Recompiled's Linux release: GTA2-x86_64.AppImage and GTA2-linux-x86_64.tar.gz (the same
# without the AppImage, no FUSE needed). Modelled on Zone of the Enders Recompiled's engine/tools/linux/release_linux.sh
# (the same author's). Neither holds game code or game data: the launcher (launcher/, GPL-3.0) carries a payload -
# the port and the runtime prebuilt as static libraries (GTA2_PORT_ONLY), SDL3 as libSDL3.so.0, unshield, the
# winrecomp lifter with an embedded Python (+ capstone, pefile), tools/setup_build.py, config/, the compiler bundle
# (clang + a glibc 2.35 sysroot) - and the player's first start builds the game from his own GTA 2 freeware installer
# (GTA2.exe) into data/ beside the AppImage: unpack the installer into game/, lift gta2.exe + binkw32.dll, compile,
# link. Nothing is downloaded.
#
#   bash tools/linux/release_linux.sh --release v0.7.0 --out <folder>
#        --llvm <the official LLVM release, unpacked> --sysroot <the dev sysroot (ZOE's make_dev_sysroot.sh)>
#        --bundle <the compiler bundle: llvm/ (clang++, ld.bfd, sysroot) + python/ (ZOE's make_toolchain_bundle.sh)>
#        --appimagetool <appimagetool-x86_64.AppImage>
#        [--deps <build_deps.sh's prefix; default: built into <out>/deps>]
#        [--site-packages <a Python site-packages with capstone 5.0.x and pefile; default ../winrecomp/.venv's>]
#        [--librashader <librashader_compat.sh's librashader.so (its librashader_compat.so beside it): the slang
#         shaders, shipped beside the game - none: shaders off>] [--librashader-licence <its LICENSE.md, MPL-2.0>]
#
# All paths absolute. Out: <out>/release/{GTA2-x86_64.AppImage, GTA2-linux-x86_64.tar.gz, SHA256SUMS.txt}.
set -euo pipefail
RELEASE= OUT= LLVM= SYSROOT= BUNDLE= TOOL= DEPS= SITE= RASHADER= RASHADER_LIC=
while [ $# -gt 0 ]; do
    case "$1" in
        --release) RELEASE="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --llvm) LLVM="$2"; shift 2 ;;
        --sysroot) SYSROOT="$2"; shift 2 ;;
        --bundle) BUNDLE="$2"; shift 2 ;;
        --appimagetool) TOOL="$2"; shift 2 ;;
        --deps) DEPS="$2"; shift 2 ;;
        --site-packages) SITE="$2"; shift 2 ;;
        --librashader) RASHADER="$2"; shift 2 ;;
        --librashader-licence) RASHADER_LIC="$2"; shift 2 ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
done
for v in RELEASE OUT LLVM SYSROOT BUNDLE TOOL; do
    [ -n "${!v}" ] || { echo "missing --${v,,} (see the header)" >&2; exit 2; }
done
[ ${#RELEASE} -le 32 ] || { echo "--release: at most 32 bytes" >&2; exit 2; }
HERE="$(cd "$(dirname "$0")" && pwd)"
RASHADER_COMPAT=
if [ -n "$RASHADER" ]; then
    RASHADER_COMPAT="$(dirname "$RASHADER")/librashader_compat.so"
    [ -f "$RASHADER_COMPAT" ] || { echo "no librashader_compat.so beside $RASHADER (librashader_compat.sh)" >&2; exit 2; }
    [ -f "$RASHADER_LIC" ] || { echo "--librashader needs --librashader-licence (MPL-2.0 text)" >&2; exit 2; }
fi
PORT="$(cd "$HERE/../.." && pwd)"
TOOLKIT="$(cd "$PORT/../winrecomp" && pwd)"
WS="$(cd "$PORT/../.." && pwd)"
SDL3_SRC="$WS/deps/android/src/SDL3"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
[ -n "$SITE" ] || SITE="$(ls -d "$TOOLKIT"/.venv/lib/python3*/site-packages | head -1)"
[ -d "$SITE/capstone" ] && [ -f "$SITE/pefile.py" ] || { echo "no capstone / pefile in $SITE" >&2; exit 2; }
APPIMAGE_NAME=GTA2-x86_64.AppImage
TAR_BASE=GTA2
TAR_NAME=GTA2-linux-x86_64.tar.gz
LAUNCHER=gta2_launcher                  # the tar.gz's program, the AppImage's usr/bin/gta2-launcher
DESKTOP_NAME="Grand Theft Auto 2 Recompiled"
DESKTOP_COMMENT="GTA 2 Recompiled - built from your own GTA 2 freeware installer on the first start"
TC=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$HERE/release_toolchain.cmake"
    -DGTA2_LINUX_LLVM="$LLVM" -DGTA2_LINUX_SYSROOT="$SYSROOT")

if [ -z "$DEPS" ]; then
    echo "== the dependencies (zlib, unshield, SDL3)"
    DEPS="$OUT/deps"
    bash "$HERE/build_deps.sh" --llvm "$LLVM" --sysroot "$SYSROOT" --out "$DEPS"
fi

echo "== the port and the runtime (static libraries, GTA2_PORT_ONLY)"
rm -rf "$OUT/build-port"
cmake -S "$PORT" -B "$OUT/build-port" "${TC[@]}" -DGTA2_LINUX_PREFIX="$DEPS" -DSDL3_DIR="$DEPS/lib/cmake/SDL3" \
    -DGTA2_PORT_ONLY=ON > "$OUT/build-port.configure.log"
ninja -C "$OUT/build-port" > "$OUT/build-port.log"

echo "== the launcher"
rm -rf "$OUT/build-launcher"
cmake -S "$PORT/launcher" -B "$OUT/build-launcher" "${TC[@]}" -DGTA2_LAUNCHER_SDL3="$SDL3_SRC" \
    -DGTA2_LAUNCHER_ICON="$PORT/assets/icon_64.bmp" -DSDL_SNDIO=OFF -DSDL_IBUS=OFF > "$OUT/build-launcher.configure.log"
ninja -C "$OUT/build-launcher" > "$OUT/build-launcher.log"
max="$(objdump -T "$OUT/build-launcher/gta2_launcher" "$DEPS/lib/libSDL3.so.0" "$DEPS/bin/unshield" \
       ${RASHADER:+"$RASHADER" "$RASHADER_COMPAT"} | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -uV | tail -1)"
case "$max" in GLIBC_2.[0-9]|GLIBC_2.[12][0-9]|GLIBC_2.3[0-5]) echo "  glibc: $max at most" ;;
    *) echo "the release needs $max (more than glibc 2.35)" >&2; exit 1 ;; esac

echo "== the payload"
S="$OUT/payload"
rm -rf "$S"
mkdir -p "$S/lib" "$S/bin" "$S/bundle" "$S/LICENSES" "$S/winrecomp"
cp "$OUT/build-port/libgta2_port.a" "$OUT/build-port/winrecomp_rt/libwinrecomp_rt.a" \
   "$OUT/build-port/winrecomp_rt/shaderpack/libxbox_shaderpack.a" "$PORT/src/exports.dynlist" "$S/lib/"
cp -L "$DEPS/lib/libSDL3.so.0" "$S/lib/libSDL3.so.0"
if [ -n "$RASHADER" ]; then
    cp -L "$RASHADER" "$S/lib/librashader.so"
    cp -L "$RASHADER_COMPAT" "$S/lib/librashader_compat.so"
fi
cp "$DEPS/bin/unshield" "$S/bin/unshield"
cp "$PORT/tools/setup_build.py" "$S/setup_build.py"
cp -r "$PORT/config" "$S/config"
# config/game.toml's exclude_manual = "../src/gta2_manual.c" (the hand-written functions: the lifter names its own
# copies sub_X_gen - without the file, duplicate symbols at the link)
mkdir -p "$S/src"
cp "$PORT/src/gta2_manual.c" "$S/src/gta2_manual.c"
# the lifter: the toolkit's tools/ (without its tests) and templates/ (recomp_types.h goes beside the lifted C)
(cd "$TOOLKIT" && find tools templates -type f ! -path '*/__pycache__/*' ! -name '*.pyc' ! -name 'test_*.py' \
     -print0 | xargs -0 cp --parents -t "$S/winrecomp/")
cp "$TOOLKIT/LICENSE" "$S/LICENSES/winrecomp-LICENSE.txt"
[ -f "$TOOLKIT/NOTICE" ] && cp "$TOOLKIT/NOTICE" "$S/LICENSES/winrecomp-NOTICE.txt"
# Python (the bundle's, python-build-standalone 3.12) with capstone and pefile (both pure Python + libcapstone.so)
cp -a "$BUNDLE/python" "$S/python"
PYV="$(basename "$(ls -d "$S"/python/lib/python3.1[0-9] | head -1)")"
cp -a "$SITE/capstone" "$SITE/pefile.py" "$SITE/peutils.py" "$SITE/ordlookup" "$S/python/lib/$PYV/site-packages/"
find "$S/python" -name __pycache__ -prune -exec rm -rf {} +
cp "$SITE"/capstone-*.dist-info/LICENSE* "$S/LICENSES/capstone-LICENSE.txt"
cp "$SITE"/pefile-*.dist-info/LICENSE* "$S/LICENSES/pefile-LICENSE.txt"
"$S/python/bin/$PYV" -I -c "import capstone, pefile" || { echo "the bundled Python cannot import capstone/pefile" >&2; exit 1; }
# the compiler: a zip unpacked once at the first start (removed after the build, its zip kept)
python3 -I - "$BUNDLE" "$S/bundle/llvm-linux.zip" <<'PY'
import glob, os, sys, zipfile
bundle, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for p in sorted(glob.glob(os.path.join(bundle, "llvm", "**", "*"), recursive=True)):
        if os.path.isfile(p):
            z.write(p, os.path.relpath(p, bundle))      # (its mode kept: the programs)
PY
for part in llvm python; do
    [ -d "$BUNDLE/$part/LICENSES" ] && cp -r "$BUNDLE/$part/LICENSES" "$S/LICENSES/$part"
done
cp "$PORT/launcher/LICENSE" "$S/LICENSES/launcher-GPL-3.0.txt"
cp "$PORT/launcher/third_party/miniz/LICENSE" "$S/LICENSES/miniz-MIT.txt"
cp "$PORT/launcher/fonts/LiberationFonts-OFL.txt" "$S/LICENSES/LiberationFonts-OFL.txt"
if [ -n "$RASHADER" ]; then
    cp "$RASHADER_LIC" "$S/LICENSES/librashader-MPL-2.0.txt"
    printf '%s\n' "librashader 0.12.1 (MPL-2.0), unmodified source: https://github.com/SnowflakePowered/librashader/tree/librashader-v0.12.1" \
        "(the shipped binary: its symbol versions adjusted for glibc 2.35 by tools/linux/librashader_compat.sh)" \
        > "$S/LICENSES/librashader-SOURCE.txt"
fi
cp "$DEPS"/SDL3-LICENSE.txt "$DEPS"/unshield-LICENSE.txt "$DEPS"/zlib-LICENSE.txt "$S/LICENSES/"
ZSHA="$(sha256sum "$S/bundle/llvm-linux.zip" | cut -d' ' -f1)"
ZSIZE="$(stat -c %s "$S/bundle/llvm-linux.zip")"
printf 'format\t2\nclang\tllvm/bin/clang++\npython\tpython/bin/%s\nbundle\tbundle/llvm-linux.zip\t%s\t%s\tllvm\tllvm\n' \
    "$PYV" "$ZSHA" "$ZSIZE" > "$S/toolchain.txt"
# the steps (launcher.c): check = the game's files of an earlier build; extract = from the installer, extract-folder
# = from an installed GTA 2 (exit 3: not the GTA 2 freeware release); then the lift lines
T=$'\t'
cat > "$S/lift.txt" <<EOF
# the first-start build (tools/setup_build.py): check / extract run by themselves, then the rest in order
check${T}{TOOLS}/setup_build.py${T}check${T}--game${T}{GAME}
extract${T}{TOOLS}/setup_build.py${T}extract${T}--installer${T}{INPUT}${T}--unshield${T}{TOOLS}/bin/unshield${T}--work${T}{WORK}${T}--game${T}{GAME}
extract-folder${T}{TOOLS}/setup_build.py${T}extract${T}--folder${T}{INPUT}${T}--work${T}{WORK}${T}--game${T}{GAME}
lift-gta2${T}{TOOLS}/setup_build.py${T}lift${T}--game${T}{GAME}${T}--toolkit${T}{TOOLS}/winrecomp${T}--config${T}{TOOLS}/config${T}--work${T}{WORK}${T}--module${T}gta2
lift-bink${T}{TOOLS}/setup_build.py${T}lift${T}--game${T}{GAME}${T}--toolkit${T}{TOOLS}/winrecomp${T}--config${T}{TOOLS}/config${T}--work${T}{WORK}${T}--module${T}bink
EOF
# the compile (CMakeLists.txt's gta2_gen / bink_gen: C11 with GNU extensions, -O2 -w -fwrapv -fno-strict-aliasing)
# and the link (as the normal Linux build's: the port whole, the runtime, SDL3, libm, exports.dynlist), with the
# bundle's clang++ as a C compiler; libSDL3.so.0 beside the game (rpath $ORIGIN)
CC=(cc --target=x86_64-unknown-linux-gnu "--sysroot={TOOLS}/llvm/sysroot" -x c -std=gnu11 -O2 -DNDEBUG -w -fwrapv
    -fno-strict-aliasing -fPIC)
LINK=(link --target=x86_64-unknown-linux-gnu "--sysroot={TOOLS}/llvm/sysroot" -fuse-ld=bfd -static-libgcc -nostdlib++
      -pthread "{OBJECTS}" -Wl,--whole-archive "{TOOLS}/lib/libgta2_port.a" -Wl,--no-whole-archive
      "{TOOLS}/lib/libwinrecomp_rt.a" "{TOOLS}/lib/libxbox_shaderpack.a" "{TOOLS}/lib/libSDL3.so.0" -lm -ldl
      "-Wl,--dynamic-list={TOOLS}/lib/exports.dynlist" '-Wl,-rpath,$ORIGIN' -o "{OUT}")
{
    echo "# the game's compile and link (tools/linux/release_linux.sh)"
    (IFS=$'\t'; echo "${CC[*]}")
    printf 'glob\t{WORK}/gen/gta2/src\t.c\nglob\t{WORK}/gen/bink/src\t.c\n'
    (IFS=$'\t'; echo "${LINK[*]}")
    printf 'copy\t{TOOLS}/lib/libSDL3.so.0\t{DATA}/libSDL3.so.0\n'
    [ -n "$RASHADER" ] && printf 'copy\t{TOOLS}/lib/librashader.so\t{DATA}/librashader.so\ncopy\t{TOOLS}/lib/librashader_compat.so\t{DATA}/librashader_compat.so\n'
    true
} > "$S/build.txt"
printf '%s' "$RELEASE" > "$S/release.txt"

echo "== the launcher with its payload"
cp "$OUT/build-launcher/gta2_launcher" "$OUT/launcher-plain"
"$LLVM/bin/llvm-objcopy" --strip-unneeded "$OUT/launcher-plain"
python3 -I - "$S" "$OUT/launcher-plain" "$OUT/launcher-payload" "$RELEASE" <<'PY'
import glob, os, shutil, struct, sys, zipfile
stage, launcher, out, release = sys.argv[1:5]
zp = out + ".zip"
with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for p in sorted(glob.glob(os.path.join(stage, "**", "*"), recursive=True)):
        if os.path.isfile(p):
            rel = os.path.relpath(p, stage)
            # (the compiler zip is compressed already)
            z.write(p, rel, zipfile.ZIP_STORED if rel.startswith("bundle/") else zipfile.ZIP_DEFLATED)
with open(out, "wb") as o:
    o.write(open(launcher, "rb").read())
    offset = o.tell()
    with open(zp, "rb") as f:
        shutil.copyfileobj(f, o, 1 << 20)
    # the trailer (launcher.c read_trailer): magic, the payload's offset, the release
    o.write(b"XRPAYLD1" + struct.pack("<Q", offset) + release.encode().ljust(32, b"\0"))
os.chmod(out, 0o755)
os.remove(zp)
print("  launcher + payload: %.1f MB" % (os.path.getsize(out) / 2 ** 20))
PY

echo "== the AppImage"
rm -rf "$OUT/release"
mkdir -p "$OUT/release"
A="$OUT/AppDir"
rm -rf "$A"
mkdir -p "$A/usr/bin"
cp "$OUT/launcher-payload" "$A/usr/bin/gta2-launcher"
# RecompLauncher's manifest: /recomplauncher.json in the AppImage, on top of the tar.gz's folder
cp "$PORT/recomplauncher.json" "$A/recomplauncher.json"
cat > "$A/AppRun" <<'EOF'
#!/bin/sh
# the launcher: it builds the game into data/ beside this AppImage at the first start, then starts it. Unpacked and
# started without APPIMAGE (RecompLauncher), the game's folder is APPDIR - this AppRun's folder unless already set.
HERE="$(dirname "$(readlink -f "$0")")"
[ -n "$APPIMAGE" ] || export APPDIR="${APPDIR:-$HERE}"
exec "$HERE/usr/bin/gta2-launcher" "$@"
EOF
chmod 755 "$A/AppRun"
cat > "$A/gta2.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$DESKTOP_NAME
Comment=$DESKTOP_COMMENT
Exec=gta2-launcher
Icon=gta2
Categories=Game;
Terminal=false
EOF
cp "$PORT/assets/icon_256.png" "$A/gta2.png"
ln -sf gta2.png "$A/.DirIcon"
ARCH=x86_64 "$TOOL" --appimage-extract-and-run --no-appstream "$A" "$OUT/release/$APPIMAGE_NAME" \
    > "$OUT/appimagetool.log" 2>&1 || { tail -20 "$OUT/appimagetool.log" >&2; exit 1; }
chmod 755 "$OUT/release/$APPIMAGE_NAME"

echo "== the tar.gz"
rm -rf "$OUT/tar-stage"
mkdir -p "$OUT/tar-stage/$TAR_BASE"
cp "$OUT/launcher-payload" "$OUT/tar-stage/$TAR_BASE/$LAUNCHER"
cp "$PORT/assets/icon_256.png" "$OUT/tar-stage/$TAR_BASE/icon.png"
cp "$PORT/recomplauncher.json" "$OUT/tar-stage/$TAR_BASE/recomplauncher.json"
printf '%s\n' "$DESKTOP_NAME $RELEASE - start ./$LAUNCHER (the first start builds the game from your GTA 2 freeware installer GTA2.exe, in this folder)." \
    > "$OUT/tar-stage/$TAR_BASE/README.txt"
tar -C "$OUT/tar-stage" -czf "$OUT/release/$TAR_NAME" "$TAR_BASE"
rm -rf "$OUT/tar-stage" "$OUT/launcher-plain"
(cd "$OUT/release" && sha256sum "$APPIMAGE_NAME" "$TAR_NAME" > SHA256SUMS.txt && cat SHA256SUMS.txt)
echo "done: $OUT/release/$APPIMAGE_NAME, $OUT/release/$TAR_NAME"
