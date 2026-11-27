/*
 * GTA 2 Recompiled - the OPTIONS menu inside the game's own frontend
 * (docs/re/frontend.md). GTA 2's OPTIONS line started the external
 * "GTA2 Manager"; it now opens the game's unused page 12, which the port
 * fills with its own pages:
 *
 * The main menu reads PLAY / RECOMP OPTIONS / EXIT GAME (maintainer 2026-11-26).
 *
 *   RECOMP OPTIONS RENDERING / PERFORMANCE / SOUND / LANGUAGE / CONTROLS / GAMEPLAY / CHEATS
 *               (GTA 2 has no separate RECOMP OPTIONS page: the maintainer's
 *               decision 2026-11-26 - every line sits in OPTIONS)
 *   SOUND       MUSIC VOLUME, EFFECTS VOLUME (the game's own registry values)
 *   RENDERING   display mode, resolution, texture filtering, aspect
 *   PERFORMANCE V-SYNC, FPS, FRAME TIME, GAME SPEED
 *   LANGUAGE    the game's languages, RESTART NOW / LATER
 *   CHEATS      the game's cheats as ON/OFF switches + UNLOCK ALL AREAS
 *
 * One game page holds them all: the port keeps the page it shows, handles
 * every key on page 12 itself (Esc there would quit the game) and draws the
 * lines with the game's text function, fonts and highlight, over the game's
 * options picture. The game's own item list holds one blank line only, so
 * its page-entry check is satisfied.
 */
#include "gta2_menu.h"
#include "gta2_options.h"
#include "gta2_state.h"
#include "render/gta2_render.h"
#include "gta2_input.h"
#include "gta2_padview.h"
#include "gta2_mixtape.h"
#include "gta2_radar.h"
#include <ctype.h>
#include "gta2_shaders.h"
#include "gta2_gameplay.h"
#include "platform/wr_platform.h"
#include "wr.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FE_PTR        0x005EB160u   /* [ ] = the frontend object */
#define FE_PAGE       0x11Eu        /* u16 current page */
#define FE_FONT       0x11Cu        /* u16 normal menu font */
#define FE_PAGES      0x122u        /* Page[17], 0xBCA bytes each */
#define FE_EDGE       0xC9B8u       /* left, right, up, down, enter, esc, delete */
#define FE_BG         0xEDF4u       /* u8 background index */
#define FONT_HILITE   0x0067064Cu   /* u16 highlight font */
#define FE_SOUND      0x005EAF50u   /* frontend sound this frame */
#define PAGE_SIZE     0xBCAu
#define PORT_PAGE     12u

enum { SND_UP = 1, SND_DOWN = 2, SND_VALUE = 3, SND_SELECT = 5, SND_BACK = 6 };

#define FN_GOTO_PAGE  0x004587B0u   /* thiscall fe, u16 page */
#define FN_DRAW_TEXT  0x004539F0u   /* (u16 *s, Q14 x, Q14 y, u16 font, Q14 scale) */
#define FN_TEXT_W     0x004CB0C0u   /* (u16 *s, u16 font) -> pixels */
#define FN_MUSIC_VOL  0x00410600u   /* thiscall 0x5D85A0, v */
#define FN_SFX_VOL    0x004105C0u
#define AUDIO_OBJ     0x005D85A0u
#define FN_UNLOCK_ALL 0x004A8B00u   /* thiscall [0x66B404] */
#define PROFILES_PTR  0x0066B404u
#define FN_READ_CONTROLS 0x0044BF30u   /* reads Control\\0..11 again */

#define REG_GTA2 "HKEY_LOCAL_MACHINE\\SOFTWARE\\DMA Design Ltd\\GTA2"

/* ---- pages -------------------------------------------------------------------------------- */

enum { VP_ROOT, VP_SOUND, VP_DISPLAY, VP_PERF, VP_LANG, VP_CONTROLS, VP_CHEATS, VP_EXIT, VP_PAUSE, VP_QUIT, VP_RESTART,
       VP_QUITPROG, VP_MAPPING, VP_GAMEPLAY, VP_SHADERS, VP_SHBROWSE, VP_SHPARAMS, VP_OVERVIEW, VP_PADVIEW, VP_SOUNDTRACK,
       VP_MINIMAP, VP_MAP, VP_COUNT };
enum { L_PAGE, L_OPT, L_VOL, L_CHEAT, L_LANG, L_ACTION, L_BACK, L_DEVICE, L_BIND, L_SHON, L_SHPRESET, L_SHITEM, L_SHRES, L_SHPARAMS, L_SHPARAM, L_OVR, L_PADVIEW, L_MTSRC, L_MTMODE, L_PROFILE,
       L_HEAD /* a heading or an empty line: not chosen, the cursor passes over it */ };
enum { ACT_UNLOCK_AREAS = 1, ACT_RESTART_NOW, ACT_RESTART_LATER, ACT_EXIT_NO, ACT_EXIT_YES, ACT_RESUME, ACT_QUIT_YES,
       ACT_RESTART_YES, ACT_NO, ACT_QUITPROG_YES, ACT_TEST_PICTURE };
enum { VOL_MUSIC, VOL_SFX };

typedef struct { const char *label; int kind, arg; } mline;
typedef struct { const char *title; int parent; const mline *l; int n; } mpage;

/* the game's cheats (docs/re/frontend.md 9.2/9.3/10): flag bytes. The menu
 * only exists in the frontend, so every flag is set before a level starts;
 * the level-start ones (lives, respect, cash, score, jail card) are applied
 * by the game at each level start while on. */
typedef struct { const char *label; const char *key; uint32_t flag; int def; } cheat;
static const cheat g_cheats[] = {
    { "INVULNERABLE",        "cheat_invulnerable", 0x005EAD71u },
    { "ALL WEAPONS",         "cheat_all_weapons", 0x005EAD74u },
    { "KEEP WEAPONS",        "cheat_keep_weapons", 0x005EAD9Eu },
    { "INFINITE LIVES",      "cheat_infinite_lives", 0x005EAD61u },
    { "99 LIVES",            "cheat_99_lives", 0x005EAD8Fu },
    { "NO POLICE",           "cheat_no_police", 0x005EAD5Eu },
    { "MAX RESPECT",         "cheat_max_respect", 0x005EAD8Au },
    { "200000 CASH",         "cheat_cash", 0x005EAD97u },
    { "9999999 POINTS",      "cheat_points", 0x005EADA6u },
    { "JAIL FREE CARD",      "cheat_jail_card", 0x005EAD83u },
    { "DOUBLE DAMAGE",       "cheat_double_damage", 0x005EAD65u },
    { "INVISIBILITY",        "cheat_invisibility", 0x005EAD8Eu },
    { "FREE SHOPPING",       "cheat_free_shopping", 0x005EAD84u },
    { "ENDLESS ELECTROGUN",  "cheat_electrogun", 0x005EAD70u },
    { "ENDLESS FLAMETHROWER", "cheat_flamethrower", 0x005EAD9Bu },
    { "BLOOD",               "cheat_blood", 0x005EAD51u, 1 },   /* blood pools under the dead: on by default */
    { "ELVIS PEDS",          "cheat_elvis", 0x005EAD9Fu },
    { "NAKED PEDS",          "cheat_naked", 0x005EADA0u },
};
#define N_CHEATS ((int)(sizeof g_cheats / sizeof g_cheats[0]))

/* Android: no DISPLAY MODE (always full screen; the maintainer's rule for every Android port); SHADERS there too
 * since 2026-11-26 (the APK carries librashader, packs come in through BACK > Settings > Shaders) */
static const mline l_root[] = {
    { "RENDERING", L_PAGE, VP_DISPLAY }, { "SHADERS", L_PAGE, VP_SHADERS },
    { "PERFORMANCE", L_PAGE, VP_PERF }, { "SOUND", L_PAGE, VP_SOUND },
    { "CONTROLS", L_PAGE, VP_CONTROLS }, { "ENHANCEMENTS", L_PAGE, VP_GAMEPLAY },
    { "CHEATS", L_PAGE, VP_CHEATS },
};
static const mline l_sound[] = {
    { "MUSIC VOLUME", L_VOL, VOL_MUSIC }, { "EFFECTS VOLUME", L_VOL, VOL_SFX },
    { "CUSTOM SOUNDTRACK", L_PAGE, VP_SOUNDTRACK },
};
/* the MIXTAPE radio station's music (maintainer 2026-11-26) */
static const mline l_soundtrack[] = { { "SOURCE", L_MTSRC, 0 }, { "PLAYBACK MODE", L_MTMODE, 0 } };
static const mline l_display[] = {
#ifndef __ANDROID__
    { NULL, L_OPT, OPT_DISPLAY_MODE },
#endif
    { NULL, L_OPT, OPT_RENDER_RES }, { NULL, L_OPT, OPT_TEXTURE_FILTER },
    { NULL, L_OPT, OPT_FRAME_RATE }, { NULL, L_OPT, OPT_ASPECT },   /* FRAME RATE before ASPECT RATIO (maintainer 2026-11-26) */
};
static const mline l_perf[] = {
    { NULL, L_OPT, OPT_VSYNC }, { NULL, L_OPT, OPT_PERF_FPS }, { NULL, L_OPT, OPT_PERF_FRAMETIME },
    { NULL, L_OPT, OPT_PERF_SPEED },
};
static mline l_cheats[N_CHEATS + 1];
static const mline l_controls[] = { { NULL, L_OPT, OPT_CONTROL_SCHEME }, { NULL, L_OPT, OPT_KEY_SCHEME },
                                    { NULL, L_OPT, OPT_RUMBLE },
                                    { "CUSTOM MAPPING", L_PAGE, VP_MAPPING },
                                    { "CONTROLLER LAYOUT", L_PAGE, VP_PADVIEW },
                                    { "CONTROL OVERVIEW", L_PAGE, VP_OVERVIEW } };
