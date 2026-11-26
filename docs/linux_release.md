# Linux release (AppImage + tar.gz)

`tools/linux/release_linux.sh` builds `GTA2-x86_64.AppImage` and `GTA2-linux-x86_64.tar.gz` (the same without the
AppImage). Neither holds game code or game data. The player's first start builds the game from his own GTA 2
freeware installer (`GTA2.exe`, the 2004 Rockstar release, ~362 MB).

## What is in it

- `launcher/`: the launcher (GPL-3.0-or-later, taken from Zone of the Enders Recompiled's engine/launcher). It is a
  program of its own, not the game, and its setup window uses the Windows 2000 look. The rows are "GTA 2
  installer:", "Translations (optional):" (GTA2-translations.pak) and the target folder.
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
- `--librashader` is optional. The file must be built for glibc 2.35 or older. Without it the release has no
  shaders (the menu greys them out). The workspace's own build needs glibc 2.38, so it cannot be used.
- The script refuses any release part that needs a glibc newer than 2.35.
- Out: `<out>/release/` holds the AppImage, the tar.gz and `SHA256SUMS.txt`.

## The player's first start

The game's folder (`WR_ROOT`) is the AppImage's folder. It holds:

- the AppImage (the only file on top)
- `game/`, `saves/`, `data/`, `media/`
- the built program, `data/gta2_recomp`, with `libSDL3.so.0` beside it

The steps (data/launcher.log names each one with its time):

1. Unpack the payload into `data/tools` and the compiler (SHA-256 checked).
2. `setup_build.py extract` unpacks the installer into `game/`. Exit 3 means it is not the freeware installer: the
   page asks again.
3. Lift gta2 and bink to C in `data/build/work`.
4. Compile in parallel. There are at most as many jobs as cores, and one per 2.5 GB of RAM. `GTA2_LAUNCHER_JOBS`
   caps it further.
5. Link, then copy `libSDL3.so.0` into data/.
6. Copy the translations to `<root>/GTA2-translations.pak`, and write the stamp `data/build/stamp.txt` (the release
   plus the installer's SHA-256).
7. Start the game with `WR_ROOT=<root>`.

The lifted C, the objects and the unpacked compiler are removed afterwards.

Later starts:

- The game starts at once.
- After a new release, the game is rebuilt from `game/` and no installer is needed.
- `--repair` builds again (from the installer when it is found).
- `--setup` shows the setup page.

Non-interactive (tests, RecompLauncher):

    RECOMP_NO_DIALOGS=1 SDL_VIDEO_DRIVER=offscreen ./GTA2-x86_64.AppImage --installer <GTA2.exe> \
        [--file <GTA2-translations.pak>] --go --no-start

`GTA2_LAUNCHER_SHOT=<file.bmp>` saves the setup page as a picture and then quits. Use it with
`SDL_VIDEO_DRIVER=offscreen` to check the layout without a window.
