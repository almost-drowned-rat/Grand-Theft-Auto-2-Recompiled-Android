/*
 * GTA 2 Recompiled - RECOMP OPTIONS as data (see gta2_options.h).
 */
#include "gta2_options.h"
#include "wr.h"
#include "vk/wr_vk.h"
#include "platform/wr_platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
/* modern = keyboard only (the pad layout); custom = the keyboard's CUSTOM MAPPING by situation (maintainer 2026-11-27) */
static const char *const v_kscheme[] = { "classic", "modern", "mouse", "custom" };
static const char *const l_kscheme[] = { "CLASSIC", "MODERN", "KEYBOARD + MOUSE", "CUSTOM" };
static const char *const v_scheme[] = { "classic", "modern", "custom" };   /* PR #7's ANALOGUE: the switches below */
static const char *const l_scheme[] = { "CLASSIC", "MODERN", "CUSTOM" };
/* ANALOGUE SETTINGS (the page under CONTROLS): the car steering of the ANALOGUE scheme (gta2_feel.c turns the index into the number) */
static const char *const v_spreset[] = { "smooth", "snappy", "heavy", "custom" };
static const char *const l_spreset[] = { "SMOOTH", "SNAPPY", "HEAVY", "CUSTOM" };
static const char *const v_sdead[] = { "0.04", "0.06", "0.08", "0.10", "0.12", "0.14", "0.16", "0.20" };
static const char *const v_scurve[] = { "1.0", "1.2", "1.4", "1.6", "1.8", "2.0", "2.4", "3.0" };
static const char *const v_shold[] = { "1", "2", "3", "4" };
static const char *const v_spreset2[] = { "1", "2" };   /* SPRINT > PRESET */
static const char *const l_spreset2[] = { "PRESET 1", "PRESET 2" };
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
static const char *const v_dpreset[] = { "1", "2", "3", "custom" };   /* DAY / NIGHT > PRESET */
static const char *const l_dpreset[] = { "PRESET 1", "PRESET 2", "PRESET 3", "CUSTOM" };
#define DN_SECS_MAX 7200   /* a phase: 0:00 .. 120:00 */

static int g_val[OPT_COUNT];

static void apply_window(int c) { (void)c; gfx_apply_window(); }
static void apply_vsync(int c) { gfx_apply_vsync(c); }
static void apply_radio(int c) { wr_miles_set_half_stereo_rate(c); }
extern int g_gta2_peds_pending;
static void apply_peds(int c) { (void)c; g_gta2_peds_pending = 1; }   /* live in a game (gta2_gameplay.c) */
void gta2_keys_apply(void);
static void apply_keys(int c) { (void)c; gta2_keys_apply(); }
static void apply_nothing(int c) { (void)c; }   /* read by the renderer each frame */

/* ANALOGUE SETTINGS presets: the indexes of dead zone / curve / smoothing in the three tables below */
static const int k_spreset[3][3] = { { 3, 3, 1 },    /* SMOOTH: 0.10, 1.6, 2 */
                                     { 2, 1, 0 },    /* SNAPPY: 0.08, 1.2, 1 */
                                     { 4, 5, 1 } };  /* HEAVY:  0.12, 2.0, 2 */
static int g_steer_busy;
static void apply_steer_preset(int c)
{
    if (c < 0 || c > 2)
        return;   /* CUSTOM: the numbers stay as they are */
    g_steer_busy = 1;
    opt_set(OPT_STEER_DEAD, k_spreset[c][0]);
    opt_set(OPT_STEER_CURVE, k_spreset[c][1]);
    opt_set(OPT_STEER_HOLD, k_spreset[c][2]);
    g_steer_busy = 0;
}
static void apply_steer_value(int c)   /* a number was changed by hand: the preset it now equals, else CUSTOM */
{
    (void)c;
    if (g_steer_busy)
        return;
    int p = 3;
    for (int i = 0; i < 3; i++)
        if (opt_get(OPT_STEER_DEAD) == k_spreset[i][0] && opt_get(OPT_STEER_CURVE) == k_spreset[i][1] &&
            opt_get(OPT_STEER_HOLD) == k_spreset[i][2])
            p = i;
    opt_set(OPT_STEER_PRESET, p);   /* its apply sees CUSTOM or the same numbers again: nothing more happens */
}

/* DAY / NIGHT presets (maintainer 2026-11-28): the DAY LENGTHs of before, 24 / 48 / 96 minutes a day, in the cycle's
 * proportions DAY 9 : SUNSET 4 : NIGHT 7.5 : SUNRISE 3.5 (the hours of gta2_daynight.c) */
