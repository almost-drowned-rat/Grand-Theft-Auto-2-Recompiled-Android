/*
 * GTA 2 Recompiled - RECOMP OPTIONS as data (see gta2_options.h).
 */
#include "gta2_options.h"
#include "wr.h"
#include "vk/wr_vk.h"
#include "platform/wr_platform.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void wr_reg_set_dword(const char *key, const char *name, uint32_t value);
void wr_miles_set_half_stereo_rate(int on);
void gfx_apply_window(void);
void gfx_apply_vsync(int on);

#define REG_SCREEN "HKEY_LOCAL_MACHINE\\SOFTWARE\\DMA Design Ltd\\GTA2\\Screen"

typedef struct {
    const char *label;
    const char *key;               /* settings.ini key (NULL: special) */
    int n;
    const char *const *values;     /* stored values */
    const char *const *labels;     /* shown */
    int def;
    void (*apply)(int c);
} optdef;

static const char *const v_display[] = { "windowed", "borderless", "fullscreen" };
static const char *const l_display[] = { "WINDOWED", "BORDERLESS", "FULLSCREEN" };
static const char *const v_res[] = { "native", "480", "720", "1080", "1440", "2160" };
static const char *const l_res[] = { "NATIVE", "480P", "720P", "1080P", "1440P", "2160P" };
static const char *const v_aspect[] = { "4:3", "16:9", "16:10", "21:9" };
static const char *const v_filter[] = { "original", "nearest", "bilinear" };
static const char *const l_filter[] = { "ORIGINAL", "NEAREST", "BILINEAR" };
/* the logic stays at 30 ticks; 60 / 120 draw interpolated frames in between (maintainer 2026-11-26) */
static const char *const v_rate[] = { "30", "60", "120" };
static const char *const l_rate[] = { "30", "60 INTERPOLATED", "120 INTERPOLATED" };
static const char *const v_kscheme[] = { "classic", "modern", "mouse" };   /* modern = keyboard only (the pad layout) */
static const char *const l_kscheme[] = { "CLASSIC", "MODERN", "KEYBOARD + MOUSE" };
static const char *const v_scheme[] = { "classic", "modern", "custom" };
static const char *const l_scheme[] = { "CLASSIC", "MODERN", "CUSTOM" };
static const char *const v_factor[] = { "1", "2", "3", "4" };
static const char *const l_factor[] = { "1X", "2X", "3X", "4X" };
static const char *const v_onoff[] = { "0", "1" };
static const char *const l_onoff[] = { "OFF", "ON" };
static const char *const v_size[] = { "small", "medium", "large" };
static const char *const l_size[] = { "SMALL", "MEDIUM", "LARGE" };
static const char *const v_zoom[] = { "near", "medium", "far" };
static const char *const l_zoom[] = { "NEAR", "MEDIUM", "FAR" };
/* MINIMAP POSITION X / Y in 2.5 % steps (maintainer 2026-11-27); the 5 % values of before stay valid */
static const char *const v_pct[] = {
    "0", "2.5", "5", "7.5", "10", "12.5", "15", "17.5", "20", "22.5", "25", "27.5", "30", "32.5",
    "35", "37.5", "40", "42.5", "45", "47.5", "50", "52.5", "55", "57.5", "60", "62.5", "65", "67.5",
    "70", "72.5", "75", "77.5", "80", "82.5", "85", "87.5", "90", "92.5", "95", "97.5", "100" };
static const char *const l_pct[] = {
    "0%", "2.5%", "5%", "7.5%", "10%", "12.5%", "15%", "17.5%", "20%", "22.5%", "25%", "27.5%", "30%", "32.5%",
    "35%", "37.5%", "40%", "42.5%", "45%", "47.5%", "50%", "52.5%", "55%", "57.5%", "60%", "62.5%", "65%", "67.5%",
    "70%", "72.5%", "75%", "77.5%", "80%", "82.5%", "85%", "87.5%", "90%", "92.5%", "95%", "97.5%", "100%" };
