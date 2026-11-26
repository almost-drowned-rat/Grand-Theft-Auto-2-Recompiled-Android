#!/usr/bin/env python3
"""GTA 2 Recompiled - the first-start build from the player's own GTA 2 (the 2004 Rockstar freeware release):
the steps every launcher runs (Android setup, Linux / Windows launchers). Python only (no shell, no sed), so the
same script runs on a phone's embedded CPython. The game's code is never shipped: it is lifted here from the
player's gta2.exe / binkw32.dll and compiled by the caller.

  setup_build.py extract --installer GTA2.exe --unshield <unshield program> --work DIR --game DIR
      the installer's InstallShield overlay split into DIR/installer, its group "App Executables" unpacked by
      unshield into --game (gta2.exe, the DLLs, data/); exit 3: not the supported installer / game
  setup_build.py extract --folder <an installed GTA 2> --work DIR --game DIR
      the same from a GTA 2 the player installed already (the folder with gta2.exe, or one above it): its files
      copied into --game (the folder is only read); exit 3: no supported gta2.exe in it
  setup_build.py check --game DIR      exit 3 unless DIR/gta2.exe is the supported build (sha1 below)
  setup_build.py lift --game DIR --toolkit WINRECOMP --config CONFIGDIR --work DIR [--module gta2|bink]
      the toolkit's lifter (disasm, func_id, abi, recomp) -> DIR/gen/<module>/src (C + headers), with the
      module prefixes tools/lift.sh applies (several modules link into one program)
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys

GTA2_SHA1 = "646e1d479fa2aea12b3ff448fa23f6362f9e012f"   # gta2.exe of the 2004 freeware release (build 2004-04-27)
WRONG = 3


def say(*a):
    print(*a, flush=True)


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def find(path, needle, chunk=1 << 20):
    """offset of the first needle in the file (streamed), -1 none"""
    with open(path, "rb") as f:
        pos, tail = 0, b""
        while True:
            b = f.read(chunk)
            if not b:
                return -1
            buf = tail + b
            i = buf.find(needle)
            if i >= 0:
                return pos - len(tail) + i
            tail = buf[-(len(needle) - 1):]
            pos += len(b)


def split_overlay(installer, out):
    """The InstallShield 7 setup's overlay: after a run of PADDINGX, records of name\\0 path\\0 version\\0
    decimal size\\0 data (tools/is_overlay_extract.py, streamed)."""
    p = find(installer, b"PADDINGXXPADDING")
    if p < 0:
        return 0
    n = 0
    with open(installer, "rb") as f:
        f.seek(p)
        while True:   # skip the padding
            b = f.read(8)
            if b not in (b"PADDINGX", b"XPADDING"):
                f.seek(-len(b), 1)
                break
        while True:
            fields = []
            for _ in range(4):
                s = bytearray()
                while True:
                    c = f.read(1)
                    if not c:
                        return n
                    if c == b"\0":
                        break
                    s += c
                fields.append(s.decode("latin-1"))
            name, path, ver, size = fields
            if not size.isdigit():
                return n
            size = int(size)
            dst = os.path.join(out, path.replace("\\", "/"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as o:
                left = size
                while left:
                    b = f.read(min(left, 1 << 20))
                    if not b:
                        return n
                    o.write(b)
                    left -= len(b)
            n += 1


def find_game_dir(top):
    """the folder holding gta2.exe (case-insensitive): top itself or one below it (not deeper than 3)"""
    base = top.rstrip(os.sep).count(os.sep)
    for root, dirs, files in os.walk(top):
        dirs.sort()
        if root.count(os.sep) - base >= 3:
            dirs[:] = []
        for f in files:
            if f.lower() == "gta2.exe" and any(d.lower() == "data" for d in dirs):
                return root, f
    return None, None


def cmd_folder(a):
    src, exe = find_game_dir(a.folder)
    if not src:
        say("no GTA 2 in that folder (gta2.exe with its data folder)")
        return WRONG
    if sha1(os.path.join(src, exe)) != GTA2_SHA1:
        say("gta2.exe is not the supported build (the 2004 freeware release)")
        return WRONG
    if os.path.realpath(src) == os.path.realpath(a.game):
        return 0
    say("copying the game files from", src)
    os.makedirs(a.game, exist_ok=True)
    for root, dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        if rel != "." and rel.split(os.sep)[0].lower() in ("saves", "player"):
            dirs[:] = []                    # the player's old saves stay where they are
            continue
        dst = os.path.join(a.game, rel) if rel != "." else a.game
        os.makedirs(dst, exist_ok=True)
        for f in files:
            # the original spellings as the installer writes them (gta2.exe in lower case: the check reads it so)
            name = "gta2.exe" if rel == "." and f.lower() == "gta2.exe" else f
            shutil.copyfile(os.path.join(root, f), os.path.join(dst, name))
    say("game files in", a.game)
    return 0


def cmd_extract(a):
    if a.folder:
        return cmd_folder(a)
    if not a.unshield:
        say("--installer needs --unshield")
        return 2
    inst = os.path.join(a.work, "installer")
    shutil.rmtree(inst, ignore_errors=True)
    say("splitting the installer")
    if split_overlay(a.installer, inst) == 0:
        say("not an InstallShield setup (the GTA 2 freeware installer is GTA2.exe, ~360 MB)")
        return WRONG
    cab = None
    for root, _, files in os.walk(inst):
        for f in files:
            if f.lower() == "data1.cab":
                cab = os.path.join(root, f)
    if not cab:
        say("no data1.cab in the installer")
        return WRONG
    out = os.path.join(a.work, "unpacked")
    shutil.rmtree(out, ignore_errors=True)
    say("unpacking the game files")
    r = subprocess.run([a.unshield, "-g", "App Executables", "-d", out, "x", cab], stdout=subprocess.DEVNULL)
    if r.returncode != 0:
        say("unshield failed:", r.returncode)
        return 1
    src = os.path.join(out, "App_Executables")
    if not os.path.isfile(os.path.join(src, "gta2.exe")):
        say("no gta2.exe in the installer")
        return WRONG
    if sha1(os.path.join(src, "gta2.exe")) != GTA2_SHA1:
        say("gta2.exe is not the supported build (the 2004 freeware release)")
        return WRONG
    os.makedirs(a.game, exist_ok=True)
    for name in os.listdir(src):   # into the game folder (a move: no second copy of 386 MB)
        d = os.path.join(a.game, name)
        if os.path.isdir(d):
            shutil.rmtree(d)
        elif os.path.exists(d):
            os.remove(d)
        shutil.move(os.path.join(src, name), d)
    shutil.rmtree(inst, ignore_errors=True)
    shutil.rmtree(out, ignore_errors=True)
    say("game files in", a.game)
    return 0


def cmd_check(a):
    exe = os.path.join(a.game, "gta2.exe")
    if not os.path.isfile(exe) or sha1(exe) != GTA2_SHA1:
        say("gta2.exe missing or not the supported build")
        return WRONG
    return 0


def lift(a, mod):
    exe = os.path.join(a.game, "gta2.exe" if mod == "gta2" else "binkw32.dll")
    an = os.path.join(a.work, "analysis", mod)
    out = os.path.join(a.work, "gen", mod)
    src = os.path.join(out, "src")
    cfg = ["--game-config", os.path.join(a.config, "game.toml")] if mod == "gta2" else []
    split = ["--split", "1000"] if mod == "bink" else []
    # each lifter step as `python -I -B -c <run the module>`: the toolkit put on sys.path by the child itself (an
    # isolated interpreter - the Android setup's embedded one - ignores PYTHONPATH)
    boot = ("import runpy, sys; sys.path.insert(0, %r); m = sys.argv.pop(1); "
            "runpy.run_module(m, run_name='__main__', alter_sys=True)" % a.toolkit)
    py = [os.environ.get("GTA2_SETUP_PYTHON") or sys.executable, "-I", "-B", "-c", boot]
    env = os.environ
    steps = [
        ["tools.disasm", exe] + cfg + ["-o", os.path.join(an, "disasm"), "--force"],
        ["tools.func_id", exe, "--functions", os.path.join(an, "disasm", "functions.json"),
         "--strings", os.path.join(an, "disasm", "strings.json"), "--xrefs", os.path.join(an, "disasm", "xrefs.json"),
         "--output", os.path.join(an, "func_id")],
        ["tools.abi_analysis", exe, "--disasm-dir", os.path.join(an, "disasm"), "--func-id-dir",
         os.path.join(an, "func_id"), "--output-dir", os.path.join(an, "abi")],
    ]
    shutil.rmtree(src, ignore_errors=True)
    steps.append(["tools.recomp", exe, "--game-name", mod] + cfg + ["--all"] + split +
                 ["--disasm-dir", os.path.join(an, "disasm"), "--func-id-dir", os.path.join(an, "func_id"),
                  "--abi-dir", os.path.join(an, "abi"), "-o", out, "--gen-dir", src])
    for s in steps:
        say(mod, s[0])
        r = subprocess.run(py + s, cwd=a.toolkit, env=env)
        if r.returncode != 0:
            return r.returncode
    # the module prefix for the symbols every generated module defines (tools/lift.sh)
    disp = os.path.join(src, "recomp_dispatch.c")
    t = open(disp, encoding="utf-8").read()
    for old, new in (("recomp_lookup", "recomp_lookup"), ("recomp_dispatch_init", "dispatch_init"),
                     ("recomp_dispatch_flat_bytes", "dispatch_flat_bytes"), ("recomp_get_count", "recomp_get_count"),
                     ("recomp_call_all", "recomp_call_all")):
        t = re.sub(r"\b%s\(" % old, "%s_%s(" % (mod, new), t)
    open(disp, "w", encoding="utf-8").write(t)
    for f in os.listdir(src):
        if (f.startswith("recomp_") and f.endswith(".c")) or f == "recomp_funcs.h":
            p = os.path.join(src, f)
            t = open(p, encoding="utf-8").read()
            t2 = re.sub(r"\bxbe_entry_point\b", "%s_entry_point" % mod, t)
            if t2 != t:
                open(p, "w", encoding="utf-8").write(t2)
    say(mod, "lifted ->", src)
    return 0


def cmd_lift(a):
    for mod in ([a.module] if a.module else ["gta2", "bink"]):
        rc = lift(a, mod)
        if rc:
            return rc
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("extract")
    src = e.add_mutually_exclusive_group(required=True)
    src.add_argument("--installer")
    src.add_argument("--folder")
    e.add_argument("--unshield")
    e.add_argument("--work", required=True)
    e.add_argument("--game", required=True)
    c = sub.add_parser("check")
    c.add_argument("--game", required=True)
    l = sub.add_parser("lift")
    l.add_argument("--game", required=True)
    l.add_argument("--toolkit", required=True)
    l.add_argument("--config", required=True)
    l.add_argument("--work", required=True)
    l.add_argument("--module", choices=["gta2", "bink"])
    a = ap.parse_args()
    return {"extract": cmd_extract, "check": cmd_check, "lift": cmd_lift}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