static const int k_dpreset[3][4] = { { 540, 240, 450, 210 }, { 1080, 480, 900, 420 }, { 2160, 960, 1800, 840 } };
static int g_day_busy;
static void apply_day_preset(int c)
{
    if (c < 0 || c > 2)
        return;   /* CUSTOM: the lengths stay */
    g_day_busy = 1;
    for (int i = 0; i < 4; i++)
        opt_set(OPT_DAY_SECS + i, k_dpreset[c][i]);
    g_day_busy = 0;
}
static void apply_day_secs(int c)   /* a length changed by hand: the preset it equals, else CUSTOM */
{
    (void)c;
    if (g_day_busy)
        return;
    int p = 3;
    for (int k = 0; k < 3 && p == 3; k++) {
        int same = 1;
        for (int i = 0; i < 4; i++)
            same &= opt_get(OPT_DAY_SECS + i) == k_dpreset[k][i];
        if (same)
            p = k;
    }
    opt_set(OPT_DAY_PRESET, p);
}

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
    [OPT_KEY_SCHEME]     = { "KEYBOARD", "keyboard_scheme", 4, v_kscheme, l_kscheme, 0, apply_keys },
    [OPT_PEDESTRIANS]    = { "PEDESTRIANS", "pedestrians", 4, v_factor, l_factor, 0, apply_peds },
    [OPT_TRAFFIC]        = { "TRAFFIC", "traffic", 4, v_factor, l_factor, 0, apply_peds },
    /* the stamina sprint in every scheme (maintainer 2026-11-28; its numbers: OPT_SPRINT_PRESET) */
    [OPT_SPRINT]         = { "SPRINT", "sprint", 2, v_onoff, l_onoff, 1, apply_nothing },
    [OPT_MORE_BLOOD]     = { "MORE BLOOD", "more_blood", 2, v_onoff, l_onoff, 0, apply_nothing },
    /* no menu line: the camera buttons step it, MANUAL CAMERA off means 1.0X (maintainer 2026-11-27) */
    [OPT_CAMERA_DISTANCE] = { "CAMERA DISTANCE", "camera_distance", 5, v_camdist, l_camdist, 0, apply_nothing },
    /* off by default (maintainer 2026-11-27: a changed distance made the camera correct itself all the time) */
    [OPT_MANUAL_CAMERA]  = { "MANUAL CAMERA", "manual_camera", 2, v_onoff, l_onoff, 0, apply_nothing },
    [OPT_MINIMAP]        = { "MINIMAP", "minimap", 2, v_onoff, l_onoff, 1, apply_nothing },
    [OPT_MINIMAP_SIZE]   = { "SIZE", "minimap_size", 3, v_size, l_size, 0, apply_nothing },   /* SMALL (maintainer) */
    [OPT_MINIMAP_ZOOM]   = { "ZOOM", "minimap_zoom", 3, v_zoom, l_zoom, 0, apply_nothing },   /* NEAR */
    /* defaults = the maintainer's own settings (2026-11-27): 2.5 % from the left, 97.5 % down */
    [OPT_MINIMAP_X]      = { "POSITION X", "minimap_x", 41, v_pct, l_pct, 1, apply_nothing },
    [OPT_MINIMAP_Y]      = { "POSITION Y", "minimap_y", 41, v_pct, l_pct, 39, apply_nothing },
    [OPT_PAUSE_MAP]      = { "PAUSE MAP", "minimap_pause_map", 2, v_onoff, l_onoff, 1, apply_nothing },
    [OPT_NAV_ARROWS]     = { "NAVIGATION ARROWS", "minimap_arrows", 3, v_arrows, l_arrows, 1, apply_nothing },
    [OPT_BLIP_MISSIONS]  = { "MISSIONS", "minimap_missions", 2, v_onoff, l_onoff, 1, apply_nothing },
    [OPT_BLIP_PLACES]    = { "CHURCHES + GARAGES", "minimap_places", 2, v_onoff, l_onoff, 1, apply_nothing },
    /* TOKENS, POWER-UPS, KILL FRENZY, POLICE off by default (maintainer's own settings, 2026-11-27) */
    [OPT_BLIP_TOKENS]    = { "TOKENS", "minimap_tokens", 2, v_onoff, l_onoff, 0, apply_nothing },
    [OPT_BLIP_WEAPONS]   = { "WEAPONS", "minimap_weapons", 2, v_onoff, l_onoff, 0, apply_nothing },
    [OPT_BLIP_BONUSES]   = { "POWER-UPS", "minimap_powerups", 2, v_onoff, l_onoff, 0, apply_nothing },
    [OPT_BLIP_FRENZY]    = { "KILL FRENZY", "minimap_frenzy", 2, v_onoff, l_onoff, 0, apply_nothing },
    [OPT_BLIP_POLICE]    = { "POLICE", "minimap_police", 2, v_onoff, l_onoff, 0, apply_nothing },
    /* ENHANCEMENTS (maintainer 2026-11-27): off by default - the original picture */
    [OPT_DAY_NIGHT]      = { "CYCLE", "day_night", 2, v_onoff, l_onoff, 0, apply_nothing },   /* DAY / NIGHT page */
    [OPT_DAY_PRESET]     = { "PRESET", "day_preset", 4, v_dpreset, l_dpreset, 0, apply_day_preset },
    /* numeric (values NULL): the choice is the number of seconds, shown M:SS */
    [OPT_DAY_SECS]       = { "DAY", "day_secs", DN_SECS_MAX + 1, NULL, NULL, 540, apply_day_secs },
    [OPT_SUNSET_SECS]    = { "SUNSET", "sunset_secs", DN_SECS_MAX + 1, NULL, NULL, 240, apply_day_secs },
    [OPT_NIGHT_SECS]     = { "NIGHT", "night_secs", DN_SECS_MAX + 1, NULL, NULL, 450, apply_day_secs },
    [OPT_SUNRISE_SECS]   = { "SUNRISE", "sunrise_secs", DN_SECS_MAX + 1, NULL, NULL, 210, apply_day_secs },
    /* issue #6 (maintainer 2026-11-28: an ENHANCEMENTS switch - purists do not need it): off = church saves only */
    [OPT_SAVE_ANYWHERE]  = { "SAVE ANYWHERE", "save_anywhere", 2, v_onoff, l_onoff, 0, apply_nothing },
    /* PR #7 (maverick-hunter) as ENHANCEMENTS switches (maintainer 2026-11-28), off by default (the original); each
     * acts in single player only (its writes are local: in a network game they would desync the others) */
    [OPT_ANALOGUE_STEERING] = { "ANALOGUE STEERING", "analogue_steering", 2, v_onoff, l_onoff, 0, apply_nothing },
    [OPT_PUNCH]          = { "PUNCH", "punch", 2, v_onoff, l_onoff, 0, apply_nothing },
    [OPT_CAMERA_PAN]     = { "CAMERA PAN", "camera_pan", 2, v_onoff, l_onoff, 0, apply_nothing },
    [OPT_STEER_PRESET]   = { "PRESET", "steer_preset", 4, v_spreset, l_spreset, 0, apply_steer_preset },
    [OPT_STEER_DEAD]     = { "DEAD ZONE", "steer_dead", 8, v_sdead, v_sdead, 3, apply_steer_value },
    [OPT_STEER_CURVE]    = { "CURVE", "steer_curve", 8, v_scurve, v_scurve, 3, apply_steer_value },
    [OPT_STEER_HOLD]     = { "SMOOTHING", "steer_hold", 4, v_shold, v_shold, 1, apply_steer_value },
    [OPT_TURRET_AIM]     = { "TURRET AIM", "turret_aim", 2, v_onoff, l_onoff, 0, apply_nothing },   /* off: the original */
    [OPT_ENDLESS_SPRINT] = { "ENDLESS SPRINTING", "cheat_endless_sprint", 2, v_onoff, l_onoff, 0, apply_nothing },
    /* 1: 2.5 s / refill after 2.5 s / full in 8 s (maintainer, default); 2: 3.5 / 0.5 / 3 (PR #7) */
    [OPT_SPRINT_PRESET]  = { "PRESET", "sprint_preset", 2, v_spreset2, l_spreset2, 0, apply_nothing },
    [OPT_SPRINT_BAR]     = { "SPRINT BAR", "sprint_bar", 2, v_onoff, l_onoff, 0, apply_nothing },   /* off (maintainer 2026-11-28) */
    /* defaults = the maintainer's own place (2026-11-28): 52.5 % across, 10 % down */
    [OPT_SPRINT_BAR_X]   = { "POSITION X", "sprint_bar_x", 41, v_pct, l_pct, 21, apply_nothing },
    [OPT_SPRINT_BAR_Y]   = { "POSITION Y", "sprint_bar_y", 41, v_pct, l_pct, 4, apply_nothing },
};