static const mline l_padview[] = { { "VIEW", L_PADVIEW, 0 } };
static int g_pv_car;   /* CONTROLLER LAYOUT shows IN A CAR */
static mline l_overview[32];
/* CUSTOM MAPPING (as option-p): DEVICE on top, then every function and its
 * button / key; Enter waits for the new one (5 s, keyboard Esc cancels) */
static mline l_mapping[48];   /* DEVICE, PROFILE, an empty line, the groups (gta2_bind_row) */
/* SHADERS (RetroArch slang; gta2_shaders.c) and its preset browser */
/* the N64 recomps' layout: Shader / Shader selection > / Shader resolution / Shader parameters > */
/* + TAKE TEST PICTURE in the pause menu only (the game's picture behind it; maintainer 2026-11-26: the test picture
 * comes from the player's own game, none is shipped) */
static const mline l_shaders[] = { { "SHADER", L_SHON, 0 }, { "SHADER SELECTION", L_SHPRESET, 0 },
                                   { "SHADER RESOLUTION", L_SHRES, 0 }, { "SHADER PARAMETERS", L_SHPARAMS, 0 },
                                   { "TAKE TEST PICTURE", L_ACTION, ACT_TEST_PICTURE } };
int g_gta2_test_grab;
static mline l_shparams[128];
static mline l_browse[1024];
static char g_browse_title[64] = "SHADER PACKS";
static const gta2_shader_item *g_browse_items;
/* ENHANCEMENTS (was GAMEPLAY, renamed by the maintainer 2026-11-27; VP_GAMEPLAY keeps its name in the code) */
/* Its order (maintainer 2026-11-27): LANGUAGE, MINIMAP, MANUAL CAMERA, then the rest; no CAMERA DISTANCE line
 * (the camera buttons set it). LANGUAGE has no sub-page; leaving the page with a language other than the one
 * the game started with opens the pop-up RESTART? (RESTART NOW / RESTART LATER) - every time */
static const mline l_gameplay[] = { { "LANGUAGE", L_LANG, 0 }, { "MINIMAP", L_PAGE, VP_MINIMAP },
                                    { NULL, L_OPT, OPT_MANUAL_CAMERA },
                                    { NULL, L_OPT, OPT_PEDESTRIANS }, { NULL, L_OPT, OPT_TRAFFIC },
                                    { NULL, L_OPT, OPT_SPRINT }, { NULL, L_OPT, OPT_MORE_BLOOD } };
/* GAMEPLAY > MINIMAP (maintainer 2026-11-27): the radar's look, then an ON/OFF switch per element */
static const mline l_minimap[] = {
    { NULL, L_OPT, OPT_MINIMAP }, { NULL, L_OPT, OPT_MINIMAP_SIZE }, { NULL, L_OPT, OPT_MINIMAP_ZOOM },
    { NULL, L_OPT, OPT_MINIMAP_X }, { NULL, L_OPT, OPT_MINIMAP_Y },
    { NULL, L_OPT, OPT_PAUSE_MAP }, { NULL, L_OPT, OPT_NAV_ARROWS },
    { NULL, L_OPT, OPT_BLIP_MISSIONS }, { NULL, L_OPT, OPT_BLIP_PLACES }, { NULL, L_OPT, OPT_BLIP_TOKENS },
    { NULL, L_OPT, OPT_BLIP_WEAPONS }, { NULL, L_OPT, OPT_BLIP_BONUSES }, { NULL, L_OPT, OPT_BLIP_FRENZY },
    { NULL, L_OPT, OPT_BLIP_POLICE },
};
static const mline l_lang_restart[] = { { "RESTART NOW", L_ACTION, ACT_RESTART_NOW },
                                        { "RESTART LATER", L_ACTION, ACT_RESTART_LATER } };
static int g_map_keyboard;          /* DEVICE: 0 controller, 1 keyboard */
static int g_wait = -1;             /* function waiting for a button / key */
static uint32_t g_wait_t0;
static uint64_t g_wait_pad_base;    /* held when the wait began */
static uint8_t g_wait_key_base[256];
static int g_wait_hold;   /* after a pad binding: ignore keys until released */
/* EXIT GAME? - B / Esc on the main menu and EXIT GAME ask first (maintainer
 * 2026-11-26; the game quit at once, through the credits) */
static const mline l_exit[] = { { "NO", L_ACTION, ACT_EXIT_NO }, { "YES", L_ACTION, ACT_EXIT_YES } };
/* the pause menu over the game (START / Esc in play; maintainer 2026-11-26) */
static const mline l_pause[] = {
    { "RESUME", L_ACTION, ACT_RESUME }, { "RECOMP OPTIONS", L_PAGE, VP_ROOT },
    { "RESTART AREA", L_PAGE, VP_RESTART }, { "EXIT GAME", L_PAGE, VP_QUITPROG },
    { "EXIT TO MAIN MENU", L_PAGE, VP_QUIT },
};
/* ... with MINIMAP > PAUSE MAP on: MAP under RESUME, the whole district (maintainer 2026-11-27) */
static const mline l_pause_map[] = {
    { "RESUME", L_ACTION, ACT_RESUME }, { "MAP", L_PAGE, VP_MAP }, { "RECOMP OPTIONS", L_PAGE, VP_ROOT },
    { "RESTART AREA", L_PAGE, VP_RESTART }, { "EXIT GAME", L_PAGE, VP_QUITPROG },
    { "EXIT TO MAIN MENU", L_PAGE, VP_QUIT },
};
static const mline l_map[] = { { "BACK", L_BACK, 0 } };
static const mline l_quitprog[] = { { "NO", L_ACTION, ACT_NO }, { "YES", L_ACTION, ACT_QUITPROG_YES } };
static const mline l_quit[] = { { "NO", L_ACTION, ACT_NO }, { "YES", L_ACTION, ACT_QUIT_YES } };
static const mline l_restart[] = { { "NO", L_ACTION, ACT_NO }, { "YES", L_ACTION, ACT_RESTART_YES } };

static mpage g_pages[VP_COUNT] = {
    [VP_ROOT]    = { "RECOMP OPTIONS", -1, l_root, (int)(sizeof l_root / sizeof l_root[0]) },
    [VP_SOUND]   = { "SOUND", VP_ROOT, l_sound, 3 },
    [VP_SOUNDTRACK] = { "CUSTOM SOUNDTRACK", VP_SOUND, l_soundtrack, 2 },
    [VP_DISPLAY] = { "RENDERING", VP_ROOT, l_display, (int)(sizeof l_display / sizeof l_display[0]) },
    [VP_PERF]    = { "PERFORMANCE", VP_ROOT, l_perf, 4 },
    [VP_CONTROLS] = { "CONTROLS", VP_ROOT, l_controls, 6 },
    [VP_PADVIEW] = { "CONTROLLER LAYOUT", VP_CONTROLS, l_padview, 1 },
    [VP_OVERVIEW] = { "CONTROL OVERVIEW", VP_CONTROLS, l_overview, 0 },
    [VP_MAPPING] = { "CUSTOM MAPPING", VP_CONTROLS, l_mapping, 0 },
    [VP_GAMEPLAY] = { "ENHANCEMENTS", VP_ROOT, l_gameplay, (int)(sizeof l_gameplay / sizeof l_gameplay[0]) },
    [VP_MINIMAP] = { "MINIMAP", VP_GAMEPLAY, l_minimap, (int)(sizeof l_minimap / sizeof l_minimap[0]) },
    [VP_LANG]    = { "RESTART?", VP_ROOT, l_lang_restart, 2 },   /* the pop-up after GAMEPLAY */
    [VP_SHADERS] = { "SHADERS", VP_ROOT, l_shaders, 4 },
    [VP_SHPARAMS] = { "SHADER PARAMETERS", VP_SHADERS, l_shparams, 0 },
    [VP_SHBROWSE] = { g_browse_title, VP_SHADERS, l_browse, 0 },
    [VP_CHEATS]  = { "CHEATS", VP_ROOT, l_cheats, 0 },
    [VP_EXIT]    = { "EXIT GAME?", -1, l_exit, 2 },
    [VP_PAUSE]   = { "PAUSED", -1, l_pause, 5 },
    [VP_MAP]     = { "MAP", VP_PAUSE, l_map, 1 },
    [VP_QUITPROG] = { "EXIT GAME?", VP_PAUSE, l_quitprog, 2 },
    [VP_QUIT]    = { "EXIT TO MAIN MENU?", VP_PAUSE, l_quit, 2 },
    [VP_RESTART] = { "RESTART AREA?", VP_PAUSE, l_restart, 2 },
};

