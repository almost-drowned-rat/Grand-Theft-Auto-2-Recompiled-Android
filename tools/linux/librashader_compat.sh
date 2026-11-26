#!/usr/bin/env bash
# librashader_compat.sh - a copy of librashader (liblibrashader_capi.so, MPL-2.0, built on a new system) that loads
# with glibc 2.35 / the libstdc++ of GCC 12 (Ubuntu 22.04), for the Linux release (release_linux.sh --librashader).
#
# The build needs three symbols the old systems lack: __isoc23_strtol and __isoc23_strtoul (GLIBC_2.38, C23's
# strtol: the same but for "0b" prefixes) and __cxa_call_terminate (CXXABI_1.3.15, GCC 14: an exception leaving a
# noexcept function -> std::terminate). A small shim, librashader_compat.so (built here against the release sysroot),
# defines them; the copy's references lose their symbol versions (patchelf --clear-symbol-version), its version
# needs for GLIBC_2.38 / CXXABI_1.3.15 are marked weak (a missing one is then no error), and it gets the shim as a
# NEEDED library and RUNPATH $ORIGIN (both lie beside the game in data/).
#
#   bash tools/linux/librashader_compat.sh --in <liblibrashader_capi.so> --llvm <LLVM release> --sysroot <sysroot>
#        --patchelf <patchelf 0.18+> --out <folder>      -> <folder>/librashader.so, <folder>/librashader_compat.so
set -euo pipefail
IN= LLVM= SYSROOT= PATCHELF= OUT=
while [ $# -gt 0 ]; do
    case "$1" in
        --in) IN="$2"; shift 2 ;;
        --llvm) LLVM="$2"; shift 2 ;;
        --sysroot) SYSROOT="$2"; shift 2 ;;
        --patchelf) PATCHELF="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
done
[ -n "$IN" ] && [ -n "$LLVM" ] && [ -n "$SYSROOT" ] && [ -n "$PATCHELF" ] && [ -n "$OUT" ] || { echo "see the header" >&2; exit 2; }
mkdir -p "$OUT"
cat > "$OUT/librashader_compat.c" <<'EOF'
/* librashader_compat.c - what a librashader built with glibc 2.38 / GCC 14 needs from systems with glibc 2.35 and
 * GCC 12's libstdc++ (tools/linux/librashader_compat.sh). */
#include <stdlib.h>

long __isoc23_strtol(const char *s, char **end, int base) { return strtol(s, end, base); }
unsigned long __isoc23_strtoul(const char *s, char **end, int base) { return strtoul(s, end, base); }

/* libstdc++ (GCC 14): an exception left a noexcept function - std::terminate, the exception caught first */
void *__cxa_begin_catch(void *);
void _ZSt9terminatev(void) __attribute__((noreturn));
void __cxa_call_terminate(void *e) __attribute__((noreturn));
void __cxa_call_terminate(void *e)
{
    if (e)
        __cxa_begin_catch(e);
    _ZSt9terminatev();
}
EOF
"$LLVM/bin/clang" --target=x86_64-unknown-linux-gnu --sysroot="$SYSROOT" -O2 -fPIC -shared -fuse-ld=bfd \
    -Wl,-soname,librashader_compat.so -o "$OUT/librashader_compat.so" "$OUT/librashader_compat.c"
"$PATCHELF" --add-needed libstdc++.so.6 "$OUT/librashader_compat.so"
rm "$OUT/librashader_compat.c"

cp "$IN" "$OUT/librashader.so"
chmod 644 "$OUT/librashader.so"
"$PATCHELF" --clear-symbol-version __isoc23_strtol --clear-symbol-version __isoc23_strtoul \
    --clear-symbol-version __cxa_call_terminate --add-needed librashader_compat.so --set-rpath '$ORIGIN' \
    "$OUT/librashader.so"
# the version needs nothing refers to any more: weak (VER_FLG_WEAK), so the loader does not ask for them
python3 -I - "$OUT/librashader.so" GLIBC_2.38 CXXABI_1.3.15 <<'PY'
import struct, sys
path, names = sys.argv[1], [n.encode() for n in sys.argv[2:]]
d = bytearray(open(path, "rb").read())
shoff, = struct.unpack_from("<Q", d, 0x28)
shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x3A)
secs = [struct.unpack_from("<IIQQQQIIQQ", d, shoff + i * shentsize) for i in range(shnum)]
def sec(t):
    return [s for s in secs if s[1] == t]
verneed = sec(0x6ffffffe)[0]
strtab = secs[verneed[6]]                          # sh_link: .dynstr
def cstr(off):
    o = strtab[4] + off
    return bytes(d[o:d.index(b"\0", o)])
off, done = verneed[4], 0
while True:
    vn_version, vn_cnt, vn_file, vn_aux, vn_next = struct.unpack_from("<HHIII", d, off)
    a = off + vn_aux
    for _ in range(vn_cnt):
        vna_hash, vna_flags, vna_other, vna_name, vna_next = struct.unpack_from("<IHHII", d, a)
        if cstr(vna_name) in names:
            struct.pack_into("<H", d, a + 4, vna_flags | 0x2)
            done += 1
        a += vna_next
    if not vn_next:
        break
    off += vn_next
open(path, "wb").write(d)
print("  %d version need(s) made weak" % done)
PY
echo "librashader for glibc 2.35: $OUT/librashader.so (+ librashader_compat.so)"
