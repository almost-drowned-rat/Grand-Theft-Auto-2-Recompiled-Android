/*
 * GTA 2 Recompiled - DAY / NIGHT cycle (ENHANCEMENTS; maintainer 2026-11-27, ahead of the ray tracing).
 *
 * The game already lights its world on the CPU: every world frame it sets the level's / script's ambient
 * (gbh_SetAmbient(sub_00401B10()) at 0x00472132, only while Screen\lighting is on - the default) and adds the
 * visible point lights (street lamps ...; gbh_AddLight 0x004614CA); docs/re/gbh_api.md 1.4. The cycle only
 * scales and tints that ambient over the time of day - at night the game's own lights carry the picture.
 *
 * The clock runs on the game's logic ticks (30 per second, standing still in pause and menus) and goes on
 * across levels; it starts at 08:00. The DAY / NIGHT page sets each phase's real length (gta2_daynight_hour).
 * OFF (default): nothing changes, the original lighting path to the bit.
 */
#include "gta2_daynight.h"
#include "gta2_options.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

uint32_t gta2_game_ticks(void);

#define DN_START_HOUR 8.0

/* time of day -> the ambient; between two keys linear (hour, d, m, r, g, b):
 *   base = level's ambient raised toward full light by d (0 = the level's own, 1 = 255),
 *   ambient = base * m * tint (r, g, b) */
typedef struct { float h, d, m, r, g, b; } dnkey;
static const dnkey k_keys[] = {
    /* three times of day (maintainer 2026-11-27): DAY, SUNRISE / SUNSET (held for a while, not only a fade), NIGHT.
     * DAY is fully lit (maintainer: "remove the shadow in the day") - the game's own ambient 1.0 path, no dim areas
     * between the street lamps' light pools; levels are often dim by design (0.3 in the first district) */
    { 0.0f, 0.0f, 0.50f, 0.60f, 0.72f, 1.00f },    /* night: dark blue, the lamps light the streets */
    { 4.5f, 0.0f, 0.50f, 0.60f, 0.72f, 1.00f },
    { 6.0f, 0.5f, 0.90f, 1.00f, 0.82f, 0.78f },    /* sunrise: pink */
    { 7.0f, 0.5f, 0.90f, 1.00f, 0.82f, 0.78f },
    { 8.0f, 1.0f, 1.00f, 1.00f, 1.00f, 1.00f },    /* day: full light */
    { 17.0f, 1.0f, 1.00f, 1.00f, 1.00f, 1.00f },
    { 18.0f, 0.5f, 0.90f, 1.00f, 0.75f, 0.55f },   /* sunset: orange */
    { 19.5f, 0.5f, 0.90f, 1.00f, 0.75f, 0.55f },
    { 21.0f, 0.0f, 0.50f, 0.60f, 0.72f, 1.00f },
    { 24.0f, 0.0f, 0.50f, 0.60f, 0.72f, 1.00f },
};

/* The four phases (maintainer 2026-11-28: DAY / NIGHT page, each one's length in real seconds) and the hours of the
 * keys above each one covers: DAY 08:00-17:00, SUNSET 17:00-21:00 (fade, held orange, fade), NIGHT 21:00-04:30,
 * SUNRISE 04:30-08:00. The clock walks the phases in that order, each at its own speed; a phase of 0 s is skipped
 * (only DAY > 0: always day). */
static const double k_ph_from[4] = { 8.0, 17.0, 21.0, 4.5 }, k_ph_len[4] = { 9.0, 4.0, 7.5, 3.5 };

static void phase_secs(double s[4])
{
    for (int i = 0; i < 4; i++)
        s[i] = (double)opt_get(OPT_DAY_SECS + i);
    static double test_minutes = -1;
    if (test_minutes < 0) {   /* GTA2_DAYNIGHT_MINUTES=<m>: a test day of m real minutes (e.g. 1), the same shares */
        const char *e = getenv("GTA2_DAYNIGHT_MINUTES");
        test_minutes = e && *e && atof(e) > 0 ? atof(e) : 0;
    }
    if (test_minutes > 0) {
        double sum = s[0] + s[1] + s[2] + s[3];
        for (int i = 0; i < 4; i++)
            s[i] = sum > 0 ? s[i] / sum * test_minutes * 60.0 : k_ph_len[i] / 24.0 * test_minutes * 60.0;
    }
}