/* ---- state --------------------------------------------------------------------------------- */

#define VISIBLE 9   /* lines shown at once; longer pages scroll */

static int g_vp = VP_ROOT, g_cur, g_top;
static int g_cur_of[VP_COUNT];   /* the line to return to */
static uint16_t g_last_page = 0xFFFF;
static uint32_t g_strbuf;        /* guest u16 text buffer */

/* languages: the game's own gxt files (Option\language, first letter) */
/* + the translations pak's (p r c n: drawn by src/gta2_text.c); 'j' = the game's own kanji path, never offered
 * (it needs a kanji.dat no edition ships). Names in the game font's letters. */
static const char g_lang_code[] = "efgisprnc";
#define N_LANG 9
static const char *const g_lang_name[] = { "ENGLISH", "FRANCAIS", "DEUTSCH", "ITALIANO", "ESPANOL", "POLSKI",
                                           "RUSSKIJ", "NIHONGO", "ZHONGWEN" };
static int g_lang_have[N_LANG], g_lang_start = -1, g_lang_sel;

static uint32_t fe(void) { return wr_rd32(FE_PTR); }
static uint32_t port_page(uint32_t f) { return f + FE_PAGES + PORT_PAGE * PAGE_SIZE; }

static uint32_t call_this(X86Ctx *ctx, uint32_t fn, uint32_t self, int n, const uint32_t *args)
{
    uint32_t ecx = ctx->r_ecx;
    ctx->r_ecx = self;
    uint32_t r = wr_call_guest(ctx, fn, n, args);
    ctx->r_ecx = ecx;
    return r;
}

/* ---- values ------------------------------------------------------------------------------- */

static uint32_t vol_get(int which)
{
    uint32_t v = 127;
    wr_reg_get_dword(REG_GTA2 "\\Sound", which == VOL_MUSIC ? "CDVol" : "SFXVol", &v);
    return v > 127 ? 127 : v;
}

static void vol_set(X86Ctx *ctx, int which, uint32_t v)
{
    wr_reg_set_dword(REG_GTA2 "\\Sound", which == VOL_MUSIC ? "CDVol" : "SFXVol", v);
    call_this(ctx, which == VOL_MUSIC ? FN_MUSIC_VOL : FN_SFX_VOL, AUDIO_OBJ, 1, &v);
}

static void lang_init(void)
{
    if (g_lang_start >= 0)
        return;
    char host[1200], name[32], cur[16] = "e";
    for (int i = 0; i < N_LANG; i++) {
        snprintf(name, sizeof name, "data\\%c.gxt", g_lang_code[i]);
        g_lang_have[i] = wr_path_resolve_read(name, host, sizeof host);
    }
    wr_reg_get_str(REG_GTA2 "\\Option", "language", cur, sizeof cur);
    g_lang_start = 0;
    for (int i = 0; i < N_LANG; i++)
        if (g_lang_code[i] == cur[0])
            g_lang_start = i;
    g_lang_sel = g_lang_start;
}

static void lang_step(int d)
{
    for (int k = 0; k < N_LANG; k++) {
        g_lang_sel = (g_lang_sel + d + N_LANG) % N_LANG;
        if (g_lang_have[g_lang_sel])
            break;
    }
    char v[2] = { g_lang_code[g_lang_sel], 0 };
    wr_reg_set_str(REG_GTA2 "\\Option", "language", v);
}

static int cheat_on(int i) { return wr_rd8(g_cheats[i].flag) != 0; }

static void cheat_set(int i, int on)
{
    wr_wr8(g_cheats[i].flag, (uint8_t)(on != 0));
    wr_settings_set_int(g_cheats[i].key, on != 0);
}

/* the SOURCE choice: -1 = ALL ALBUMS, else the album */
static int mt_source_index(void)
{
    const char *k = wr_settings_get("mixtape_source", "all");
    for (int i = 0; i < gta2_mixtape_album_count(); i++)
        if (!strcmp(k, gta2_mixtape_album_key(i)))
            return i;
    return -1;
}

/* what the game's font draws: upper case letters, digits, space, a few marks */
static void upper_text(const char *s, char *out, size_t cap)
{
    size_t k = 0;
    for (; *s && k + 1 < cap && k < 40; s++) {
        unsigned char c = (unsigned char)*s;
        char o = c < 0x80 && (isalnum(c) || c == '.' || c == ':' || c == '-' || c == ',' || c == '\'') ? (char)toupper(c) : ' ';
        if (o == ' ' && (!k || out[k - 1] == ' '))
            continue;
        out[k++] = o;
    }
    while (k && out[k - 1] == ' ')
        k--;
    out[k] = 0;
}

/* "LABEL" + value for a line */
static void line_value(const mline *l, char *v, size_t cap)
{
    v[0] = 0;
    switch (l->kind) {
    case L_OPT: snprintf(v, cap, "%s", opt_choice_label(l->arg, opt_get(l->arg))); break;
    case L_VOL: snprintf(v, cap, "%u", (vol_get(l->arg) * 10 + 63) / 127); break;
    case L_CHEAT: snprintf(v, cap, "%s", cheat_on(l->arg) ? "ON" : "OFF"); break;
    case L_LANG: snprintf(v, cap, "%s", g_lang_name[g_lang_sel]); break;
    case L_DEVICE: snprintf(v, cap, "%s", g_map_keyboard ? "KEYBOARD" : "CONTROLLER"); break;
    case L_PROFILE:
        if (gta2_bind_profile())
            snprintf(v, cap, "%d", gta2_bind_profile());
        else
            snprintf(v, cap, "STOCK");
        break;
    case L_SHON: snprintf(v, cap, "%s", gta2_shader_on() ? "ON" : "OFF"); break;
    case L_SHPRESET: gta2_shader_preset_name(v, cap); snprintf(v + strlen(v), cap - strlen(v), " >"); break;
    case L_SHRES: snprintf(v, cap, "%s", (const char *[]){ "ORIGINAL", "2X", "FULL" }[gta2_shader_resolution()]); break;
    case L_SHPARAMS: snprintf(v, cap, ">"); break;
    case L_SHPARAM: gta2_shader_param_value(l->arg, v, cap); break;
    case L_PADVIEW: snprintf(v, cap, "%s", g_pv_car ? "< IN A CAR >" : "< ON FOOT >"); break;
    case L_MTSRC: {
        if (!gta2_mixtape_album_count()) {
            snprintf(v, cap, "NO MUSIC IN MEDIA");
            break;
        }
        int a = mt_source_index();
        upper_text(a < 0 ? "ALL ALBUMS" : gta2_mixtape_album_name(a), v, cap);
        break;
    }
    case L_MTMODE:
        snprintf(v, cap, "%s", strcmp(wr_settings_get("mixtape_mode", "order"), "shuffle") ? "IN ORDER" : "SHUFFLE");
        break;
    case L_OVR:
        if (gta2_overview_is_head(l->arg))
            snprintf(v, cap, "CONTROLLER %s   |   %s%s", opt_choice_label(OPT_CONTROL_SCHEME, opt_get(OPT_CONTROL_SCHEME)),
                     opt_get(OPT_KEY_SCHEME) == 2 ? "" : "KEYBOARD ", opt_choice_label(OPT_KEY_SCHEME, opt_get(OPT_KEY_SCHEME)));
        else
            gta2_overview_value(l->arg, v, cap);
        break;
    case L_BIND:
        if (g_wait == l->arg)
            snprintf(v, cap, "%s", g_map_keyboard ? "PRESS A KEY" : "PRESS A BUTTON");
        else
            gta2_bind_value(g_map_keyboard, l->arg, v, cap);
        break;
    default: break;
    }
}

static void build_browse_page(void)
{
    int n = gta2_shader_browse(&g_browse_items);
    if (n > (int)(sizeof l_browse / sizeof l_browse[0]))
        n = (int)(sizeof l_browse / sizeof l_browse[0]);
    for (int i = 0; i < n; i++)
        l_browse[i] = (mline){ g_browse_items[i].label, L_SHITEM, i };
    if (!n)   /* no pack yet: one line saying so (an empty page crashed - the cursor went to -1) */
        l_browse[n++] = (mline){ "NO SHADER PACKS IN THE SHADER FOLDER", L_ACTION, 0 };
    g_pages[VP_SHBROWSE].n = n;
    gta2_shader_dir_title(g_browse_title, sizeof g_browse_title);
}

/* the preset's parameters; one line saying why when there are none */
static void build_params_page(void)
{
    int n = gta2_shader_params();
    if (n > (int)(sizeof l_shparams / sizeof l_shparams[0]))
        n = (int)(sizeof l_shparams / sizeof l_shparams[0]);
    for (int i = 0; i < n; i++)
        l_shparams[i] = (mline){ NULL, L_SHPARAM, i };
    if (!n)
        l_shparams[n++] = (mline){ gta2_shader_on() ? "NO PARAMETERS OR STILL LOADING" : "NO SHADER IN USE",
                                   L_ACTION, 0 };
    g_pages[VP_SHPARAMS].n = n;
}

