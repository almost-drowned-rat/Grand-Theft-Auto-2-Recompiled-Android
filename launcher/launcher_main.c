/* launcher_main.c -- GTA 2 Recompiled's launcher: its names. From Zone of the Enders Recompiled's
 * engine/launcher/launcher_main.c (ps2recomp-option-p, ad3b491). GPL-3.0-or-later (launcher/LICENSE). */

#include "launcher.h"

#include <SDL3/SDL_main.h>

#ifdef GTA2_LAUNCHER_ICON_INC
static const unsigned char k_icon[] = {     /* -DGTA2_LAUNCHER_ICON=<a BMP>: the setup window's icon */
#include "launcher_icon.inc"
};
#define ICON_DATA k_icon, sizeof k_icon
#else
#define ICON_DATA NULL, 0
#endif

int main(int argc, char **argv)
{
    static const GtaLauncherConfig cfg = {
        "GTA 2",
        "GTA 2 freeware installer (GTA2.exe, the 2004 Rockstar release) or a folder with it installed",
        "GTA 2 installer or folder:",
        "GTA 2 installer (GTA2.exe)",
        "exe",
        "GTA2.exe",
        "That is neither the GTA 2 freeware installer (GTA2.exe, the 2004 Rockstar release) nor a folder with that "
        "GTA 2 installed.",
        "Grand Theft Auto 2 Recompiled - Setup",
#ifdef _WIN32
        "gta2_recomp.exe",                  /* data\gta2_recomp.exe (the zip's top: only gta2_launcher.exe) */
#else
        "gta2_recomp",
#endif
        ICON_DATA,
        "Translations (optional):",
        "Translations (GTA2-translations.pak)",
        "pak",
        "GTA2-translations.pak",
    };
    return gta_launcher_main(&cfg, argc, argv);
}
