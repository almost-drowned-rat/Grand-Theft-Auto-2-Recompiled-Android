#!/usr/bin/env python3
"""The Android APK's first-start build kit for Grand Theft Auto 2 Recompiled (agent L, 2026-11-26; from Zone of the
Enders Recompiled's packaging/android/make_setup_assets.py). The APK holds no game code: at the first start the
setup unpacks the player's own GTA 2 freeware installer, lifts gta2.exe / binkw32.dll (winrecomp, embedded CPython)
and compiles + links the game on the device (clang / lld) against the port's own prebuilt libraries.

    python3 make_setup_assets.py --port <gta2 repo> --winrecomp <winrecomp> --port-build <GTA2_PORT_ONLY build>
        --device-toolchain <device_toolchain> --ndk <NDK> --python <python.org Android prefix>
        --capstone-so <arm64 libcapstone.so> --native <the app's jni build> --unshield <arm64 unshield>
        --sdl3 <arm64 libSDL3.so> --out <folder>

Out:
  <out>/assets/setup/python.zip     the stdlib + site-packages (capstone + its libcapstone.so, pefile)
  <out>/assets/setup/tools.zip      setup_build.py, config/, winrecomp/tools (the lifter), lib/*.a, build.txt
  <out>/assets/setup/toolchain.zip  sysroot (+ API 30) and clang's resource dir
  <out>/assets/setup/version.txt    one line: the kit's id (a new kit -> the game is built again)
  <out>/libs/arm64-v8a/             the programs (lib*_exe.so) and libraries the APK carries - no game code
  <out>/assets/licenses/            the third-party licence texts
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile

API = 30


def zip_tree(zf, src, arc, skip=()):
    for root, dirs, files in os.walk(src):
        dirs[:] = sorted(d for d in dirs if d not in skip and d != "__pycache__")
        for f in sorted(files):
            if f.endswith(".pyc"):
                continue
            p = os.path.join(root, f)
            rel = os.path.relpath(p, src)
            zf.write(p, os.path.join(arc, rel) if arc else rel)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    for k in ("port", "winrecomp", "port-build", "device-toolchain", "ndk", "python", "capstone-so", "native",
              "unshield", "sdl3", "out"):
        ap.add_argument("--" + k, required=True)
    ap.add_argument("--librashader", help="arm64 liblibrashader_capi.so (slang shaders; shipped as librashader.so with "
                    "the NDK's libc++_shared.so); without it the game's SHADERS page stays greyed out")
    ap.add_argument("--librashader-licence", help="librashader's LICENSE.md (MPL-2.0)")
    ap.add_argument("--release", default="dev", help="the release name in the kit id (e.g. android-0.1.0)")
    a = ap.parse_args(argv)

    host = os.path.join(a.ndk, "toolchains/llvm/prebuilt/linux-x86_64")
    ndk_sys = os.path.join(host, "sysroot")
    strip = os.path.join(host, "bin/llvm-strip")
    shutil.rmtree(a.out, ignore_errors=True)
    setup = os.path.join(a.out, "assets/setup")
    libs = os.path.join(a.out, "libs/arm64-v8a")
    lic = os.path.join(a.out, "assets/licenses")
    for d in (setup, libs, lic):
        os.makedirs(d)

    # ---- build.txt: the generated C (each .c finds its own headers next to it: no -I) + the link
    drv = ["--target=aarch64-linux-android%d" % API, "--sysroot={TC}/sysroot", "-resource-dir={TC}/lib/clang/23"]
    cc = drv + ["-std=gnu11", "-O2", "-w", "-fwrapv", "-fno-strict-aliasing", "-fPIC", "-DANDROID",
                "-fdata-sections", "-ffunction-sections", "-funwind-tables", "-fstack-protector-strong"]
    link = drv + ["--ld-path={NATIVE}/libld_exe.so", "-shared", "-O2", "-Wl,--build-id=sha1", "-Wl,--gc-sections",
                  "-Wl,--strip-debug", "-Wl,-z,max-page-size=16384", "-Wl,-soname,libgta2_game.so", "-o", "{OUT}",
                  "{OBJECTS}", "-Wl,--whole-archive", "{TOOLS}/lib/libgta2_port.a", "-Wl,--no-whole-archive",
                  "{TOOLS}/lib/libwinrecomp_rt.a", "{TOOLS}/lib/libxbox_shaderpack.a",
                  "{NATIVE}/libSDL3.so", "-landroid", "-llog", "-lm", "-ldl"]
    build_txt = "\n".join([
        "# the game's compile and link on the device (make_setup_assets.py); {TOOLS} the kit, {TC} the toolchain,",
        "# {WORK} the lift (setup_build.py), {NATIVE} nativeLibraryDir, {OUT} the game library",
        "\t".join(["cc"] + cc),
        "glob\t{WORK}/gen/gta2/src\t.c",
        "glob\t{WORK}/gen/bink/src\t.c",
        "\t".join(["link"] + link), ""])

    # ---- tools.zip: the setup script, the port's config, the lifter, the port libraries (debug info stripped:
    # it names the build machine's folders)
    with zipfile.ZipFile(os.path.join(setup, "tools.zip"), "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        z.write(os.path.join(a.port, "tools/setup_build.py"), "setup_build.py")
        zip_tree(z, os.path.join(a.port, "config"), "config")
        # the hand-written replacements: the lifter names their generated bodies sub_X_gen (game.toml manual_file)
        z.write(os.path.join(a.port, "src/gta2_manual.c"), "src/gta2_manual.c")
        zip_tree(z, os.path.join(a.winrecomp, "tools"), "winrecomp/tools", skip=("tests", "test"))
        zip_tree(z, os.path.join(a.winrecomp, "templates/runtime"), "winrecomp/templates/runtime")   # recomp_types.h
        stripped = os.path.join(a.out, "stripped")
        os.makedirs(stripped)
        for rel in ("libgta2_port.a", "winrecomp_rt/libwinrecomp_rt.a", "winrecomp_rt/shaderpack/libxbox_shaderpack.a"):
            dst = os.path.join(stripped, os.path.basename(rel))
            subprocess.run([strip, "--strip-debug", "-o", dst, os.path.join(a.port_build, rel)], check=True)
            z.write(dst, "lib/" + os.path.basename(rel))
        shutil.rmtree(stripped)
        z.writestr("build.txt", build_txt)
        z.write(os.path.join(a.port, "LICENSE"), "LICENSES/GTA2-Recompiled-LICENSE.txt") \
            if os.path.isfile(os.path.join(a.port, "LICENSE")) else None

    # ---- toolchain.zip: the device toolchain's sysroot (API 29) + the NDK's API 30 libs, clang's resource dir
    dtc = a.device_toolchain
    ndk_lib = os.path.join(ndk_sys, "usr/lib/aarch64-linux-android")
    with zipfile.ZipFile(os.path.join(setup, "toolchain.zip"), "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        zip_tree(z, os.path.join(dtc, "sysroot"), "sysroot")
        zip_tree(z, os.path.join(dtc, "lib"), "lib")
        zip_tree(z, os.path.join(ndk_lib, str(API)), "sysroot/usr/lib/aarch64-linux-android/%d" % API)

    # ---- python.zip: the stdlib (no tests, no GUI) + capstone (the pure-Python binding with its libcapstone.so) +
    # pefile (pure Python), from the lifter's own environment
    venv_sp = None
    for root, dirs, _ in os.walk(os.path.join(a.winrecomp, ".venv/lib")):
        if os.path.basename(root) == "site-packages":
            venv_sp = root
            break
    if not venv_sp:
        raise SystemExit("--winrecomp: no .venv site-packages (capstone, pefile)")
    with zipfile.ZipFile(os.path.join(setup, "python.zip"), "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        zip_tree(z, os.path.join(a.python, "lib/python3.15"), "lib/python3.15",
                 skip=("test", "idlelib", "tkinter", "turtledemo", "ensurepip", "site-packages"))
        sp = "lib/python3.15/site-packages/"
        zip_tree(z, os.path.join(venv_sp, "capstone"), sp + "capstone", skip=("lib", "include"))
        cs = os.path.join(a.out, "libcapstone.so")
        subprocess.run([strip, "--strip-debug", "-o", cs, a.capstone_so], check=True)
        z.write(cs, sp + "capstone/lib/libcapstone.so")
        os.remove(cs)
        for f in sorted(os.listdir(venv_sp)):
            if f in ("pefile.py", "peutils.py") or f == "ordlookup":
                p = os.path.join(venv_sp, f)
                if os.path.isdir(p):
                    zip_tree(z, p, sp + f)
                else:
                    z.write(p, sp + f)

    # ---- the kit's id: the release name + the three zips' hashes
    h = hashlib.sha256()
    for f in ("tools.zip", "toolchain.zip", "python.zip"):
        with open(os.path.join(setup, f), "rb") as fh:
            for block in iter(lambda: fh.read(1 << 20), b""):
                h.update(block)
    with open(os.path.join(setup, "version.txt"), "w") as fh:
        fh.write("%s %s\n" % (a.release, h.hexdigest()[:16]))

    # ---- the APK's native programs and libraries (no game code)
    def put(src, name, do_strip=True):
        dst = os.path.join(libs, name)
        shutil.copy2(src, dst)
        if do_strip:
            subprocess.run([strip, "--strip-unneeded", dst], check=True)
    put(a.sdl3, "libSDL3.so")
    put(a.unshield, "libunshield_exe.so")

    def native(f):
        for d in ("", "out", "adrenotools/src/hook"):
            p = os.path.join(a.native, d, f)
            if os.path.isfile(p):
                return p
        raise SystemExit("--native: no %s" % f)
    for f in ("libgta2_app.so", "libvulkan_port.so", "libpython_exe.so", "libld_exe.so", "libmain_hook.so",
              "libhook_impl.so", "libfile_redirect_hook.so", "libgsl_alloc_hook.so"):
        put(native(f), f)
    put(os.path.join(a.python, "lib/libpython3.15.so"), "libpython3.15.so")
    put(os.path.join(a.python, "lib/libcrypto_python.so"), "libcrypto_python.so")
    if a.librashader:   # loaded by name at run time (winrecomp post/shader_pass.c); it needs libc++_shared.so
        put(a.librashader, "librashader.so")
        put(os.path.join(ndk_sys, "usr/lib/aarch64-linux-android/libc++_shared.so"), "libc++_shared.so")
    put(os.path.join(dtc, "bin/clang"), "libclang_exe.so", do_strip=False)
    put(os.path.join(dtc, "bin/ld.lld"), "liblld_exe.so", do_strip=False)

    # ---- licences
    def lic_copy(src, name):
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(lic, name))
    lic_copy(os.path.join(a.python, "lib/python3.15/LICENSE.txt"), "CPython-PSF.txt")
    lic_copy(os.path.join(a.winrecomp, "LICENSE"), "winrecomp-LICENSE.txt")
    lic_copy(os.path.join(a.winrecomp, "NOTICE"), "winrecomp-NOTICE.txt")
    if a.librashader and a.librashader_licence:
        lic_copy(a.librashader_licence, "librashader-MPL-2.0.txt")
    for root, _, files in os.walk(os.path.join(dtc, "LICENSES")):
        for f in files:
            lic_copy(os.path.join(root, f), "toolchain_" + f)
    with open(os.path.join(lic, "README.txt"), "w") as fh:
        fh.write("Grand Theft Auto 2 Recompiled for Android - third-party software\n\n"
                 "The app builds the game on this device from your own GTA 2 (the 2004 Rockstar freeware\n"
                 "installer); it ships no game code or data.\n\n"
                 "winrecomp (runtime, lifter)          MIT                 see winrecomp-LICENSE / -NOTICE\n"
                 "SDL 3                                zlib                libsdl.org\n"
                 + ("librashader (slang shaders)          MPL-2.0             github.com/SnowflakePowered/librashader\n"
                    "LLVM libc++ (libc++_shared.so)       Apache-2.0 WITH LLVM-exception   llvm.org\n"
                    if a.librashader else "") +
                 "libadrenotools                       BSD-2-Clause        github.com/bylaws/libadrenotools\n"
                 "CPython 3.15 (embedded, the lifter)  PSF License         python.org\n"
                 "capstone 5                           BSD-3-Clause        capstone-engine.org\n"
                 "pefile                               MIT                 github.com/erocarrera/pefile\n"
                 "unshield                             MIT                 github.com/twogood/unshield\n"
                 "LLVM clang + lld (device compiler)   Apache-2.0 WITH LLVM-exception   llvm.org\n"
                 "Android NDK sysroot                  see toolchain_* and NOTICE texts\n"
                 "dr_libs, stb_vorbis, stb_truetype    public domain / MIT\n"
                 "Noto Sans, Noto Sans CJK (in the translations pak)   SIL OFL 1.1\n\n"
                 "Licence texts: the files in this folder.\n")

    sizes = {f: os.path.getsize(os.path.join(setup, f)) >> 20 for f in os.listdir(setup)}
    print("setup kit: %s MB; libs: %s" % (sizes, sorted(os.listdir(libs))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