static const char *const v_arrows[] = { "0", "1", "near" };
static const char *const v_camdist[] = { "1", "1.25", "1.5", "1.75", "2" };
static const char *const l_camdist[] = { "1.0X", "1.25X", "1.5X", "1.75X", "2.0X" };
static const char *const l_arrows[] = { "OFF", "ON", "NEAR TARGET" };

static int g_val[OPT_COUNT];

static void apply_window(int c) { (void)c; gfx_apply_window(); }
static void apply_vsync(int c) { gfx_apply_vsync(c); }
static void apply_radio(int c) { wr_miles_set_half_stereo_rate(c); }
extern int g_gta2_peds_pending;
static void apply_peds(int c) { (void)c; g_gta2_peds_pending = 1; }   /* live in a game (gta2_gameplay.c) */
void gta2_keys_apply(void);
static void apply_keys(int c) { (void)c; gta2_keys_apply(); }
static void apply_nothing(int c) { (void)c; }   /* read by the renderer each frame */

static void apply_perf(int c)
{
    (void)c;
    int m = (g_val[OPT_PERF_FPS] ? WR_PERF_FPS : 0) | (g_val[OPT_PERF_FRAMETIME] ? WR_PERF_FRAMETIME : 0) |
            (g_val[OPT_PERF_SPEED] ? WR_PERF_SPEED : 0);
    wr_overlay_set(m);
}

/* ASPECT RATIO: the window's shape now, the gameplay width (the game's own
 * Screen\full_width, 480 lines high) from the next game start on. */
extern int g_gta2_aspect_pending;
static void apply_aspect(int c)
{
    (void)c;
    uint32_t w = (uint32_t)lround(480.0 * opt_aspect_ratio());
    w &= ~1u;
    wr_reg_set_dword(REG_SCREEN, "full_width", w);
    wr_reg_set_dword(REG_SCREEN, "full_height", 480);
    gfx_apply_window();
    g_gta2_aspect_pending = 1;   /* in a game: the mode switches at the next loop step (gta2_menu.c) */
}

int g_gta2_aspect_pending;