static void build_mapping_page(void)
{
    int n = 0;
    l_mapping[n++] = (mline){ "DEVICE", L_DEVICE, 0 };
    l_mapping[n++] = (mline){ "PROFILE", L_PROFILE, 0 };   /* STOCK, 1..3: a whole mapping each (maintainer 2026-11-27) */
    l_mapping[n++] = (mline){ "", L_HEAD, 0 };
    /* the functions in groups with headings and empty lines (maintainer 2026-11-27) */
    for (int r = 0; r < gta2_bind_rows(g_map_keyboard) && n < (int)(sizeof l_mapping / sizeof l_mapping[0]); r++) {
        int b;
        const char *head = gta2_bind_row(g_map_keyboard, r, &b);
        l_mapping[n++] = head ? (mline){ head, L_HEAD, 0 } : (mline){ NULL, L_BIND, b };
    }
    g_pages[VP_MAPPING].n = n;
}

static void build_overview_page(void)
{
    int n = gta2_overview_count();
    if (n > (int)(sizeof l_overview / sizeof l_overview[0]))
        n = (int)(sizeof l_overview / sizeof l_overview[0]);
    for (int i = 0; i < n; i++)
        l_overview[i] = (mline){ NULL, L_OVR, i };
    g_pages[VP_OVERVIEW].n = n;
}

static const char *line_label(const mline *l)
{
    if (l->kind == L_OPT)
        return opt_label(l->arg);
    if (l->kind == L_BIND)
        return gta2_bind_label(g_map_keyboard, l->arg);
    if (l->kind == L_SHPARAM)
        return gta2_shader_param_label(l->arg);
    if (l->kind == L_OVR)
        return gta2_overview_label(l->arg);
    return l->label;
}

/* ---- the game's page 12 --------------------------------------------------------------------- */

/* One blank, visible, selectable button: the game's page-entry check wants a
 * visible line; the port draws the real ones. */
static void prepare_page(uint32_t f)
{
    uint32_t p = port_page(f);
    memset(wr_ptr(p), 0, PAGE_SIZE);
    wr_wr16(p + 0, 1);                       /* one item */
    uint32_t it = p + 4;
    wr_wr8(it + 0, 1);                       /* button */
    wr_wr8(it + 1, 1);                       /* visible */
    wr_wr16(it + 2, 300);
    wr_wr16(it + 4, 210);
    wr_wr16(it + 6, ' ');
    wr_wr16(it + 0x6A, 0xFFFF);
    wr_wr16(it + 0x6C, 0xFFFF);
    wr_wr16(it + 0x80, PORT_PAGE);
    wr_wr16(p + 0xB8A, 280);                 /* slot 0 */
    wr_wr16(p + 0xB8C, 218);
    wr_wr8(p + 0xB8E, 1);
}

static void build_mapping_page(void);

static void build_cheat_page(void)
{
    if (g_pages[VP_CHEATS].n)
        return;
    int n = 0;
    for (int i = 0; i < N_CHEATS; i++)
        l_cheats[n++] = (mline){ g_cheats[i].label, L_CHEAT, i };
    l_cheats[n++] = (mline){ "UNLOCK ALL AREAS", L_ACTION, ACT_UNLOCK_AREAS };
    g_pages[VP_CHEATS].n = n;
    build_mapping_page();
    build_overview_page();
}

static void open_vpage(int vp)
{
    if (vp == VP_MAPPING)
        build_mapping_page();   /* its controller lines follow CONTROLLER (MODERN or CUSTOM) */
    g_cur_of[g_vp] = g_cur;
    g_vp = vp;
    g_cur = 0;
    g_top = 0;
}

/* an item's u16[50] text (upper-case ASCII; translations come later) */
static void set_item_text(uint32_t va, const char *t)
{
    int i = 0;
    for (; t[i] && i < 49; i++)
        wr_wr16(va + 2 * (uint32_t)i, (uint16_t)(unsigned char)t[i]);
    wr_wr16(va + 2 * (uint32_t)i, 0);
}

/* Called by the game's frontend build (every frontend construction). */
void gta2_menu_frontend_built(uint32_t f)
{
    if (!f)
        return;
    build_cheat_page();
    /* main menu OPTIONS (page 0, item 1): the Manager launch -> page 12 */
    if (wr_rd16(f + 0x228) == 0x101) {
        wr_wr16(f + 0x228, PORT_PAGE);
        set_item_text(f + 0x1AE, "RECOMP OPTIONS");   /* item 1 text */
    }
    if (wr_rd16(f + 0x2AA) == 0x102 || wr_rd16(f + 0x2AA) == 9)   /* item 2: QUIT (credits, then quit) */
        set_item_text(f + 0x230, "EXIT GAME");
    prepare_page(f);
}

/* After the game picked the background: the options picture on page 12. */
void gta2_menu_background(void)
{
    uint32_t f = fe();
    if (f && wr_rd16(f + FE_PAGE) == PORT_PAGE)
        wr_wr8(f + FE_BG, 0);
}


/* Y (pad) / Home (keyboard): every line of the page back to its default */
static void page_defaults(X86Ctx *ctx)
{
    const mpage *pg = &g_pages[g_vp];
    for (int i = 0; i < pg->n; i++) {
        const mline *l = &pg->l[i];
        switch (l->kind) {
        case L_OPT: opt_set(l->arg, opt_default(l->arg)); break;
        case L_VOL: vol_set(ctx, l->arg, 127); break;
        case L_CHEAT: if (cheat_on(l->arg) != g_cheats[l->arg].def) cheat_set(l->arg, g_cheats[l->arg].def); break;
        case L_LANG: break;   /* the language stays: Y on GAMEPLAY resets the gameplay settings only */
        case L_SHPARAM: gta2_shader_param_reset(l->arg); break;   /* Y: the preset's values */
        default: break;
        }
    }
    if (g_vp == VP_MAPPING) {   /* the device's default layout */
        gta2_bind_defaults(g_map_keyboard);
        if (g_map_keyboard)
            wr_call_guest(ctx, FN_READ_CONTROLS, 0, NULL);
    }
    wr_wr32(FE_SOUND, SND_VALUE);
}

static int g_post_cursor = -1, g_post_quit;

/* where the menu is shown: the frontend's page 12, or over the paused game */
enum { HOST_FRONTEND, HOST_PAUSE };
static int g_host = HOST_FRONTEND;
static int g_pause_open;
static int g_after_quit;   /* 1: land on the main menu, 2: restart the area (page 11 REPLAY), 3: exit */

#define FN_PAUSE      0x0045A6E0u   /* thiscall game: state 2 (single player) */
#define FN_RESUME     0x0045BA60u   /* thiscall game: state 1, flushes input */
#define FN_END_GAME   0x0045A4B0u   /* thiscall game (delay, reason 2 = quit) */

static void pause_close(X86Ctx *ctx, int resume)
{
    uint32_t g = gta2_game();
    if (resume && g && wr_rd32(g) == 2)
        call_this(ctx, FN_RESUME, g, 0, NULL);
    g_pause_open = 0;
    g_host = HOST_FRONTEND;
    g_pages[VP_SHADERS].n = 4;   /* no game picture behind the main menu */
    g_wr_input_captured = 0;
}

int gta2_pause_open(void) { return g_pause_open; }

/* After the game's own input step (it rewrites the result and the cursor). */
void gta2_menu_after_input(void)
{
    uint32_t f = fe();
    if (!f)
        return;
    if (g_post_cursor >= 0) {
        wr_wr16(f + FE_PAGES + 0xBC6, (uint16_t)g_post_cursor);
        g_post_cursor = -1;
    }
    if (g_post_quit) {
        wr_wr32(f + 0x108, 1);   /* the frontend's result: quit the program */
        g_post_quit = 0;
    }
}

static void go_back(X86Ctx *ctx, uint32_t f)
{
    int parent = g_pages[g_vp].parent;
    if (g_vp == VP_GAMEPLAY && g_lang_start >= 0 && g_lang_sel != g_lang_start) {   /* the pop-up RESTART? */
        open_vpage(VP_LANG);
        wr_wr32(FE_SOUND, SND_SELECT);
        return;
    }
    if (g_vp == VP_ROOT && g_host == HOST_PAUSE)
        parent = VP_PAUSE;
    wr_wr32(FE_SOUND, SND_BACK);
    if (parent < 0 && g_host == HOST_PAUSE) {   /* B / Esc on PAUSED: back to the game */
        pause_close(ctx, 1);
        return;
    }
    if (parent < 0) {   /* leave to the main menu, cursor on the line that led here */
        uint32_t a = 0;
        call_this(ctx, FN_GOTO_PAGE, f, 1, &a);
        g_post_cursor = g_vp == VP_EXIT ? 2 : 1;   /* set after the game's input step */
        return;
    }
    g_vp = parent;
    g_cur = g_cur_of[parent];
    g_top = g_cur >= VISIBLE ? g_cur - VISIBLE + 1 : 0;
}

