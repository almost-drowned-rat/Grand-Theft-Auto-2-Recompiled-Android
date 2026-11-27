/*
 * GTA 2 Recompiled - entry point.
 *
 * Portable layout next to the executable (WR_ROOT overrides it for dev
 * runs): game/ holds the installed game (gta2.exe and data/), saves/ the
 * player's data and the registry, data/ the log.
 */
#include "wr.h"
#include "gta2_options.h"
#include "gta2_rumble.h"
#include "gta2_mixtape.h"
#include "gta2_radar.h"
#include "gta2_git.h"
#include "gta2_translations.h"
#include "gta2_text.h"   /* GTA2_GIT, written by the build */
#include "shaderpack/shader_pack.h"
#include "platform/wr_platform.h"
#include "render/gta2_render.h"

#include <SDL3/SDL.h>
#if defined(__ANDROID__)
#include <SDL3/SDL_main.h>   /* main -> SDL_main, called by SDLActivity */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void wr_register_miles(void);
wr_coop_step_fn gta2_thread_hook(X86Ctx *ctx, uint32_t start, uint32_t param);
recomp_func_t gta2_recomp_lookup(uint32_t va);
int gta2_dispatch_init(void);
recomp_func_t bink_recomp_lookup(uint32_t va);
int bink_dispatch_init(void);
extern const wr_export g_bink_overrides[];
void gta2_registry_defaults(void);
void gta2_manual_register(void);
void gta2_input_register(void);
void w32_exit(uint32_t code);

/* gta2.exe of the Rockstar freeware release (build 2004-04-27) */
static const char k_sha1[] = "646e1d479fa2aea12b3ff448fa23f6362f9e012f";