static const optdef g_def[OPT_COUNT] = {
    [OPT_DISPLAY_MODE]   = { "DISPLAY MODE", "display_mode", 3, v_display, l_display, 1, apply_window },
    [OPT_RENDER_RES]     = { "RENDER RESOLUTION", "render_resolution", 6, v_res, l_res, 0, apply_nothing },
    [OPT_ASPECT]         = { "ASPECT RATIO", "aspect_ratio", 4, v_aspect, v_aspect, 0, apply_aspect },
    [OPT_TEXTURE_FILTER] = { "TEXTURE FILTERING", "texture_filter", 3, v_filter, l_filter, 0, apply_nothing },
    [OPT_FRAME_RATE]     = { "FRAME RATE", "frame_rate", 3, v_rate, l_rate, 0, apply_nothing },
    [OPT_VSYNC]          = { "V-SYNC", "vsync", 2, v_onoff, l_onoff, 1, apply_vsync },
    [OPT_PERF_FPS]       = { "FPS", NULL, 2, v_onoff, l_onoff, 0, apply_perf },
    [OPT_PERF_FRAMETIME] = { "FRAME TIME", NULL, 2, v_onoff, l_onoff, 0, apply_perf },
    [OPT_PERF_SPEED]     = { "GAME SPEED", NULL, 2, v_onoff, l_onoff, 0, apply_perf },
    [OPT_RADIO_HALF_RATE] = { "RADIO RATE FIX", "radio_half_rate", 2, v_onoff, l_onoff, 1, apply_radio },
    [OPT_CONTROL_SCHEME] = { "CONTROLLER", "control_scheme", 3, v_scheme, l_scheme, 0, apply_nothing },
    [OPT_RUMBLE]         = { "RUMBLE", "rumble", 2, v_onoff, l_onoff, 1, apply_nothing },
    [OPT_KEY_SCHEME]     = { "KEYBOARD", "keyboard_scheme", 3, v_kscheme, l_kscheme, 0, apply_keys },
    [OPT_PEDESTRIANS]    = { "PEDESTRIANS", "pedestrians", 4, v_factor, l_factor, 0, apply_peds },
    [OPT_TRAFFIC]        = { "TRAFFIC", "traffic", 4, v_factor, l_factor, 0, apply_peds },
    [OPT_SPRINT]         = { "SPRINT", "sprint", 2, v_onoff, l_onoff, 1, apply_nothing },
    [OPT_MORE_BLOOD]     = { "MORE BLOOD", "more_blood", 2, v_onoff, l_onoff, 0, apply_nothing },
    /* no menu line: the camera buttons step it, MANUAL CAMERA off means 1.0X (maintainer 2026-11-27) */
    [OPT_CAMERA_DISTANCE] = { "CAMERA DISTANCE", "camera_distance", 5, v_camdist, l_camdist, 0, apply_nothing },
    /* off by default (maintainer 2026-11-27: a changed distance made the camera correct itself all the time) */
    [OPT_MANUAL_CAMERA]  = { "MANUAL CAMERA", "manual_camera", 2, v_onoff, l_onoff, 0, apply_nothing },
    [OPT_MINIMAP]        = { "MINIMAP", "minimap", 2, v_onoff, l_onoff, 1, apply_nothing },
    [OPT_MINIMAP_SIZE]   = { "SIZE", "minimap_size", 3, v_size, l_size, 0, apply_nothing },   /* SMALL (maintainer) */
    [OPT_MINIMAP_ZOOM]   = { "ZOOM", "minimap_zoom", 3, v_zoom, l_zoom, 0, apply_nothing },   /* NEAR */
    [OPT_MINIMAP_X]      = { "POSITION X", "minimap_x", 41, v_pct, l_pct, 0, apply_nothing },
    [OPT_MINIMAP_Y]      = { "POSITION Y", "minimap_y", 41, v_pct, l_pct, 40, apply_nothing },
    [OPT_PAUSE_MAP]      = { "PAUSE MAP", "minimap_pause_map", 2, v_onoff, l_onoff, 1, apply_nothing },
    [OPT_NAV_ARROWS]     = { "NAVIGATION ARROWS", "minimap_arrows", 3, v_arrows, l_arrows, 1, apply_nothing },
    [OPT_BLIP_MISSIONS]  = { "MISSIONS", "minimap_missions", 2, v_onoff, l_onoff, 1, apply_nothing },
    [OPT_BLIP_PLACES]    = { "CHURCHES + GARAGES", "minimap_places", 2, v_onoff, l_onoff, 1, apply_nothing },
    [OPT_BLIP_TOKENS]    = { "TOKENS", "minimap_tokens", 2, v_onoff, l_onoff, 1, apply_nothing },
    [OPT_BLIP_WEAPONS]   = { "WEAPONS", "minimap_weapons", 2, v_onoff, l_onoff, 0, apply_nothing },
    [OPT_BLIP_BONUSES]   = { "POWER-UPS", "minimap_powerups", 2, v_onoff, l_onoff, 1, apply_nothing },
    [OPT_BLIP_FRENZY]    = { "KILL FRENZY", "minimap_frenzy", 2, v_onoff, l_onoff, 1, apply_nothing },
    [OPT_BLIP_POLICE]    = { "POLICE", "minimap_police", 2, v_onoff, l_onoff, 1, apply_nothing },
};

static int find_choice(int id, const char *v)
{
    if (!v)
        return g_def[id].def;
    for (int c = 0; c < g_def[id].n; c++)
        if (!strcmp(v, g_def[id].values[c]))
            return c;
    return g_def[id].def;
}