static void change(X86Ctx *ctx, const mline *l, int d)
{
    switch (l->kind) {
    case L_OPT: {
        int n = opt_count(l->arg);
        opt_set(l->arg, (opt_get(l->arg) + d + n) % n);
        break;
    }
    case L_VOL: {
        int v = (int)vol_get(l->arg) + d * 13;   /* 10 steps */
        v = v < 0 ? 0 : v > 127 ? 127 : v;
        if (v > 120 && d > 0) v = 127;
        vol_set(ctx, l->arg, (uint32_t)v);
        break;
    }
    case L_CHEAT: cheat_set(l->arg, !cheat_on(l->arg)); break;
    case L_PADVIEW: g_pv_car = !g_pv_car; break;
    case L_MTSRC: {
        int n = gta2_mixtape_album_count();
        if (!n)
            break;
        int a = mt_source_index() + 1 + d;   /* 0 = ALL ALBUMS, k = album k-1 */
        a = ((a % (n + 1)) + n + 1) % (n + 1);
        wr_settings_set("mixtape_source", a ? gta2_mixtape_album_key(a - 1) : "all");
        gta2_mixtape_rebuild();
        break;
    }
    case L_MTMODE:
        wr_settings_set("mixtape_mode", strcmp(wr_settings_get("mixtape_mode", "order"), "shuffle") ? "shuffle" : "order");
        gta2_mixtape_rebuild();
        break;
    case L_LANG: lang_step(d); break;
    case L_DEVICE:
        g_map_keyboard = !g_map_keyboard;
        build_mapping_page();
        break;
    case L_PROFILE:
        gta2_bind_profile_set((gta2_bind_profile() + d + 4) % 4);   /* STOCK, 1, 2, 3 */
        wr_call_guest(ctx, FN_READ_CONTROLS, 0, NULL);   /* the keyboard's keys changed */
        break;
    case L_SHON: gta2_shader_set_on(!gta2_shader_on()); break;
    case L_SHRES: gta2_shader_set_resolution(gta2_shader_resolution() + d); break;
    case L_SHPARAM: gta2_shader_param_step(l->arg, d); break;
    default: return;
    }
    wr_wr32(FE_SOUND, d > 0 ? SND_VALUE : SND_VALUE + 1);
}

/* Waiting for the new button / key of a CUSTOM MAPPING line: 1 while it
 * waits (the menu's own keys are ignored meanwhile). */
static int poll_wait(X86Ctx *ctx)
{
    if (g_wait < 0)
        return 0;
    if (wr_time_ms() - g_wait_t0 > 5000) {   /* nothing pressed: cancel */
        g_wait = -1;
        return 1;
    }
    if (g_map_keyboard) {
        uint8_t st[256];
        wr_keys_keyboard_dik(st);
        for (int k = 1; k < 256; k++) {
            if (!(st[k] & 0x80)) {
                g_wait_key_base[k] = 0;   /* released since the wait began */
                continue;
            }
            if (g_wait_key_base[k])
                continue;
            if (k != 0x01) {   /* Esc cancels */
                gta2_bind_set_key(g_wait, (uint8_t)k);
                wr_call_guest(ctx, FN_READ_CONTROLS, 0, NULL);
            }
            g_wait = -1;
            return 1;
        }
        return 1;
    }
    uint64_t now = wr_pad_controls();
    g_wait_pad_base &= now;   /* released ones may count again */
    uint64_t fresh = now & ~g_wait_pad_base;
    if (fresh) {
        int c = 0;
        while (!(fresh >> c & 1))
            c++;
        /* MODERN keeps MODERN (its own button layout, issue #3); CLASSIC becomes CUSTOM as before */
        if (gta2_bind_set_pad(g_wait, c) && opt_get(OPT_CONTROL_SCHEME) == 0)
            opt_set(OPT_CONTROL_SCHEME, 2);
        g_wait = -1;
        g_wait_hold = 1;   /* the menu ignores keys until everything is released */
    }
    return 1;
}

/* Held arrows repeat (maintainer 2026-11-26): after 350 ms, every 90 ms,
 * after 1.5 s every 35 ms. held: the four DIK states. */
static void key_repeat(const uint8_t held[4], int *up, int *down, int *left, int *right)
{
    static uint32_t t0[4], next[4];
    int *e[4] = { up, down, left, right };
    uint32_t now = wr_time_ms();
    for (int i = 0; i < 4; i++) {
        if (!(held[i] & 0x80)) {
            t0[i] = 0;
            continue;
        }
        if (*e[i] || !t0[i]) {   /* the press itself */
            t0[i] = now;
            next[i] = now + 350;
            continue;
        }
        if ((int32_t)(now - next[i]) >= 0) {
            *e[i] = 1;
            next[i] = now + (now - t0[i] > 1500 ? 35 : 90);
        }
    }
}

/* One step of the port's menu (frontend page 12 or the pause menu). */
static void menu_keys(X86Ctx *ctx, uint32_t f, int left, int right, int up, int down, int enter, int esc,
                      int defaults)
{
    if (poll_wait(ctx))
        return;
    if (g_wait_hold) {
        if (wr_pad_controls())
            return;
        g_wait_hold = 0;
    }
    const mpage *pg = &g_pages[g_vp];
    const mline *l = &pg->l[g_cur];
    if (esc && g_vp == VP_SHBROWSE && gta2_shader_browse_up()) {   /* B: a folder up */
        build_browse_page();
        g_cur = g_top = 0;
        wr_wr32(FE_SOUND, SND_BACK);
    } else if (esc) {
        go_back(ctx, f);
    } else if (defaults) {
        page_defaults(ctx);
    } else if (up || down) {
        int tries = pg->n;
        do   /* past headings and empty lines */
            g_cur = (g_cur + (down ? 1 : pg->n - 1)) % pg->n;
        while (pg->l[g_cur].kind == L_HEAD && --tries > 0);
        wr_wr32(FE_SOUND, down ? SND_DOWN : SND_UP);
    } else if (enter) {
        if (l->kind == L_PAGE) {
            open_vpage(l->arg);
            wr_wr32(FE_SOUND, SND_SELECT);
        } else if (l->kind == L_BACK) {
            go_back(ctx, f);
        } else if (l->kind == L_ACTION && l->arg == ACT_EXIT_NO) {
            go_back(ctx, f);
        } else if (l->kind == L_ACTION && l->arg == ACT_EXIT_YES) {
            g_post_quit = 1;   /* the frontend's result, set after the game's input step */
        } else if (l->kind == L_ACTION && l->arg == ACT_TEST_PICTURE) {
            g_gta2_test_grab = 3;   /* the menu hidden for two frames, then the picture saved */
            wr_wr32(FE_SOUND, SND_SELECT);
        } else if (l->kind == L_ACTION && l->arg == ACT_RESTART_NOW) {
            wr_restart();
        } else if (l->kind == L_ACTION && l->arg == ACT_RESTART_LATER) {
            go_back(ctx, f);   /* asked again the next time GAMEPLAY is left */
        } else if (l->kind == L_SHPRESET) {
            gta2_shader_browse_open();
            build_browse_page();
            open_vpage(VP_SHBROWSE);
            wr_wr32(FE_SOUND, SND_SELECT);
        } else if (l->kind == L_SHPARAMS) {
            build_params_page();
            open_vpage(VP_SHPARAMS);
            wr_wr32(FE_SOUND, SND_SELECT);
        } else if (l->kind == L_SHITEM) {
            if (gta2_shader_pick(&g_browse_items[l->arg])) {   /* into a folder or up */
                build_browse_page();
                g_cur = g_top = 0;
            } else {
                go_back(ctx, f);   /* chosen: back to SHADERS */
            }
            wr_wr32(FE_SOUND, SND_SELECT);
        } else if (l->kind == L_BIND) {
            g_wait = l->arg;
            g_wait_t0 = wr_time_ms();
            g_wait_pad_base = wr_pad_controls();
            wr_keys_keyboard_dik(g_wait_key_base);
            wr_wr32(FE_SOUND, SND_SELECT);
        } else if (l->kind == L_ACTION && l->arg == ACT_RESUME) {
            pause_close(ctx, 1);
        } else if (l->kind == L_ACTION && l->arg == ACT_NO) {
            go_back(ctx, f);
        } else if (l->kind == L_ACTION && (l->arg == ACT_QUIT_YES || l->arg == ACT_RESTART_YES)) {
            uint32_t g = gta2_game(), a2[2] = { 1, 2 };   /* end the game: reason 2 = quit (the prompt's yes) */
            if (g) {
                call_this(ctx, FN_END_GAME, g, 2, a2);
                g_after_quit = l->arg == ACT_QUIT_YES ? 1 : 2;
            }
            pause_close(ctx, 0);
        } else if (l->kind == L_ACTION && l->arg == ACT_QUITPROG_YES) {
            /* as EXIT TO MAIN MENU (the progress is stored), then the
             * frontend quits at once (reason 1 "close the window" crashes
             * from a running game) */
            uint32_t g = gta2_game(), a2[2] = { 1, 2 };
            if (g) {
                call_this(ctx, FN_END_GAME, g, 2, a2);
                g_after_quit = 3;
            }
            pause_close(ctx, 0);
        } else if (l->kind == L_ACTION && l->arg == ACT_UNLOCK_AREAS) {
            call_this(ctx, FN_UNLOCK_ALL, wr_rd32(PROFILES_PTR), 0, NULL);
            wr_wr32(FE_SOUND, 9);   /* the game's "cheat accepted" sound */
        } else {
            change(ctx, l, 1);
        }
    } else if ((left || right) && g_vp == VP_SHBROWSE) {
        g_cur += right ? 5 : -5;   /* five at a time (maintainer 2026-11-26) */
        if (g_cur >= pg->n) g_cur = pg->n - 1;
        if (g_cur < 0) g_cur = 0;
        wr_wr32(FE_SOUND, right ? SND_DOWN : SND_UP);
    } else if (left || right) {
        change(ctx, l, right ? 1 : -1);
    }
    pg = &g_pages[g_vp];
    if (g_cur >= pg->n)
        g_cur = pg->n - 1;
    if (g_cur < 0)   /* an empty page: never a line before the first */
        g_cur = 0;
    if (g_cur < g_top)
        g_top = g_cur;
    if (g_cur >= g_top + VISIBLE)
        g_top = g_cur - VISIBLE + 1;
}

