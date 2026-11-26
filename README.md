<p align="center">
  <img src="assets/icon_256.png" alt="Grand Theft Auto 2 Recompiled" width="160">
</p>

# Grand Theft Auto 2 Recompiled

A native PC port of *Grand Theft Auto 2* (1999, the PC version) made by **static recompilation**: the game's own
x86 code is translated to C and compiled for your machine, with a Vulkan renderer, SDL3 for input and sound, and
no emulator, no Windows layer and no original DLLs. Windows and Linux, and an
[Android version](https://github.com/almost-drowned-rat/Grand-Theft-Auto-2-Recompiled-Android).

<a href="https://recomplauncher.com"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

> This project contains **no game data and no game code**. You need GTA 2 yourself: the **2004 Rockstar freeware
> release** (`GTA2.exe`, the installer, about 360 MB) or a GTA 2 you installed from it. The game is translated and
> compiled **on your own PC** from your copy at the first start.

> **AI disclosure:** AI assistance yes (Claude Code by Anthropic).

## Download and play

Download the latest release from [Releases](https://github.com/almost-drowned-rat/Grand-Theft-Auto-2-Recompiled/releases).

- **Windows:** unpack the zip into an empty folder and start the launcher in it.
- **Linux:** `GTA2-x86_64.AppImage` (make it executable and start it), or `GTA2-linux-x86_64.tar.gz` if you cannot
  use AppImages (no FUSE). Needs glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36 or later) and a Vulkan 1.1
  driver.

At the first start the launcher asks for your **GTA 2 installer (`GTA2.exe`)** - or a folder with an **installed
GTA 2** - and, optionally, the **translations pack** (`GTA2-translations.pak`, its own download in the release). It
unpacks the game files, translates `gta2.exe` and the movie decoder to C and compiles them; this takes a few
minutes, once. Later starts go straight into the game. Your installer or folder is only read, never changed.

Everything stays in the game's folder: `game/` (the game's files), `saves/` (your saves and settings), `data/`
(logs, caches - safe to delete), `media/` (your own music). Nothing is written anywhere else.

## Features

- **RECOMP OPTIONS** in the game's own options menu (no separate settings program):
  - **Rendering:** display mode (borderless / fullscreen / windowed), render resolution, texture filtering, frame
    rate, aspect ratio 4:3 / 16:9 / 16:10 / 21:9 (real widescreen: you see more of the city, nothing is stretched;
    menus stay 4:3)
  - **Frame rate:** 30 (original), 60 or 120 interpolated - the game logic keeps its original 30 ticks per second,
    the frames in between are drawn from it, so the game never runs faster
  - **Performance:** V-Sync, FPS counter, frame time, game speed
  - **Shaders:** RetroArch slang shader presets (librashader) - put a shader pack into `media/shaders/`
  - **Gameplay:** the settings of the old GTA 2 Manager (pedestrians, traffic, sprint, more blood)
  - **Cheats:** the game's own cheats as switches
- **Controls:** any SDL3 gamepad with hot-plug and rumble (explosions), a classic and a modern pad layout plus your
  own mapping, keyboard only or keyboard + mouse; an illustrated controller layout and a control overview
- **MIXTAPE**, a radio station of your own: put your music (MP3, FLAC, Ogg Vorbis, WAV) into album folders under
  `media/`; it plays on like a radio station after the game's stations. All albums or one, in order or shuffled.
- **Languages:** English and Spanish come with the game. The translations pack adds **German, French, Italian, Polish,
  Russian, Japanese and Chinese** (all game texts; the port's own menus stay English, radio station names stay as
  they are). Pick the language in the game's options.
- Crash-safe saves, a detailed log in `data/gta2.log` (the previous run in `gta2.previous.log`)

## Supported version

| Release | File | `gta2.exe` SHA-1 |
|---|---|---|
| Rockstar freeware release (2004, build 2004-04-27) | `GTA2.exe` installer, about 360 MB | `646e1d479fa2aea12b3ff448fa23f6362f9e012f` |

## Building from source

The port: `src/` (game-specific code), the toolkit [winrecomp](https://github.com/almost-drowned-rat/winrecomp)
(runtime, Win32 layer, lifter) beside it as `../winrecomp`. Linux:

```sh
python3 tools/setup_build.py extract --installer GTA2.exe --unshield "$(command -v unshield)" --work work --game run/game
python3 tools/setup_build.py lift --game run/game --toolkit ../winrecomp --config config --work work
# or tools/lift.sh / tools/lift.sh bink with the toolkit's Python venv (capstone, pefile)
cmake -S . -B build -G Ninja && ninja -C build
WR_ROOT=run build/gta2_recomp
```

The release packaging: `docs/linux_release.md`. Generated C, game files and builds are never committed.

## Thanks

- Rockstar Games for releasing GTA 2 as freeware
- [SDL](https://libsdl.org), [librashader](https://github.com/SnowflakePowered/librashader),
  [capstone](https://www.capstone-engine.org), [pefile](https://github.com/erocarrera/pefile),
  [unshield](https://github.com/twogood/unshield), dr_libs, stb_vorbis, Noto fonts

Grand Theft Auto and GTA are trademarks of Take-Two Interactive Software. This project is not affiliated with or
endorsed by Rockstar Games or Take-Two Interactive.