static int find_choice(int id, const char *v)
{
    if (!v)
        return g_def[id].def;
    if (!g_def[id].values) {   /* numeric */
        char *end;
        long n = strtol(v, &end, 10);
        return end != v && n >= 0 && n < g_def[id].n ? (int)n : g_def[id].def;
    }
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
    /* DAY / NIGHT: the DAY LENGTH of before (24 / 48 / 96) becomes PRESET 1 / 2 / 3; a preset sets the four lengths */
    if (!wr_settings_get("day_preset", NULL)) {
        const char *old = wr_settings_get("day_length", NULL);
        g_val[OPT_DAY_PRESET] = old && !strcmp(old, "48") ? 1 : old && !strcmp(old, "96") ? 2 : 0;
    }
    if (g_val[OPT_DAY_PRESET] < 3)
        for (int i = 0; i < 4; i++)
            g_val[OPT_DAY_SECS + i] = k_dpreset[g_val[OPT_DAY_PRESET]][i];
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
    if (!g_def[id].labels) {   /* numeric: seconds as M:SS (a few buffers: callers print it at once) */
        static char buf[4][16];
        static int k;
        char *b = buf[k++ & 3];
        snprintf(b, 16, "%d:%02d", c / 60, c % 60);
        return b;
    }
    return g_def[id].labels[c];
}

int opt_is_numeric(int id) { return id >= 0 && id < OPT_COUNT && !g_def[id].values; }

void opt_set(int id, int c)
{
    if (id < 0 || id >= OPT_COUNT || c < 0 || c >= g_def[id].n || g_val[id] == c)
        return;
    g_val[id] = c;
    if (g_def[id].key && !g_def[id].values) {
        char v[16];
        snprintf(v, sizeof v, "%d", c);
        wr_settings_set(g_def[id].key, v);
    } else if (g_def[id].key) {
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
