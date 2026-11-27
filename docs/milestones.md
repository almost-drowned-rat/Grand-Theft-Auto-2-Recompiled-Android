# GTA 2 Recompiled — milestones

Targets: Windows, Linux, Android. Renderer: Vulkan 1.1 baseline. Toolkit: winrecomp
(own toolkit for Win32 PE games; x86 lifter taken from xboxrecomp option-c; no Ghidra).

## M0 — Groundwork (DONE 2026-11-25)
- [x] Unpack the installer (InstallShield overlay -> data1.hdr/data2.cab -> unshield)
- [x] Analyse gta2.exe: imports, renderer DLLs d3ddll.dll (gbh_*) + Dmavideo.dll (Vid_*)
- [x] Create the winrecomp toolkit: lifter from option-c, Win32 PE front end
- [x] Full lift: 25,390 functions, 0 failed, 32 unresolved

## M1 — Clean lift
- [x] Clear out data decoded as code (arpl/iretd/outsb hits), write gta2 game.toml
- [x] Resolve the 32 unresolved indirect targets, set up icall feedback
- [x] Mark jump tables / switch tables, check SEH/CRT parts (startup code, _initterm table at .data 0x57c00c)
- [x] Gen into gta2_recomp/gen, stable partitions

## M2 — Runtime core (winrecomp/runtime)
- Note: the CMake build is Linux-tested; Windows (M7) and NDK (M8) builds come with those milestones
- [x] Guest memory: map the PE image at 0x400000 (.text/.rdata/.data/.bss), heap, stack
- [x] Import binding: IAT slots -> native handlers (216 imports, DLL!Func table)
- [x] Dispatcher for indirect calls / callbacks (WndProc, threads, timers)
- [ ] CMake build of the generated C (GCC/Clang on Linux, MSVC/clang-cl on Windows, NDK)
- [x] First start up to WinMain, log + crash report with guest address

## M3 — Win32 API layer (on SDL3)
- [x] KERNEL32: files (paths, case-insensitive), memory, threads/sync, time, ini/registry
- [x] USER32/GDI32: window, message loop, WndProc, cursor, MessageBox
- [x] ADVAPI32 (registry -> ini), SHELL32, ole32, VERSION, WINMM (timeGetTime, timers)
- [x] Settings that gta2 manager.exe writes -> our own settings menu (RECOMP OPTIONS, 2026-11-26)

## M4 — Renderer Vulkan 1.1
- Status 2026-11-26: main menu + in-game render correctly (HUD, lighting, sprites); original runs at 30 FPS
- [x] Native replacement for Dmavideo.dll (Vid_*: mode, flip, surfaces, gamma)
- [x] Native replacement for d3ddll.dll (gbh_*: textures, palettes, polygons, lights, DrawLine)
- [x] Vulkan 1.1 backend (swapchain, pipelines, texture cache), no 1.2+ dependency
- [x] Offscreen game image at RENDER RESOLUTION, final pass to the swapchain (ASPECT RATIO 4:3/16:9/16:10/21:9 as option-c/p: real wider view of the city, never stretched; frontend pillarboxed)
- [x] Slang shaders (RetroArch) via librashader (built from source, Vulkan only); shader packs + menu with M6
- [x] Real widescreen 16:9/16:10/21:9 (Hor+, all cameras, HUD anchored; 4:3 byte-identical to the original); verify pager/mission texts + fast-car zoom-out in M6
- [x] No busy-wait: menu + game loop sleep to their deadline (100 % -> ~3 % of a core), timing exact (30.3 Hz)
- [x] V-Sync (WR_VSYNC; menu line with M6)
- [x] FRAME RATE 60/120: moved behind M6 (own item below) - logic is frame-locked at 30 Hz, needs interpolated extra frames
- [ ] Android: landscape, preTransform identity

## M4b — FRAME RATE 60/120 (DONE 2026-11-26: maintainer "funktioniert tadellos"; FRAME RATE line in RENDERING before ASPECT RATIO)
- [x] Menu labels make the interpolation visible (maintainer 2026-11-26): FRAME RATE = 30 / 60 INTERPOLATED / 120 INTERPOLATED (first line of PERFORMANCE, shown once 60/120 work)
- [x] Keep the 30 Hz logic tick; render interpolated frames in between (camera + object positions/angles lerped, restored after the draw)
- [x] Make the render path re-runnable within a tick (sprite lists are cleared after each draw, 0x004C4D6E)
- [x] Per-tick animations / HUD stay per tick; FRAME RATE line in the PERFORMANCE sub-menu

## M4c — Rumble + custom soundtracks (DONE 2026-11-26)
- [x] Controller rumble: explosions make the controller rumble (their sounds: 61182 / 127022 bytes, strength by volume; CONTROLS: RUMBLE; maintainer: "funktioniert alles", 2026-11-26)
- [x] Custom soundtracks: radio station MIXTAPE after the last station (own albums from media/, plays on like a radio
      station, silent in the pause), SOUND > CUSTOM SOUNDTRACK: SOURCE (ALL ALBUMS / album) + PLAYBACK MODE (PLAY IN
      ORDER / SHUFFLE); station info label for every station (6 Funami FM / 7 Lithium FM heard; 8..11 order still
      to be heard in Residential / Industrial) - maintainer OK 2026-11-26

