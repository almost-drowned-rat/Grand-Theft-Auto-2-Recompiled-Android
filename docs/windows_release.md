# Windows release (zip)

`tools/windows/release_windows.ps1` builds `GTA2-windows-x86_64.zip`. Like the Linux release
(`docs/linux_release.md`) it holds no game code and no game data: the player's first start builds the game from his
own GTA 2 freeware installer (`GTA2.exe`, the 2004 Rockstar release) or an installed GTA 2 folder.

## What is in it

The zip's top holds only `gta2_launcher.exe` and `recomplauncher.json` (RecompLauncher's manifest).

- The launcher: `launcher/` (GPL-3.0-or-later, from Zone of the Enders Recompiled's engine/launcher), on Windows with
  the Windows 95 window `launcher/game_setup_ui_win95.c` (plain Win32 + GDI). SDL3 is linked in statically; the exe
  carries the icon, VERSIONINFO and an asInvoker manifest. It is not signed.
- The payload, a zip appended to the launcher (trailer `XRPAYLD1`, `tools/windows/append_payload.py`):
  - `lib/`: the port and the runtime as static libraries (`GTA2_PORT_ONLY=ON`), `gta2_res.o` (the game exe's icon,
    manifest and VERSIONINFO), `SDL3.dll` and its import library, `librashader.dll` (0.12.1, built from source,
    Vulkan only - imports only Windows' own DLLs and the UCRT; the script refuses any other import)
  - `bin/unshield.exe` (static, zlib linked in)
  - `setup_build.py`, `config/` and `src/gta2_manual.c` (game.toml's `exclude_manual`)
  - the winrecomp lifter (`winrecomp/tools`, `winrecomp/templates`)
  - the embeddable Python 3.12 with capstone and pefile (`python/`)
  - the compiler: llvm-mingw cut to x86-64 (the toolkit's `prune_llvm_mingw.py`), `bundle/llvm-mingw.zip`
  - `toolchain.txt`, `lift.txt`, `build.txt`, `release.txt` and `LICENSES/`

## Building it

Needs on PATH: llvm-mingw's `clang` (and `llvm-windres` beside it), CMake, Ninja, Python 3. Inputs:

    powershell -ExecutionPolicy Bypass -File tools\windows\release_windows.ps1 -Release v0.7.0 -Out <folder>
        -SdlDev <SDL3-devel-3.4.x-mingw>\x86_64-w64-mingw32 -SdlSrc <SDL3-3.4.x source>
        -Unshield <unshield.exe, static> -UnshieldLicence <unshield's LICENSE> -ZlibLicence <zlib's LICENSE>
        -LlvmZip <llvm-mingw-...-ucrt-x86_64-slim.zip> -PyZip <python-3.12.x-embed-amd64.zip>
        -SitePackages <a Python 3.12 site-packages with capstone 5 and pefile>
        -Librashader <librashader.dll> -LibrashaderLicence <its LICENSE.md (MPL-2.0)>

Out: `<folder>\release\GTA2-windows-x86_64.zip` and `SHA256SUMS-windows.txt`. Build from a clean checkout: the game's
log names the port's commit (`-dirty` with local changes).

## The player's first start

As on Linux (`docs/linux_release.md`): the payload is unpacked into `data\tools`, the compiler once (SHA-256
checked); `setup_build.py extract` puts the game's files into `game\` (from the installer, or copied from an
installed GTA 2); gta2.exe and binkw32.dll are lifted to C, compiled (one job per core, at most one per 2.5 GB RAM)
and linked into `data\gta2_recomp.exe` with `SDL3.dll` and `librashader.dll` beside it (shaders work out of the box,
as on Linux and Android); then the game starts with `WR_ROOT` = the
launcher's folder. A start of `data\gta2_recomp.exe` itself finds the same folder (the one above `data`).

Later starts start the game at once; a new release and `--repair` rebuild from `game\` alone. Quitting during the
installation asks first and stops every tool (a job object).

Non-interactive (tests, RecompLauncher):

    set RECOMP_NO_DIALOGS=1
    gta2_launcher.exe --installer=<GTA2.exe or folder> [--file=<GTA2-translations.pak>] --go --no-start

`GTA2_LAUNCHER_SHOT=<file.bmp>` saves the setup page as a picture and quits.