/* The game's menu input step (sub_004597C0): on page 12 the port takes every
 * key; returns 1 when it did (the caller then runs the original with all
 * edge flags cleared). */
int gta2_menu_input(X86Ctx *ctx)
{
    uint32_t f = fe();
    if (!f)
        return 0;
    uint16_t page = wr_rd16(f + FE_PAGE);
    if (getenv("WR_MENU_LOG")) {
        static int n;
        uint32_t e = wr_rd32(f + FE_EDGE) | wr_rd32(f + FE_EDGE + 4);
        if (e && n++ < 5000)
            wr_log("menu: page %u edges %08X %08X", page, wr_rd32(f + FE_EDGE), wr_rd32(f + FE_EDGE + 4));
    }
    if (page == 0 && wr_rd16(f + 0x122) == 3) {   /* main menu: Esc, or Enter on EXIT GAME */
        int esc0 = wr_rd8(f + FE_EDGE + 5);
        int exit0 = wr_rd8(f + FE_EDGE + 4) && wr_rd16(f + FE_PAGES + 0xBC6) == 2;
        if (esc0 || exit0) {
            wr_wr8(f + FE_EDGE + 4, 0);
            wr_wr8(f + FE_EDGE + 5, 0);
            uint32_t a = PORT_PAGE;
            call_this(ctx, FN_GOTO_PAGE, f, 1, &a);
            lang_init();
            g_vp = VP_EXIT;
            g_cur = g_top = 0;   /* NO */
            g_last_page = PORT_PAGE;
            wr_wr32(FE_SOUND, SND_SELECT);
            return 1;
        }
    }
    if (g_after_quit && getenv("WR_MENU_LOG")) {
        static uint16_t lp = 0xFFFF;
        if (page != lp)
            wr_log("menu: after quit, page %u", page);
        lp = page;
    }
    if (page == 11 && g_after_quit) {   /* PLAYER QUIT! after the pause menu's choice */
        if (g_after_quit == 1) {
            uint32_t a = 0;
            call_this(ctx, FN_GOTO_PAGE, f, 1, &a);
            for (int i = 0; i < 7; i++)   /* the YES press must not also choose PLAY on the main menu */
                wr_wr8(f + FE_EDGE + i, 0);
            g_post_cursor = 0;
        } else if (g_after_quit == 3) {
            g_post_quit = 1;   /* EXIT GAME: the frontend's result "quit" */
        } else {   /* REPLAY PREVIOUS AREA: the game's own restart */
            wr_wr16(f + FE_PAGES + 11 * PAGE_SIZE + 0xBC6, 1);
            wr_wr8(f + FE_EDGE + 4, 1);
        }
        g_after_quit = 0;
        g_last_page = page;
        return 0;
    }
    if (page != PORT_PAGE) {
        g_last_page = page;
        return 0;
    }
    if (g_last_page != PORT_PAGE) {   /* just entered from the main menu */
        lang_init();
        g_vp = VP_ROOT;
        g_cur = g_top = 0;
        g_last_page = PORT_PAGE;
    }
    int left = wr_rd8(f + FE_EDGE + 0), right = wr_rd8(f + FE_EDGE + 1), up = wr_rd8(f + FE_EDGE + 2),
        down = wr_rd8(f + FE_EDGE + 3), enter = wr_rd8(f + FE_EDGE + 4), esc = wr_rd8(f + FE_EDGE + 5);
    {
        uint8_t held[4] = { wr_rd8(f + 8 + 0xC8), wr_rd8(f + 8 + 0xD0), wr_rd8(f + 8 + 0xCB), wr_rd8(f + 8 + 0xCD) };
        key_repeat(held, &up, &down, &left, &right);
    }
    for (int i = 0; i < 7; i++)
        wr_wr8(f + FE_EDGE + i, 0);
    /* Home (pad Y): the game's menus have no edge flag for it */
    static uint8_t home_prev;
    uint8_t home = wr_rd8(f + 8 + 0xC7) & 0x80;
    int defaults = home && !home_prev;
    home_prev = home;
    menu_keys(ctx, f, left, right, up, down, enter, esc, defaults);
    return 1;
}

/* ---- drawing -------------------------------------------------------------------------------- */

static void put_text(X86Ctx *ctx, const char *s, int x, int y, uint16_t font, uint32_t scale)
{
    if (!g_strbuf)
        g_strbuf = wr_galloc(256);
    int i = 0;
    for (; s[i] && i < 120; i++)
        wr_wr16(g_strbuf + 2 * (uint32_t)i, (uint16_t)(unsigned char)s[i]);
    wr_wr16(g_strbuf + 2 * (uint32_t)i, 0);
    uint32_t a[5] = { g_strbuf, (uint32_t)x << 14, (uint32_t)y << 14, font, scale };
    wr_call_guest(ctx, FN_DRAW_TEXT, 5, a);
}

static int text_w(X86Ctx *ctx, const char *s, uint16_t font, uint32_t scale)
{
    int i = 0;
    for (; s[i] && i < 120; i++)
        wr_wr16(g_strbuf + 2 * (uint32_t)i, (uint16_t)(unsigned char)s[i]);
    wr_wr16(g_strbuf + 2 * (uint32_t)i, 0);
    uint32_t a[2] = { g_strbuf, font };
    return (int)((uint64_t)wr_call_guest(ctx, FN_TEXT_W, 2, a) * scale >> 14);
}

/* a CONTROL OVERVIEW row: function | controller | keyboard in three columns */
static void put_text(X86Ctx *ctx, const char *s, int x, int y, uint16_t font, uint32_t scale);
static void draw_overview_row(X86Ctx *ctx, const mline *l, const char *v, int x0, int xr, int y, uint16_t fo,
                              uint32_t sc)
{
    int w = xr - x0;
    put_text(ctx, line_label(l), x0, y, fo, sc);
    const char *bar = strstr(v, "   |   ");
    if (bar) {
        char a[64];
        size_t n = (size_t)(bar - v) < sizeof a - 1 ? (size_t)(bar - v) : sizeof a - 1;
        memcpy(a, v, n);
        a[n] = 0;
        put_text(ctx, a, x0 + w * 30 / 100, y, fo, sc);
        put_text(ctx, bar + 7, x0 + w * 66 / 100, y, fo, sc);
    }
}

/* CONTROLLER LAYOUT: the drawn pad over the whole screen (sw x sh), the labels at its lines */
static int text_w(X86Ctx *ctx, const char *s, uint16_t font, uint32_t scale);
static void draw_padview(X86Ctx *ctx, int sw, int sh, uint16_t font, uint16_t hi, uint32_t sc, uint32_t title_sc)
{
    float fx = (float)sw / 640.0f, fy = (float)sh / 480.0f;
    gbh_port_image(gta2_pad_picture(), 640, 480, 0, 0, (float)sw, (float)sh);
    put_text(ctx, "CONTROLLER LAYOUT", (int)(40 * fx), (int)(30 * fy), hi, title_sc);
    char t[64];
    snprintf(t, sizeof t, "CONTROLLER %s", opt_choice_label(OPT_CONTROL_SCHEME, opt_get(OPT_CONTROL_SCHEME)));
    put_text(ctx, t, (int)(40 * fx), (int)(62 * fy), font, sc);
    for (int i = 0; i < PV_N; i++) {
        const pv_item *it = &g_pv_items[i];
        gta2_pad_function(it->id, g_pv_car, t, sizeof t);
        int y = (int)((float)it->ly * fy);
        int x = it->right ? (int)(PV_RX * fx) : (int)(PV_LX * fx) - text_w(ctx, t, font, sc);
        put_text(ctx, t, x, y, font, sc);
    }
    const char *v = g_pv_car ? "<  IN A CAR  >" : "<  ON FOOT  >";
    put_text(ctx, v, (sw - text_w(ctx, v, hi, title_sc)) / 2, (int)(410 * fy), hi, title_sc);
}

#define X_LABEL 290
#define X_RIGHT 615
#define Y_FIRST 205
#define Y_STEP  20
#define SCALE   0x3000u   /* 0.75: label and value fit the right half */

