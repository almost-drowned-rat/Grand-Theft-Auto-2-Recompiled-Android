# Linux release (AppImage + tar.gz)

`tools/linux/release_linux.sh` builds `GTA2-x86_64.AppImage` and `GTA2-linux-x86_64.tar.gz` (the same without the
AppImage). Neither holds game code or game data. The player's first start builds the game from his own GTA 2
freeware installer (`GTA2.exe`, the 2004 Rockstar release, ~362 MB).

## What is in it

- `launcher/`: the launcher (GPL-3.0-or-later, taken from Zone of the Enders Recompiled's engine/launcher). It is a
  program of its own, not the game, and its setup window uses the Windows 2000 look. The rows are:
  - "GTA 2 installer or folder:", with Folder... and Browse...: GTA2.exe, or a folder where that GTA 2 is already
    installed
  - "Translations (optional):" (GTA2-translations.pak)
  - the target folder
- `recomplauncher.json`, RecompLauncher's manifest: `/recomplauncher.json` in the AppImage, and on top of the tar.gz.
- The payload, a zip appended to the launcher:
  - the port and runtime as static libraries (`GTA2_PORT_ONLY=ON`) plus `exports.dynlist`
  - `libSDL3.so.0`
  - `unshield` (zlib linked in)
  - `setup_build.py`, `config/` and `src/gta2_manual.c` (game.toml's `exclude_manual`)
  - the winrecomp lifter (`tools/`, `templates/`)
  - Python 3.12 with capstone and pefile
  - the compiler bundle (clang++, ld.bfd, a glibc 2.35 sysroot), as `bundle/llvm-linux.zip`
  - `lift.txt`, `build.txt`, `toolchain.txt`, `release.txt` and `LICENSES/`

## Building it

Inputs are the same as ZOE's Linux release:

- the official LLVM release (`zoe_work/toolchain/llvm-full`)
- the dev sysroot (`zoe_work/toolchain/sysroot-dev`, from ZOE's `make_dev_sysroot.sh`)
- the compiler bundle with `llvm/` and `python/` (`zoe_work/toolchain/bundle2`, from ZOE's `make_toolchain_bundle.sh`)
- appimagetool

Give every path as an absolute path.

    bash tools/linux/release_linux.sh --release v0.7.0 --out <abs folder> \
        --llvm <llvm-full> --sysroot <sysroot-dev> --bundle <bundle2> \
        --appimagetool <appimagetool-x86_64.AppImage> [--deps <prefix>] [--librashader <so>]

- `tools/linux/build_deps.sh` builds zlib, unshield 1.6.2 and SDL3 3.4.18 against the sysroot. It also copies the
  Vulkan headers (SDL3's copy). Without `--deps`, the release script runs it into `<out>/deps`.
  - Sources: `deps/linux/src/zlib-1.3.1` and `deps/android/src/{unshield,SDL3}`.
- capstone and pefile come from `../winrecomp/.venv` by default (`--site-packages` to change it).
- `--librashader` is optional; without it the release has no shaders (the menu greys them out). Pass
  `--librashader-licence deps/android/src/librashader/LICENSE.md` (MPL-2.0) with it.
  - The workspace's build (`deps/linux/build/librashader/.../liblibrashader_capi.so`) needs glibc 2.38 and GCC 14's
    libstdc++.
  - `tools/linux/librashader_compat.sh` makes a copy that loads on glibc 2.35 and GCC 12. It adds a shim,
    `librashader_compat.so`, with `__isoc23_strtol`, `__isoc23_strtoul` and `__cxa_call_terminate`. The copy's
    references to them are unversioned, those two version needs are weak, and the copy has `NEEDED` on the shim and
    `RUNPATH $ORIGIN`. It needs patchelf (`pip install patchelf`).
  - Tested with Ubuntu 22.04's loader, libc and libstdc++: dlopen works, and so does `libra_preset_create`.
- The script refuses any release part that needs a glibc newer than 2.35.
- Out: `<out>/release/` holds the AppImage, the tar.gz and `SHA256SUMS.txt`.

## The player's first start

The game's folder (`WR_ROOT`) is the AppImage's folder. It holds:

- the AppImage (the only file on top)
- `game/`, `saves/`, `data/`, `media/`
- the built program, `data/gta2_recomp`, with `libSDL3.so.0` beside it

The steps (data/launcher.log names each one with its time):

1. Unpack the payload into `data/tools` and the compiler (SHA-256 checked).
2. `setup_build.py extract` puts the game's files into `game/`:
   - from the installer (`--installer`), or
   - copied from an installed GTA 2 (`--folder`).

   Exit 3 means it is not the freeware release: the page asks again.
3. Lift gta2 and bink to C in `data/build/work`.
4. Compile in parallel. There are at most as many jobs as cores, and one per 2.5 GB of RAM. `GTA2_LAUNCHER_JOBS`
   caps it further.
5. Link, then copy `libSDL3.so.0` into data/.
6. Copy the translations to `<root>/GTA2-translations.pak`, and write the stamp `data/build/stamp.txt`: the release,
   plus the SHA-256 of `game/gta2.exe`.
7. Start the game with `WR_ROOT=<root>`.

The lifted C, the objects and the unpacked compiler are removed afterwards.

**Portable** (the maintainer's rule): after the first start nothing refers to the player's installer or folder.

- `game/` holds real copies of the game's files (no links).
- A new release and `--repair` lift from `game/gta2.exe` and `game/binkw32.dll` (sha1-checked). They never ask for
  the installer or read it again.
- `data/launcher.ini` remembers the last installer or folder only to pre-fill the setup page. It is never taken by
  itself, and nothing needs it.
- The installer or folder is read again only when `game/` is missing or broken, and only when the player chooses it
  again.

Later starts:

- The game starts at once.
- After a new release, the game is rebuilt from `game/`.
- `--repair` rebuilds from `game/`.
- `--setup` shows the setup page.

Non-interactive (tests, RecompLauncher):

    RECOMP_NO_DIALOGS=1 SDL_VIDEO_DRIVER=offscreen ./GTA2-x86_64.AppImage --installer <GTA2.exe or folder> \
        [--file <GTA2-translations.pak>] --go --no-start

`--folder <dir>` does the same as `--installer`. Either one takes a file or a folder.

Without dialogs, the launcher prints `progress 0.42 <step>` lines on stdout, as RecompLauncher's setup step expects.
When a step fails, the reason is the last line on stderr.

`GTA2_LAUNCHER_SHOT=<file.bmp>` saves the setup page as a picture and then quits. Use it with
`SDL_VIDEO_DRIVER=offscreen` to check the layout without a window.