int main(int argc, char **argv)
{
    char root[1024];
    const char *env = getenv("WR_ROOT");
    if (env && *env) {
        snprintf(root, sizeof root, "%s", env);
    } else {
#if defined(__ANDROID__)
        /* the app's shared-storage folder: Android/data/<app id>/files (game/, saves/, data/, media/) */
        const char *b = SDL_GetAndroidExternalStoragePath();
#else
        const char *b = SDL_GetBasePath();
#endif
        snprintf(root, sizeof root, "%s", b ? b : ".");
    }
    size_t l = strlen(root);
    while (l > 1 && (root[l - 1] == '/' || root[l - 1] == '\\'))
        root[--l] = 0;
#if !defined(__ANDROID__)
    if (!(env && *env)) {
        /* the release layout (the launcher on top, the game in data/): started directly from data/, the game's
         * folder is the one above it - never data/data, data/saves */
        char *slash = strrchr(root, '/'), *bs = strrchr(root, '\\');
        if (bs > slash)
            slash = bs;
        if (slash && slash > root && SDL_strcasecmp(slash + 1, "data") == 0) {
            *slash = 0;
            l = strlen(root);
        }
    }
#endif
#if defined(__ANDROID__)
    {   /* measurements on a device (no shell env there): <folder>/dev_env.txt, KEY=VALUE per line (like the
         * other Android ports; remove it after a bench) */
        char p[1100], line[512];
        snprintf(p, sizeof p, "%s/dev_env.txt", root);
        FILE *f = fopen(p, "r");
        while (f && fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            char *eq = strchr(line, '=');
            if (eq && line[0] != '#') {
                *eq = 0;
                setenv(line, eq + 1, 1);
            }
        }
        if (f)
            fclose(f);
    }
#endif

    snprintf(g_wr_vroot, sizeof g_wr_vroot, "%s", "C:\\GTA2");
    snprintf(g_wr_exe_name, sizeof g_wr_exe_name, "%s", "gta2.exe");
    wr_paths_init(root);
    wr_log_set_name("gta2");   /* data/gta2.log, the run before: data/gta2.previous.log */
    wr_log_init();
    wr_log_header("GTA 2 Recompiled", "build " GTA2_GIT " (" __DATE__ ")");
    wr_settings_init("GTA2");   /* saves/settings.ini, GTA2_<KEY> overrides */
    {   /* <root>/mods: every folder in it is one mod, read before the game's own files and before
         * our translations (maintainer 2026-11-27); mods/textures is for texture packs */
        char mods[1100];
        snprintf(mods, sizeof mods, "%s/mods", g_wr_paths.root);
        int n = wr_path_add_mod_dirs(mods);
        wr_log("mods: %d in %s", n, mods);
    }
    gta2_translations_init();   /* GTA2-translations.pak: data\\<letter>.gxt for more languages */
    gta2_text_init();           /* p r c n: the language's own glyphs (data\\fonts\\<letter>.otf) */
    wr_crash_init();

    if (!wr_mem_init())
        return 1;
    wr_win32_register();
    wr_register_miles();
    gta2_rumble_init();
    gta2_mixtape_init();   /* MIXTAPE: the albums in media/ */
    gta2_radio_test_init();
    gta2_radar_init();     /* MINIMAP: notes the level's .gmp / .sty (after the radio test's read map) */
    wr_set_thread_hook(gta2_thread_hook);
    gta2_registry_defaults();
    opt_load_all();   /* RECOMP OPTIONS from settings.ini (sets the gameplay width) */
    {   /* the player's shader packs: <root>/shader -> data/shaders (a thread of its own); <root>/shader was
         * media/shaders up to 0.7.0 - an old one is moved over once (maintainer 2026-11-26) */
        char ms[1100], ds[1100], old[1100];
        snprintf(ms, sizeof ms, "%s/shader", g_wr_paths.root);
        snprintf(old, sizeof old, "%s/shaders", g_wr_paths.media);
        SDL_PathInfo pi;
        if (!SDL_GetPathInfo(ms, &pi) && SDL_GetPathInfo(old, &pi) && pi.type == SDL_PATHTYPE_DIRECTORY) {
            if (SDL_RenamePath(old, ms))
                wr_log("shaders: moved media/shaders to shader/");
            else
                wr_log("shaders: could not move media/shaders to shader/: %s", SDL_GetError());
        }
        SDL_CreateDirectory(ms);
        snprintf(ds, sizeof ds, "%s/shaders", g_wr_paths.data);
        shader_pack_show_bezels(1);   /* as the N64 recomps: bezel / border listed too */
        shader_pack_start(ms, ds);
    }
    gta2_manual_register();
    gta2_input_register();
    gta2_register_video();
    gta2_register_renderer();

    /* binkw32.dll first: gta2.exe's imports bind to its lifted code */
    char exe[1200], bink[1200];
    if (!wr_path_ci(g_wr_paths.game, "binkw32.dll", bink, sizeof bink) ||
        !wr_pe_load_dll(bink, "binkw32.dll", g_bink_overrides, bink_recomp_lookup, bink_dispatch_init)) {
        wr_log("binkw32.dll missing: movies are skipped");
        void wr_register_bink_absent(void);
        wr_register_bink_absent();
    }
    if (!wr_path_ci(g_wr_paths.game, "gta2.exe", exe, sizeof exe)) {
        wr_log("gta2.exe not found in %s", g_wr_paths.game);
        return 1;
    }
    if (!wr_pe_load(exe, gta2_recomp_lookup, gta2_dispatch_init))
        return 1;
    char hex[41];
    for (int i = 0; i < 20; i++)
        snprintf(hex + 2 * i, 3, "%02x", g_wr_image.sha1[i]);
    if (strcmp(hex, k_sha1))
        wr_log("WARNING: gta2.exe sha1 %s is not the freeware release this build was lifted from (%s)",
               hex, k_sha1);

    if (!wr_platform_init())
        return 1;
    int code = wr_run(argc, argv);
    w32_exit((uint32_t)code);
    return code;
}