/* After the game drew page 12 (its blank line): the port's lines. */
void gta2_menu_draw(X86Ctx *ctx)
{
    uint32_t f = fe();
    if (!f || wr_rd16(f + FE_PAGE) != PORT_PAGE || g_last_page != PORT_PAGE)
        return;
    if (!g_strbuf)
        g_strbuf = wr_galloc(256);
    uint16_t font = wr_rd16(f + FE_FONT), hi = wr_rd16(FONT_HILITE);
    const mpage *pg = &g_pages[g_vp];
    int xl = X_LABEL, xr = X_RIGHT;
    if (g_vp == VP_PADVIEW) {
        draw_padview(ctx, 640, 480, font, hi, 0x2400u, 0x3000u);
        return;
    }
    if (g_vp == VP_SHADERS || g_vp == VP_SHBROWSE || g_vp == VP_SHPARAMS) {
        /* the shader pages: a test picture behind, the lines centred and wide on a dark panel so
         * long preset names fit (maintainer 2026-11-26) */
        xl = 40;
        xr = 600;
        gbh_port_image(gta2_shader_test_picture(), 640, 480, 0, 0, 640, 480);
        int rows = pg->n - g_top < VISIBLE ? pg->n - g_top : VISIBLE;
        gbh_port_rect((float)(xl - 12), (float)(Y_FIRST - 48), (float)(xr - xl + 24),
                      (float)(48 + rows * Y_STEP + 30), 0xB0000000u);
    } else if (g_vp == VP_OVERVIEW) {   /* three columns: the whole width on a dark panel */
        xl = 24;
        xr = 616;
        int rows = pg->n - g_top < VISIBLE ? pg->n - g_top : VISIBLE;
        gbh_port_rect((float)(xl - 12), (float)(Y_FIRST - 48), (float)(xr - xl + 24),
                      (float)(48 + rows * Y_STEP + 30), 0xD0000000u);
    }
    put_text(ctx, pg->title, xl, Y_FIRST - 38, font, 0x4000);
    char v[128];
    for (int i = g_top; i < pg->n && i < g_top + VISIBLE; i++) {
        const mline *l = &pg->l[i];
        int y = Y_FIRST + (i - g_top) * Y_STEP;
        uint16_t fo = i == g_cur ? hi : font;
        line_value(l, v, sizeof v);
        if (l->kind == L_OVR) {
            draw_overview_row(ctx, l, v, xl, xr, y, fo, 0x2800u);
            continue;
        }
        put_text(ctx, line_label(l), xl, y, fo, SCALE);
        if (v[0])
            put_text(ctx, v, xr - text_w(ctx, v, fo, SCALE), y, fo, SCALE);
    }
    if (g_top > 0)
        put_text(ctx, "...", xl, Y_FIRST - 16, font, SCALE);
    if (g_top + VISIBLE < pg->n)
        put_text(ctx, "...", xl, Y_FIRST + VISIBLE * Y_STEP - 6, font, SCALE);
    if (g_vp == VP_SHADERS && gta2_shader_hint())
        put_text(ctx, gta2_shader_hint(), xl, Y_FIRST + 4 * Y_STEP, font, SCALE);
    if (g_vp == VP_LANG)
    {   /* two lines: one is too wide for the 4:3 page */
        put_text(ctx, "THE NEW LANGUAGE", xl, Y_FIRST + 3 * Y_STEP, font, SCALE);
        put_text(ctx, "NEEDS A RESTART", xl, Y_FIRST + 4 * Y_STEP, font, SCALE);
    }
}

/* After the game read its Debug flags at start: the saved cheats. */
void gta2_menu_apply_cheats(void)
{
    for (int i = 0; i < N_CHEATS; i++)
        wr_wr8(g_cheats[i].flag, (uint8_t)(wr_settings_get_int(g_cheats[i].key, g_cheats[i].def) != 0));
}

/* ---- the pause menu over the game ----------------------------------------------------------- */

/* ASPECT RATIO changed during a game: the game's own WM_SETFOCUS sequence
 * (docs/re/enhancements.md §3) - re-read Screen\\full_width and set the mode
 * (sub_004CC5C0), refresh cameras + HUD (sub_004CB880), forced tick off -
 * at a loop-step boundary, not during a movie or with the surface locked. */
extern int g_gta2_aspect_pending;

static void apply_aspect_live(X86Ctx *ctx, uint32_t g)
{
    if (!g_gta2_aspect_pending)
        return;
    uint32_t vs = wr_rd32(0x00673D20u);
    if (!g || wr_rd8(0x00595018u) || wr_rd32(0x00673E2Cu) || !vs || (wr_rd32(vs + 4) & 2u))
        return;   /* not now (or not in a game: the next game reads it anyway) */
    if (wr_call_guest(ctx, 0x00481DD0u, 0, NULL))
        return;   /* a movie plays */
    g_gta2_aspect_pending = 0;
    uint32_t ecx = ctx->r_ecx;
    wr_call_guest(ctx, 0x004CC5C0u, 0, NULL);
    wr_call_guest(ctx, 0x004CB880u, 0, NULL);
    wr_wr8(0x006735A5u, 0);
    ctx->r_ecx = ecx;
    wr_log("aspect: switched in the game to %ux%u", g_gfx.w, g_gfx.h);
}

/* Between two game loop steps (before sub_00462A30): START / Esc opens the
 * menu (the game pauses itself, freeze.wav); while open the port reads the
 * keys and the game gets none. Only in single-player games. */
void gta2_pause_tick(X86Ctx *ctx)
{
    uint32_t g = gta2_game();
    int playing = g && wr_mem_valid(g, 0x40) && wr_rd8(g + 0x23) == 1 && !wr_rd32(0x00673E2Cu) &&
                  !wr_rd8(0x00595018u);
    /* Esc / START never reach the game in play: they open this menu (the
     * game's own "Is that it?" prompt is replaced by EXIT TO MAIN MENU) */
    g_wr_key_hidden[0x01] = (uint8_t)playing;
    g_wr_key_hidden[0x42] = 1;   /* F8: the game would minimise and start the GTA2 Manager (gone) */
    if (!playing) {
        if (g_pause_open)
            pause_close(ctx, 0);
        if (!g)
            g_gta2_aspect_pending = 0;   /* no game: the next one reads the new width at its start */
        return;
    }
    apply_aspect_live(ctx, g);
    gta2_peds_live(ctx);
    {   /* dev: WR_TEST_BLOOD=1 - one blood pool at the player after 12 s of play */
        static int done;
        static uint32_t t0;
        if (!done && getenv("WR_TEST_BLOOD") && gta2_player_ped()) {
            if (!t0)
                t0 = wr_time_ms();
            else if (wr_time_ms() - t0 > 12000) {
                uint32_t pp = gta2_player_ped(), a[3] = { wr_rd32(pp + 0x1AC), wr_rd32(pp + 0x1B0), wr_rd32(pp + 0x1B4) };
                uint32_t ecx = ctx->r_ecx;
                ctx->r_ecx = wr_rd32(0x00669E74u);
                wr_call_guest(ctx, 0x0048CC50u, 3, a);
                ctx->r_ecx = ecx;
                done = 1;
            }
        }
    }
    static uint8_t prev[256];
    uint8_t st[256];
    wr_keys_dik_state(st);
#define EDGE(k) ((st[k] & 0x80) && !(prev[k] & 0x80))
    int esc = EDGE(0x01), enter = EDGE(0x1C), up = EDGE(0xC8), down = EDGE(0xD0), left = EDGE(0xCB),
        right = EDGE(0xCD), home = EDGE(0xC7);
#undef EDGE
    memcpy(prev, st, sizeof prev);
    if (g_pause_open) {
        uint8_t held[4] = { st[0xC8], st[0xD0], st[0xCB], st[0xCD] };
        key_repeat(held, &up, &down, &left, &right);
    }
    if (!g_pause_open) {
        uint32_t player = gta2_player();
        if (!esc || (player && wr_rd8(player + 0x78A)))
            return;
        if (wr_rd32(g) == 1)
            call_this(ctx, FN_PAUSE, g, 0, NULL);
        if (wr_rd32(g) != 2)
            return;   /* the game would not pause (a state change in progress) */
        lang_init();
        build_cheat_page();
        g_pause_open = 1;
        g_host = HOST_PAUSE;
        g_pages[VP_SHADERS].n = 5;   /* + TAKE TEST PICTURE */
        if (opt_get(OPT_MINIMAP) && opt_get(OPT_PAUSE_MAP)) {
            g_pages[VP_PAUSE].l = l_pause_map;
            g_pages[VP_PAUSE].n = (int)(sizeof l_pause_map / sizeof l_pause_map[0]);
        } else {
            g_pages[VP_PAUSE].l = l_pause;
            g_pages[VP_PAUSE].n = (int)(sizeof l_pause / sizeof l_pause[0]);
        }
        g_wr_input_captured = 1;
        g_vp = VP_PAUSE;
        g_cur = g_top = 0;
        return;
    }
    menu_keys(ctx, 0, left, right, up, down, enter, esc, home);
}

/* The game's PAUSED panel (sub_004C9FA0) stays hidden while the menu is open. */
int gta2_pause_hide_panel(void) { return g_pause_open; }

#define P_FONT  1      /* the level style's HUD font (fonts 3 and 4 do not exist in play: never use them) */
#define P_HI    6      /* the "PAUSED" font for the title and the chosen line */
#define P_FONT_SCALE 0x4000u   /* the look the maintainer chose (2026-11-26) */
#define P_HI_SCALE   0x2000u   /* font 6 at half size = font 1's height */