## M5 — Audio, video, input
- [x] Miles (mss32): samples, streams, 3D sound, MSSDS3DH -> SDL3 audio (native libs/miles.c, 2026-11-26)
- [x] Bink (binkw32): intros/movies (lifted binkw32, 32-bit surface, cooperative thread, 2026-11-26)
- [x] DirectInput 8: keyboard, gamepad (SDL3), controller layout (CLASSIC/MODERN/CUSTOM pad, CLASSIC/MODERN/KEYBOARD + MOUSE keys, CONTROLLER LAYOUT + CONTROL OVERVIEW pages, 2026-11-26)
- [ ] Android: touch controls

## M6 — Playable on Linux (DONE 2026-11-26: maintainer test "sieht alles gut aus")
- [x] Frontend/menu, missions load, saves (POSIX-safe writes): replaced files go through <name>.tmp + fsync + rename (+ folder fsync) in winrecomp kernel32 CloseHandle; checked 2026-11-26 (plyslot0.dat rewritten, no .tmp left)
- [x] RECOMP OPTIONS and CHEATS lines in the game's own options menu (CHEATS: the game's cheats as ON/OFF switches, one-shot cheats as actions; maintainer 2026-11-26)
- ~~Play through all three districts~~ dropped (maintainer 2026-11-26: too long; crashes are fixed as they show up)
- [x] Performance: constant 60 FPS, game logic speed correct (M4b: 61 / 122 frames per s at 60 / 120, ticks 33.0 ms; maintainer: "funktioniert tadellos", 2026-11-26)

## M7 — Windows build (DONE 2026-11-26, v0.7.0 Windows)
- [x] llvm-mingw clang build (runtime: Vulkan SDK headers, Threads, `wr_sleep_ns` high-resolution waits, Windows
      crash report), Win95 launcher (launcher/game_setup_ui_win95.c), the in-game RECOMP OPTIONS as on Linux
- [x] Release: tools/windows/release_windows.ps1 (docs/windows_release.md), GTA2-windows-x86_64.zip
- [x] Test on Windows 11 (RTX 3050 Ti): first start from GTA2.exe in ~3 min, menu + in-game at 30 and 60 fps
- [ ] Hang watchdog on Windows (POSIX only so far)

## M8 — Android build (only after M6 is finished and everything is tested; maintainer 2026-11-26)
- [ ] APK (ARM64), Vista launcher (Android standard), game data import
- [ ] Back menu in the fixed order, graphics driver (Turnip) row
- [ ] Test on the Odin 2

## After M8 (maintainer 2026-11-26; rumble + soundtracks moved to M4c)
- [x] Translations DONE 2026-11-26 (all game texts; our own menus stay English, maintainer): DE, FR, IT on the game
      font; PL, RU, JA, ZH on our own glyphs (src/gta2_text.c, Noto Sans / Noto Sans CJK Bold cut per language,
      OFL); GTA2-translations.pak (data/<letter>.gxt + data/fonts/<letter>.otf), letters g f i p r n c; tools gxt.py,
      tr_check.py (0 problems for every language), make_font.py, make_translations.sh. German checked in game by the
      maintainer; the others load clean and were looked at in screenshots (RU, JA, ZH). Radio station names
      1..11 all named (8..11 heard with GTA2_RADIO_TEST).
- [x] (was:) Own milestone (source texts: the game's e.gxt / bob_e.gxt, English, with s.gxt as a cross-check - the freeware edition ships only EN + ES; maintainer 2026-11-26: "das reicht"): complete translation of ALL story content and UI (radio station names and their music lines stay English everywhere; maintainer 2026-11-26) (game texts + our menus/launcher) into EN, FR, DE, IT, ES, JA, ZH, RU, PL; own text renderer + font (Latin-Ext, Cyrillic, CJK); shipped as a translations pak next to the launcher (like ZOE-subtitles.pak); LANGUAGE in the RECOMP menu

## v0.8.0 — Minimap, controls, camera, mods (maintainer 2026-11-27, from issues #1-#3)
- [x] MINIMAP: own radar from the level's .gmp/.sty, blips per category, POLICE, PAUSE MAP with legend, NAVIGATION
      ARROWS, free position (docs/re/minimap.md)
- [x] ENHANCEMENTS (was GAMEPLAY): LANGUAGE, MINIMAP, MANUAL CAMERA, PEDESTRIANS, TRAFFIC, SPRINT, MORE BLOOD
- [x] MANUAL CAMERA (off by default): CAMERA / ZOOM IN / ZOOM OUT buttons (docs/re/camera_distance.md)
- [x] CUSTOM MAPPING: MODERN by function (on foot / in a car), groups with headings, PROFILE STOCK / 1 / 2 / 3
- [x] mods/: one folder per mod read before the game's files (winrecomp wr_path_add_mod_dirs)
- [ ] Texture packs in mods/textures (postponed by the maintainer)
- [ ] Ray tracing: own Vulkan path, all RT GPUs (decided 2026-11-27, start after the maintainer's go)

## M9 — Multiplayer (optional)
- [ ] DirectPlay (DPLAYX) -> own network layer (LAN/IP)

## M10 — Release
- [ ] Repos (main + -Android), README, AI note, .recomp.json, recomplauncher.json
- [ ] Linux AppImage (Win2000 launcher), Windows zip, Android APK
- [ ] Release notes per version, link to the Android version
