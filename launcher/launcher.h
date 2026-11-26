/*
 * launcher.h -- GTA 2 Recompiled's Linux launcher: the one program a player downloads (in the AppImage, or the
 * tar.gz's gta2_launcher). It carries no game code and no game data: the port and the runtime prebuilt (static
 * libraries), the lifter (the winrecomp toolkit's Python with an embedded Python, capstone and pefile), unshield,
 * the compiler and the licences - as a zip appended to the program (the payload) - and builds the game on the
 * player's machine at the first start:
 *
 *   his GTA 2 freeware installer (GTA2.exe, beside the AppImage, the last one used, or picked) -> unpack the payload
 *   -> unpack the shipped compiler (SHA-256) -> unpack the game's files from the installer into game/
 *   (tools/setup_build.py extract: exit 3 = not that installer) -> lift gta2.exe and binkw32.dll to C -> compile ->
 *   link -> start the game (WR_ROOT = the game's folder).
 *
 * Nothing is downloaded and the installer (or an installed GTA 2 folder) is only read, once: after the first start
 * nothing refers to it - game/ holds real copies of every file the game and a rebuild need (the maintainer's rule).
 * Later starts start the game at once (data/build/stamp.txt: the release + game/gta2.exe's SHA-256); a new release
 * and `--repair` build again from game/ alone (sha1-checked), without the installer. What to run is data in the payload (lift.txt, build.txt, toolchain.txt, written by
 * tools/linux/release_linux.sh), so this file knows only the game's names (GtaLauncherConfig).
 *
 * From Zone of the Enders Recompiled's engine/launcher/launcher.h (ps2recomp-option-p, ad3b491, the same author's),
 * adapted for GTA 2. GPL-3.0-or-later (launcher/LICENSE). SDL3 only (processes, dialogs, files).
 */
#ifndef GTA_LAUNCHER_H
#define GTA_LAUNCHER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *game;                   /* "GTA 2": the name in the messages */
    const char *input;                  /* "GTA 2 freeware installer (GTA2.exe, the 2004 Rockstar release)" */
    const char *input_label;            /* the setup page's first line: "GTA 2 installer:" */
    const char *input_filter_name;      /* its file dialog's filter: "GTA 2 installer (GTA2.exe)" */
    const char *input_filter;           /* "exe" */
    const char *input_file;             /* the installer's usual name, looked for beside the AppImage: "GTA2.exe" */
    const char *wrong_input;            /* setup_build.py's exit 3 ("That file is not ...") */
    const char *window_title;           /* "Grand Theft Auto 2 Recompiled - Setup" */
    const char *game_exe;               /* "gta2_recomp": built in data/ (only the launcher on top) */
    const void *icon_bmp;               /* the window's icon (a BMP), or NULL */
    size_t      icon_bmp_size;
    /* An optional file of the port's own on the setup page ("Translations (optional):", GTA2-translations.pak), or
     * NULL: its label, the file dialog's filter, and the name it is copied to in the game's folder (the game unpacks
     * it itself). */
    const char *extra_file_label;
    const char *extra_file_filter_name;
    const char *extra_file_filter;
    const char *extra_file_name;
} GtaLauncherConfig;

/* The whole launcher; returns the process exit code. */
int gta_launcher_main(const GtaLauncherConfig *cfg, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif
