/*
 * GTA 2 Recompiled - RECOMP OPTIONS as data.
 *
 * Every option is a list of choices kept in saves/settings.ini (GTA2_<KEY>
 * overrides one run). The menu pages only page through this table:
 * opt_count / opt_choice_label / opt_get / opt_set. opt_set stores the
 * choice and applies it at once (where the game allows; ASPECT RATIO's
 * gameplay width applies when the next game starts).
 */
#ifndef GTA2_OPTIONS_H
#define GTA2_OPTIONS_H

enum {
    OPT_DISPLAY_MODE,      /* WINDOWED / BORDERLESS / FULLSCREEN (hidden on Android) */
    OPT_RENDER_RES,        /* NATIVE / 480P ... 2160P */
    OPT_ASPECT,            /* 4:3 / 16:9 / 16:10 / 21:9 */
    OPT_TEXTURE_FILTER,    /* ORIGINAL / NEAREST / BILINEAR */
    OPT_FRAME_RATE,        /* 30 / 60 INTERPOLATED / 120 INTERPOLATED (M4b) */
    OPT_VSYNC,             /* OFF / ON */
    OPT_PERF_FPS,          /* OFF / ON  \                               */
    OPT_PERF_FRAMETIME,    /* OFF / ON   } stored together: performance_overlay */
    OPT_PERF_SPEED,        /* OFF / ON  /                               */
    OPT_RADIO_HALF_RATE,   /* OFF / ON (stereo radio stream rate, see docs/re/audio.md) */
    OPT_CONTROL_SCHEME,    /* CLASSIC / MODERN / CUSTOM (controller); PR #7's ANALOGUE: the switches further down */
    OPT_RUMBLE,            /* OFF / ON: explosions rumble the controller (M4c) */
    OPT_KEY_SCHEME,        /* CLASSIC / MODERN (keyboard only, as a controller) / KEYBOARD + MOUSE */
    OPT_PEDESTRIANS,       /* 1X / 2X / 3X / 4X (from the next game) */
    OPT_TRAFFIC,           /* 1X / 2X / 3X / 4X */
    OPT_SPRINT,            /* OFF / ON: the stamina sprint, every scheme (ENHANCEMENTS > SPRINT page; maintainer 2026-11-28) */
    OPT_MORE_BLOOD,        /* OFF / ON: blood stays longer, dries, and the bodies stay with it */
    OPT_CAMERA_DISTANCE,   /* 1.0X .. 2.0X: the view further out (issue #2; docs/re/camera_distance.md) */
    OPT_MANUAL_CAMERA,     /* OFF / ON: the CAMERA / ZOOM IN / ZOOM OUT buttons step CAMERA DISTANCE in play */
    /* GAMEPLAY > MINIMAP (maintainer 2026-11-27: one page, an ON/OFF switch per element) */
    OPT_MINIMAP,           /* OFF / ON */
    OPT_MINIMAP_SIZE,      /* SMALL / MEDIUM / LARGE */
    OPT_MINIMAP_ZOOM,      /* NEAR / MEDIUM / FAR */
    OPT_MINIMAP_X,         /* 0% .. 100% in 2.5% steps: left .. right (maintainer: free position) */
    OPT_MINIMAP_Y,         /* 0% .. 100%: top .. bottom */
    OPT_PAUSE_MAP,         /* OFF / ON: MAP line in the pause menu, the whole district */
    OPT_NAV_ARROWS,        /* OFF / ON / NEAR TARGET: the game's own arrows */
    OPT_BLIP_MISSIONS,     /* OFF / ON: phones + mission targets */
    OPT_BLIP_PLACES,       /* OFF / ON: churches (save) + garages */
    OPT_BLIP_TOKENS,       /* OFF / ON */
    OPT_BLIP_WEAPONS,      /* OFF / ON */
    OPT_BLIP_BONUSES,      /* OFF / ON: power-ups */
    OPT_BLIP_FRENZY,       /* OFF / ON: kill frenzies, also on cars */
    OPT_BLIP_POLICE,       /* OFF / ON: police / law cars (maintainer 2026-11-27) */
    OPT_DAY_NIGHT,         /* OFF / ON: the DAY / NIGHT cycle (gta2_daynight.c) */
    /* ENHANCEMENTS > DAY / NIGHT page (maintainer 2026-11-28): PRESET 1 / 2 / 3 = 24 / 48 / 96 real minutes a day,
     * CUSTOM = the four phases' own lengths; the four are numeric: choice = seconds (0 = the phase is skipped) */
    OPT_DAY_PRESET,
    OPT_DAY_SECS, OPT_SUNSET_SECS, OPT_NIGHT_SECS, OPT_SUNRISE_SECS,
    OPT_SAVE_ANYWHERE,     /* OFF / ON: SAVE GAME in the pause menu, LOAD GAME in the main menu (issue #6) */
    /* PR #7 (maverick-hunter) split into switches (maintainer 2026-11-28); analogue walking (the stick) and analogue
     * pedals (gas / brake mapped to R2 / L2) need none */
    OPT_ANALOGUE_STEERING, /* OFF / ON: the car steers with the left stick's push (ENHANCEMENTS > ANALOGUE STEERING) */
    OPT_PUNCH,             /* OFF / ON: punch with the weapon put away (MODERN / KEYBOARD CUSTOM: X by default) */
    OPT_CAMERA_PAN,        /* OFF / ON: SPECIAL 2 held + the right stick pans the view */
    OPT_STEER_PRESET,      /* SMOOTH / SNAPPY / HEAVY / CUSTOM: sets the three numbers below; changing one of them makes it match or CUSTOM */
    OPT_STEER_DEAD,        /* ANALOGUE car steering: stick dead zone 0.04 .. 0.20 (default 0.10) */
    OPT_STEER_CURVE,       /* curve 1.0 .. 3.0, higher = finer around the middle (default 1.6) */
    OPT_STEER_HOLD,        /* smoothing: ticks one steering pulse is held, 1 .. 4 (default 2) */
    OPT_TURRET_AIM,        /* OFF / ON: in a turret vehicle the right stick points the turret and fires (ENHANCEMENTS) */
    OPT_ENDLESS_SPRINT,    /* OFF / ON cheat: the sprint never runs out (gta2_state.c) */
    /* ENHANCEMENTS > SPRINT page (maintainer 2026-11-28: as the minimap - SPRINT, PRESET, SPRINT BAR, its position) */
    OPT_SPRINT_PRESET,     /* PRESET 1 / PRESET 2: the stamina sprint's numbers (1 the maintainer's, 2 PR #7's) */
    OPT_SPRINT_BAR,        /* OFF / ON: draw the sprint stamina bar on foot (gta2_state.c) */
    OPT_SPRINT_BAR_X,      /* 0% .. 100% in 2.5% steps: the bar's middle, left .. right (default 52.5%) */
    OPT_SPRINT_BAR_Y,      /* 0% .. 100%: the bar's top, top .. bottom (default 10%) */
    OPT_COUNT
};