/* After the game drew its HUD (sub_004CA440, inside Begin/EndScene). */
void gta2_pause_draw(X86Ctx *ctx)
{
    if (!g_pause_open || g_gta2_test_grab > 0)   /* TAKE TEST PICTURE: the game's picture without the menu */
        return;
    if (!g_strbuf)
        g_strbuf = wr_galloc(256);
    int sw = (int)g_gfx.w, sh = (int)g_gfx.h;
    if (g_vp == VP_PADVIEW) {
        draw_padview(ctx, sw, sh, P_FONT, P_HI, P_FONT_SCALE * 5 / 8, P_HI_SCALE);
        return;
    }
    gbh_port_rect(0, 0, (float)sw, (float)sh, 0xA0000000u);   /* dim the game */
    if (g_vp == VP_MAP) {   /* PAUSE MAP: the district as big as the screen's height allows, the title above */
        /* the map on the left, the legend in the room to its right (maintainer 2026-11-27); 4:3 too */
        float s = (float)sh * 0.80f, y = (float)sh * 0.15f;
        float lw = (float)sw * 0.26f;                       /* the legend's column */
        float x = ((float)sw - s - lw) / 2;
        if (!gta2_radar_draw_full(x, y, s)) {
            put_text(ctx, "NO MAP", (int)x, (int)y, P_FONT, P_FONT_SCALE);
        } else {
            float lx = x + s + (float)sw * 0.03f, ly = y + 10, d = (float)sh * 0.032f, step = (float)sh * 0.05f;
            const char *t;
            for (int n = 0; (t = gta2_radar_legend(n, lx + d / 2, ly + (float)n * step, d)) != NULL; n++)
                put_text(ctx, t, (int)(lx + d * 1.6f), (int)(ly + (float)n * step - d * 0.6f), P_FONT,
                         P_FONT_SCALE * 5 / 8);
        }
        put_text(ctx, "MAP", (int)x, (int)((float)sh * 0.02f), P_HI, P_HI_SCALE * 2);   /* over it, above it */
        return;
    }
    const mpage *pg = &g_pages[g_vp];
    int wide = g_vp == VP_SHADERS || g_vp == VP_SHBROWSE || g_vp == VP_SHPARAMS ||
               g_vp == VP_OVERVIEW;   /* long preset names, three columns */
    int pw = wide ? (g_vp == VP_OVERVIEW ? 600 : 580) : 420, x0 = (sw - pw) / 2, xr = x0 + pw;
    int y0 = sh / 2 - 110;
    gbh_port_rect((float)(x0 - 20), (float)(y0 - 48), (float)(pw + 40), 300.0f, 0xC0201410u);
    put_text(ctx, pg->title, x0, y0 - 38, P_HI, 0x4000);
    char v[128];
    for (int i = g_top; i < pg->n && i < g_top + VISIBLE; i++) {
        const mline *l = &pg->l[i];
        int y = y0 + (i - g_top) * 24;
        uint16_t fo = i == g_cur ? P_HI : P_FONT;
        uint32_t sc = i == g_cur ? P_HI_SCALE : P_FONT_SCALE;
        line_value(l, v, sizeof v);
        if (l->kind == L_OVR) {
            draw_overview_row(ctx, l, v, x0, xr, y, fo, sc * 3 / 4);
            continue;
        }
        put_text(ctx, line_label(l), x0, y, fo, sc);
        if (v[0])
            put_text(ctx, v, xr - text_w(ctx, v, fo, sc), y, fo, sc);
    }
    if (g_top > 0)
        put_text(ctx, "...", x0, y0 - 16, P_FONT, 0x4000);
    if (g_top + VISIBLE < pg->n)
        put_text(ctx, "...", x0, y0 + VISIBLE * 24 - 6, P_FONT, 0x4000);
    if (g_vp == VP_SHADERS && gta2_shader_hint())
        put_text(ctx, gta2_shader_hint(), x0, y0 + pg->n * 24, P_FONT, 0x4000);
    if (g_vp == VP_LANG)
    {
        put_text(ctx, "THE NEW LANGUAGE", x0, y0 + 3 * 24, P_FONT, 0x4000);
        put_text(ctx, "NEEDS A RESTART", x0, y0 + 4 * 24, P_FONT, 0x4000);
    }
}


/* The radio label (maintainer 2026-11-26): the game names no station. When a station
 * comes on - a game station or MIXTAPE - its name and what it plays show for 4 s,
 * centred near the top; MIXTAPE shows each new song too. Station numbers = the
 * GTAudio file numbers (docs/re/radio.md); 1 = Head Radio (the one file of double size,
 * two DJs). Unknown ones show their number until identified. */
/* names and music from the maintainer's list (2026-11-26) - English in every language, never translated; numbers: 1 and 5 play in every
 * district (Head Radio, Futuro FM), 2 / 3 / 4 are the district stations without a gang
 * (Rockstar Radio downtown, KREZ residential, Lo-Fi FM industrial). 6..11 are the two gang
 * stations of each district: Downtown 6 Funami FM (Japanese), 7 Lithium FM (oldies, electro, a
 * Jesus song only Lithium / Heavenly play; its host does voices), Residential 8 King 130.7 ("Taxi
 * Drivers"), 9 Osmosis FM ("I Wanna Phunk"), Industrial 10 Heavenly Radio, 11 KGBH (acid sounds) - all
 * heard by the maintainer with GTA2_RADIO_TEST, as the wiki says. */
static const char *const k_station_name[12] = {
    "", "HEAD RADIO", "ROCKSTAR RADIO", "KREZ", "LO-FI FM", "FUTURO FM", "FUNAMI FM", "LITHIUM FM", "KING 130.7", "OSMOSIS FM",
    "HEAVENLY RADIO", "KGBH",
};
static const char *const k_station_info[12][2] = {
    { "", "" },
    { "PHANNY JOE STYLES: FUNK, POP AND DANCE", "JOHNNY RICCARO: JAZZSTEP AND ROCK" },
    { "ROCK, FUNK AND HOUSE", "" },
    { "HIP HOP, RAP, JAZZSTEP AND INDUSTRIAL", "" },
    { "FUNK, JAZZ, OLDIES AND CLASSIC ROCK", "" },
    { "FUNK, DANCE AND CLASSIC ROCK", "" },
    { "ACID, ELECTRONIC AND DRUM N BASS", "" },   /* 6: heard (Japanese commercials) */
    { "TECHNO, OLDIES, ELECTRONIC AND CHRISTIAN ROCK", "" },   /* 7: heard: oldies, electro, a Jesus song */
    { "HARD ROCK AND SOFT ROCK", "" },   /* 8: heard (GTA2_RADIO_TEST): "Taxi Drivers" plays only on Head Radio + King */
    { "DANCE, POP AND ALTERNATIVE ROCK", "" },   /* 9: heard: "I Wanna Phunk" (only Rockstar Radio + Osmosis FM) */
    { "CHRISTIAN ROCK, SOFT ROCK AND TRADITIONAL GOSPEL", "" },   /* 10: KGBH's pair in Industrial */
    { "CLASSIC ROCK AND TECHNO", "" },   /* 11: heard: psychedelic / acid sounds */
};

void gta2_radio_label_draw(X86Ctx *ctx)
{
    static int was_mix;
    static uint32_t was_st;
    static char song[128], title[48], l1[96], l2[96];
    static uint64_t until;
    int mix = gta2_mixtape_audible();
    uint32_t st = gta2_radio_station();
    const char *cur = mix ? gta2_mixtape_song() : "";
    if (mix && (!was_mix || strcmp(cur, song))) {
        snprintf(song, sizeof song, "%s", cur);
        snprintf(title, sizeof title, "MIXTAPE");
        upper_text(song, l1, sizeof l1);
        l2[0] = 0;
        until = wr_time_ns() + 4000000000ull;
    } else if (!mix && st && st != was_st) {
        if (st < 12 && k_station_name[st]) {
            snprintf(title, sizeof title, "%s", k_station_name[st]);
            snprintf(l1, sizeof l1, "%s", k_station_info[st][0]);
            snprintf(l2, sizeof l2, "%s", k_station_info[st][1]);
        } else {
            snprintf(title, sizeof title, "STATION %u", st);
            l1[0] = l2[0] = 0;
        }
        wr_log("radio: station %u", st);
        until = wr_time_ns() + 4000000000ull;
    }
    was_mix = mix;
    was_st = st;
    if ((!mix && !st) || wr_time_ns() > until || g_pause_open)
        return;
    if (!g_strbuf)
        g_strbuf = wr_galloc(256);
    int sw = (int)g_gfx.w;
    /* small and high: above the game's own messages (maintainer 2026-11-26) */
    uint32_t sc = P_FONT_SCALE * 3 / 4, tsc = P_HI_SCALE * 3 / 4;   /* the song line as it was (maintainer) */
    put_text(ctx, title, (sw - text_w(ctx, title, P_HI, tsc)) / 2, 22, P_HI, tsc);
    if (l1[0])
        put_text(ctx, l1, (sw - text_w(ctx, l1, P_FONT, sc)) / 2, 40, P_FONT, sc);
    if (l2[0])
        put_text(ctx, l2, (sw - text_w(ctx, l2, P_FONT, sc)) / 2, 62, P_FONT, sc);
}