/* the real seconds from the start of DAY to the hour h (GTA2_DAYNIGHT_START) */
static double secs_of_hour(double h, const double s[4])
{
    double at = 0;
    for (int i = 0; i < 4; i++) {
        double into = fmod(h - k_ph_from[i] + 24.0, 24.0);
        if (into < k_ph_len[i])
            return at + into / k_ph_len[i] * s[i];
        at += s[i];
    }
    return 0;
}

double gta2_daynight_hour(void)
{
    double s[4];
    phase_secs(s);
    double cycle = s[0] + s[1] + s[2] + s[3];
    if (cycle <= 0)
        return 12.0;   /* every phase 0: day */
    static double start_h = -1;
    if (start_h < 0) {   /* GTA2_DAYNIGHT_START=<hour>: tests / screenshots at a fixed time of day */
        const char *e = getenv("GTA2_DAYNIGHT_START");
        start_h = e && *e ? fmod(fabs(atof(e)), 24.0) : DN_START_HOUR;
    }
    /* the game's logic ticks (30 a second; still in pause and menus) */
    double t = fmod(secs_of_hour(start_h, s) + (double)gta2_game_ticks() / 30.0, cycle);
    for (int i = 0; i < 4; i++) {
        if (t < s[i])
            return fmod(k_ph_from[i] + t / s[i] * k_ph_len[i], 24.0);
        t -= s[i];
    }
    return DN_START_HOUR;
}

/* Headlights (gta2_manual.c sub_004BE060): off in the DAY, the same span as the untinted keys (08:00-17:00), so
 * they come on as the sunset starts and go off when the sunrise has faded. A switch costs one texture rebuild per
 * visible car (the game's delta cache), once - never flickers, the hour only moves forward. */
int gta2_daynight_lights_off(void)
{
    if (!opt_get(OPT_DAY_NIGHT))
        return 0;
    double h = gta2_daynight_hour();
    return h >= 8.0 && h < 17.0;
}

/* Full NIGHT (21:00-04:30, between the sunset and the sunrise): the U-Jerk Truck's extra lamps glow only then
 * (maintainer 2026-11-27: sunrise / sunset only the two middle lamps, as the original) */
int gta2_daynight_full_night(void)
{
    if (!opt_get(OPT_DAY_NIGHT))
        return 0;
    double h = gta2_daynight_hour();
    return h >= 21.0 || h < 4.5;
}

int gta2_daynight_ambient(float game_ambient, float out[3])
{
    if (!opt_get(OPT_DAY_NIGHT))
        return 0;
    float h = (float)gta2_daynight_hour();
    int i = 0;
    while (i + 2 < (int)(sizeof k_keys / sizeof k_keys[0]) && h >= k_keys[i + 1].h)
        i++;
    const dnkey *a = &k_keys[i], *b = &k_keys[i + 1];
    float t = b->h > a->h ? (h - a->h) / (b->h - a->h) : 0.0f;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float d = a->d + (b->d - a->d) * t, m = a->m + (b->m - a->m) * t;
    float base = (game_ambient + (255.0f - game_ambient) * d) * m;
    out[0] = base * (a->r + (b->r - a->r) * t);
    out[1] = base * (a->g + (b->g - a->g) * t);
    out[2] = base * (a->b + (b->b - a->b) * t);
    /* full light too goes through the lighting (ambient 255 -> every vertex full): the game's own ambient 1.0 path
     * (no lighting, vertex = its shade) turned faces with shade 0 - some roofs - black (maintainer's screenshots) */
    return 1;
}