void        opt_load_all(void);               /* at start, before the window opens */
const char *opt_label(int id);                /* the line's name, e.g. "ASPECT RATIO" */
int         opt_count(int id);                /* number of choices */
const char *opt_choice_label(int id, int c);  /* e.g. "16:9" */
int         opt_get(int id);
int         opt_is_numeric(int id);           /* choice = a number (DAY / NIGHT's seconds) */

/* OPT_CONTROL_SCHEME values (CLASSIC / MODERN / CUSTOM). PR #7's ANALOGUE scheme (maverick-hunter) is split into the
 * switches above (maintainer 2026-11-28): each scheme keeps its own buttons. */
enum { SCHEME_CLASSIC, SCHEME_MODERN, SCHEME_CUSTOM };
#define SCHEME_IS(v) (opt_get(OPT_CONTROL_SCHEME) == (v))
#define SCHEME_MODERNISH() SCHEME_IS(SCHEME_MODERN)
int         opt_default(int id);
void        opt_set(int id, int c);           /* store + apply */
int         opt_visible(int id);              /* 0: hidden on this platform */

/* Values the renderer reads */
double opt_aspect_ratio(void);                /* the chosen width / height */
int    opt_render_height(void);               /* 0 = NATIVE (cover the window) */
int    opt_texture_filter(void);              /* -1 original, 0 nearest, 1 bilinear */
int    opt_display_mode(void);                /* 0 windowed, 1 borderless, 2 fullscreen */

#endif