void opt_load_all(void)
{
    for (int id = 0; id < OPT_COUNT; id++)
        if (g_def[id].key)
            g_val[id] = find_choice(id, wr_settings_get(g_def[id].key, NULL));
    int m = wr_overlay_parse(wr_settings_get("performance_overlay", "off"));
    g_val[OPT_PERF_FPS] = !!(m & WR_PERF_FPS);
    g_val[OPT_PERF_FRAMETIME] = !!(m & WR_PERF_FRAMETIME);
    g_val[OPT_PERF_SPEED] = !!(m & WR_PERF_SPEED);
    /* the game's gameplay width follows ASPECT RATIO before the game reads it */
    uint32_t w = (uint32_t)lround(480.0 * opt_aspect_ratio()) & ~1u;
    wr_reg_set_dword(REG_SCREEN, "full_width", w);
    wr_reg_set_dword(REG_SCREEN, "full_height", 480);
    wr_miles_set_half_stereo_rate(g_val[OPT_RADIO_HALF_RATE]);
    apply_perf(0);
    gta2_keys_apply();
    wr_log("options: display %s, resolution %s, aspect %s, filter %s, vsync %d",
           v_display[g_val[OPT_DISPLAY_MODE]], v_res[g_val[OPT_RENDER_RES]], v_aspect[g_val[OPT_ASPECT]],
           v_filter[g_val[OPT_TEXTURE_FILTER]], g_val[OPT_VSYNC]);
}

const char *opt_label(int id) { return id >= 0 && id < OPT_COUNT ? g_def[id].label : ""; }
int opt_count(int id) { return id >= 0 && id < OPT_COUNT ? g_def[id].n : 0; }
int opt_default(int id) { return id >= 0 && id < OPT_COUNT ? g_def[id].def : 0; }
int opt_get(int id) { return id >= 0 && id < OPT_COUNT ? g_val[id] : 0; }

const char *opt_choice_label(int id, int c)
{
    if (id < 0 || id >= OPT_COUNT || c < 0 || c >= g_def[id].n)
        return "";
    return g_def[id].labels[c];
}

void opt_set(int id, int c)
{
    if (id < 0 || id >= OPT_COUNT || c < 0 || c >= g_def[id].n || g_val[id] == c)
        return;
    g_val[id] = c;
    if (g_def[id].key) {
        wr_settings_set(g_def[id].key, g_def[id].values[c]);
    } else {   /* the PERFORMANCE switches: one value */
        char v[48];
        int m = (g_val[OPT_PERF_FPS] ? WR_PERF_FPS : 0) | (g_val[OPT_PERF_FRAMETIME] ? WR_PERF_FRAMETIME : 0) |
                (g_val[OPT_PERF_SPEED] ? WR_PERF_SPEED : 0);
        wr_overlay_format(m, v, sizeof v);
        wr_settings_set("performance_overlay", v);
    }
    g_def[id].apply(c);
}

int opt_visible(int id)
{
#if defined(__ANDROID__)
    if (id == OPT_DISPLAY_MODE)
        return 0;   /* always full screen on Android */
#endif
    return id >= 0 && id < OPT_COUNT;
}

double opt_aspect_ratio(void)
{
    switch (g_val[OPT_ASPECT]) {
    case 1: return 16.0 / 9.0;
    case 2: return 16.0 / 10.0;
    case 3: {   /* 21:9 = the display's own ratio when it is an ultrawide, else 64:27 */
        int dw = 0, dh = 0;
        wr_display_size(&dw, &dh);
        double r = dh > 0 ? (double)dw / dh : 0;
        return r >= 2.30 && r <= 2.42 ? r : 64.0 / 27.0;
    }
    default: return 4.0 / 3.0;
    }
}

int opt_render_height(void)
{
    static const int h[] = { 0, 480, 720, 1080, 1440, 2160 };
    return h[g_val[OPT_RENDER_RES]];
}

int opt_texture_filter(void) { return g_val[OPT_TEXTURE_FILTER] - 1; }
int opt_display_mode(void) { return g_val[OPT_DISPLAY_MODE]; }
