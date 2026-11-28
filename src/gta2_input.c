/*
 * GTA 2 Recompiled - controller mapping.
 *
 * Pad controls map to GTA 2's twelve control actions; the key each action
 * produces is the one the game's own control settings assign to it
 * (in the menus: A confirm, B back, see map_menu)
 * (registry Control\0..11, DirectInput key codes), so a player who rebinds
 * the keyboard keeps a working pad. START is Escape (pause / menu back).
 * BUTTON MAPPING in the RECOMP menu (M6) will make this table editable.
 */
#include "wr.h"
#include "platform/wr_platform.h"
#include "gta2_options.h"
#include "gta2_state.h"
#include "gta2_menu.h"
#include "gta2_input.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int wr_reg_get_dword(const char *key, const char *name, uint32_t *out);

extern uint32_t g_gta2_frontend_ms;   /* gta2_manual.c: the frontend loop last ran */

enum { ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_ATTACK, ACT_ENTER, ACT_JUMP, ACT_PREV, ACT_NEXT,
       ACT_SPECIAL, ACT_SPECIAL2, ACT_STATUS, ACT_N, ACT_ESCAPE = 100, ACT_RADIO_NEXT, ACT_RADIO_PREV, ACT_SPRINT,
       /* the camera (maintainer 2026-11-27): CAMERA and ZOOM OUT step CAMERA DISTANCE out, ZOOM IN in, each
        * going round at the end - one button reaches every step */
       ACT_CAMERA, ACT_ZOOM_IN, ACT_ZOOM_OUT,
       /* ANALOGUE: attack with the weapon put away (gta2_punch_pressed) */
       ACT_PUNCH };

static const uint8_t k_default_dik[ACT_N] = { 0xC8, 0xD0, 0xCB, 0xCD, 0x1D, 0x1C, 0x39, 0x2C, 0x2D, 0x0F, 0x38, 0x36 };

static int pad_action(int c)
{
    switch (c) {
    case WR_PAD_DPAD_UP: case WR_PAD_LEFT_STICK_UP: return ACT_UP;
    case WR_PAD_DPAD_DOWN: case WR_PAD_LEFT_STICK_DOWN: return ACT_DOWN;
    case WR_PAD_DPAD_LEFT: case WR_PAD_LEFT_STICK_LEFT: return ACT_LEFT;
    case WR_PAD_DPAD_RIGHT: case WR_PAD_LEFT_STICK_RIGHT: return ACT_RIGHT;
    case WR_PAD_WEST: case WR_PAD_RT: return ACT_ATTACK;
    case WR_PAD_SOUTH: return ACT_ENTER;
    case WR_PAD_EAST: return ACT_JUMP;
    case WR_PAD_LB: return ACT_PREV;
    case WR_PAD_RB: return ACT_NEXT;
    case WR_PAD_NORTH: return ACT_SPECIAL;
    case WR_PAD_LT: return ACT_SPECIAL2;
    case WR_PAD_RSTICK: return ACT_STATUS;   /* special 3 on R3 (maintainer 2026-11-26) */
    /* the radio (it changes only in a car): right stick left / right (maintainer 2026-11-26) */
    case WR_PAD_RIGHT_STICK_LEFT: return ACT_RADIO_PREV;
    case WR_PAD_RIGHT_STICK_RIGHT: return ACT_RADIO_NEXT;
    case WR_PAD_START: return ACT_ESCAPE;
    default: return -1;
    }
}


/* In the menus (the frontend, and the port's own pages): A confirms, B goes
 * back, d-pad / left stick move, START confirms. The menus read these keys
 * directly (arrows, Return, Escape), not the control settings. */
static uint8_t map_menu(int control)
{
    switch (control) {
    case WR_PAD_DPAD_UP: case WR_PAD_LEFT_STICK_UP: return 0xC8;
    case WR_PAD_DPAD_DOWN: case WR_PAD_LEFT_STICK_DOWN: return 0xD0;
    case WR_PAD_DPAD_LEFT: case WR_PAD_LEFT_STICK_LEFT: return 0xCB;
    case WR_PAD_DPAD_RIGHT: case WR_PAD_LEFT_STICK_RIGHT: return 0xCD;
    case WR_PAD_SOUTH: case WR_PAD_START: return 0x1C;   /* Return */
    case WR_PAD_EAST: return 0x01;                       /* Escape */
    case WR_PAD_NORTH: return 0xC7;                      /* Home: defaults on the port's pages */
    default: return 0;
    }
}

/* ---- MODERN (maintainer 2026-11-26) -----------------------------------------------------
 *            on foot                       in a car
 * stick and  walk where it points          steer (left / right)
 * d-pad      (8 directions on the d-pad)
 * R-stick    turn / aim                    left / right: radio previous / next
 * A          sprint (1.5 s, every 10 s)      gas
 * X          attack                        brake / reverse
 * B          jump                          handbrake
 * L3         special 3                     special 3
 * R3         special 1                     special 1
 * R2         attack                        attack
 * L2         special 2                     special 1
 * Y          enter                         exit
 * LB / RB    previous / next weapon
 * Select special 2, START the menu
 * The stick's direction on foot is set in the on-foot control step
 * (gta2_modern_on_foot, wrapper of sub_004A5C50); this table only presses
 * "forward" while the stick is pushed. */
static int modern_action(int c, int driving)
{
    switch (c) {
    /* stick and d-pad alike (maintainer 2026-11-26) */
    case WR_PAD_LEFT_STICK_UP: case WR_PAD_LEFT_STICK_DOWN:
        return driving ? -1 : ACT_UP;
    case WR_PAD_DPAD_UP: case WR_PAD_DPAD_DOWN: return driving ? -1 : ACT_UP;
    /* right stick: aiming on foot (analogue, gta2_state.c), the radio in a car */
    case WR_PAD_RIGHT_STICK_LEFT: return driving ? ACT_RADIO_PREV : -1;
    case WR_PAD_RIGHT_STICK_RIGHT: return driving ? ACT_RADIO_NEXT : -1;
    case WR_PAD_RIGHT_STICK_UP: case WR_PAD_RIGHT_STICK_DOWN: return -1;
    case WR_PAD_LEFT_STICK_LEFT: case WR_PAD_DPAD_LEFT: return driving ? ACT_LEFT : ACT_UP;
    case WR_PAD_LEFT_STICK_RIGHT: case WR_PAD_DPAD_RIGHT: return driving ? ACT_RIGHT : ACT_UP;
    /* car buttons (maintainer 2026-11-26): A gas, X brake, B handbrake, R2 shoot */
    case WR_PAD_RT: return ACT_ATTACK;
    case WR_PAD_SOUTH: return driving ? ACT_UP : opt_get(OPT_SPRINT) ? -1 : ACT_JUMP;   /* on foot: SPRINT (or jump without it) */
    case WR_PAD_WEST: return driving ? ACT_DOWN : ACT_ATTACK;
    case WR_PAD_EAST: return ACT_JUMP;                   /* jump on foot, handbrake in a car */
    case WR_PAD_RSTICK: return ACT_SPECIAL;              /* special 1 on R3 */
    case WR_PAD_LSTICK: return ACT_STATUS;               /* special 3 on L3 */
    case WR_PAD_BACK: return ACT_SPECIAL2;               /* special 2 on Select */
    case WR_PAD_LT: return driving ? ACT_SPECIAL : ACT_SPECIAL2;
    case WR_PAD_NORTH: return ACT_ENTER;
    default: return pad_action(c);   /* LB/RB, Back, START as CLASSIC */
    }
}

/* ---- MODERN's own mapping (issue #3, maintainer 2026-11-27): CUSTOM MAPPING with CONTROLLER MODERN puts
 * each MODERN function on a button, ON FOOT and IN A CAR apart, plus the CAMERA ones (both). One function
 * once per situation: the double ones of the layout above went (X attack on foot - RT is attack; LT
 * special 2 / 1 - they are on Select / R3; LT is the CAMERA now). The sticks, the d-pad's walking /
 * steering and START stay; a function may go on the d-pad too. ZOOM IN / OUT are on no button at first.
 * settings.ini mbind_<slot> = control number, -1 none (the per-button modern_bind_<n> of before is unused). */
enum { MF_SPRINT, MF_ATTACK, MF_JUMP, MF_ENTER, MF_PREV, MF_NEXT, MF_SP1, MF_SP2, MF_SP3,
       MC_GAS, MC_BRAKE, MC_HB, MC_ATTACK, MC_EXIT, MC_PREV, MC_NEXT, MC_SP1, MC_SP2, MC_SP3,
       MK_CAMERA, MK_ZIN, MK_ZOUT,
       MF_PUNCH,   /* ENHANCEMENTS > PUNCH (PR #7; maintainer 2026-11-28): on foot, X by default; appended - the saved
                    * mbind_<n> of the slots before keep their numbers */
       MC_RADIO_NEXT, MC_RADIO_PREV,   /* PR #7's d-pad radio as a choice (maintainer 2026-11-28): unbound by default -
                                        * the right stick keeps the radio until a button is chosen */
       MS_N };
#define CTX_FOOT 1
#define CTX_CAR  2
static const struct { const char *label; int ctl, ctx, act; } k_mslot[MS_N] = {
    [MF_SPRINT] = { "SPRINT", WR_PAD_SOUTH, CTX_FOOT, ACT_SPRINT },
    [MF_ATTACK] = { "ATTACK", WR_PAD_RT, CTX_FOOT, ACT_ATTACK },
    [MF_JUMP] = { "JUMP", WR_PAD_EAST, CTX_FOOT, ACT_JUMP },
    [MF_ENTER] = { "ENTER VEHICLE", WR_PAD_NORTH, CTX_FOOT, ACT_ENTER },
    [MF_PREV] = { "PREVIOUS WEAPON", WR_PAD_LB, CTX_FOOT, ACT_PREV },
    [MF_NEXT] = { "NEXT WEAPON", WR_PAD_RB, CTX_FOOT, ACT_NEXT },
    [MF_SP1] = { "SPECIAL 1", WR_PAD_RSTICK, CTX_FOOT, ACT_SPECIAL },
    [MF_SP2] = { "SPECIAL 2", WR_PAD_BACK, CTX_FOOT, ACT_SPECIAL2 },
    [MF_SP3] = { "SPECIAL 3", WR_PAD_LSTICK, CTX_FOOT, ACT_STATUS },
    [MC_GAS] = { "GAS", WR_PAD_SOUTH, CTX_CAR, ACT_UP },
    [MC_BRAKE] = { "BRAKE / REVERSE", WR_PAD_WEST, CTX_CAR, ACT_DOWN },
    [MC_HB] = { "HANDBRAKE", WR_PAD_EAST, CTX_CAR, ACT_JUMP },
    [MC_ATTACK] = { "ATTACK", WR_PAD_RT, CTX_CAR, ACT_ATTACK },
    [MC_EXIT] = { "EXIT VEHICLE", WR_PAD_NORTH, CTX_CAR, ACT_ENTER },
    [MC_PREV] = { "PREVIOUS WEAPON", WR_PAD_LB, CTX_CAR, ACT_PREV },
    [MC_NEXT] = { "NEXT WEAPON", WR_PAD_RB, CTX_CAR, ACT_NEXT },
    [MC_SP1] = { "SPECIAL 1", WR_PAD_RSTICK, CTX_CAR, ACT_SPECIAL },
    [MC_SP2] = { "SPECIAL 2", WR_PAD_BACK, CTX_CAR, ACT_SPECIAL2 },
    [MC_SP3] = { "SPECIAL 3", WR_PAD_LSTICK, CTX_CAR, ACT_STATUS },
    [MK_CAMERA] = { "CAMERA", WR_PAD_LT, CTX_FOOT | CTX_CAR, ACT_CAMERA },
    [MK_ZIN] = { "ZOOM IN", -1, CTX_FOOT | CTX_CAR, ACT_ZOOM_IN },
    [MK_ZOUT] = { "ZOOM OUT", -1, CTX_FOOT | CTX_CAR, ACT_ZOOM_OUT },
    [MF_PUNCH] = { "PUNCH", WR_PAD_WEST, CTX_FOOT, ACT_PUNCH },   /* only with ENHANCEMENTS > PUNCH */
    [MC_RADIO_NEXT] = { "RADIO NEXT", -1, CTX_CAR, ACT_RADIO_NEXT },
    [MC_RADIO_PREV] = { "RADIO PREVIOUS", -1, CTX_CAR, ACT_RADIO_PREV },
};

static int is_dpad(int c)
{
    return c == WR_PAD_DPAD_UP || c == WR_PAD_DPAD_DOWN || c == WR_PAD_DPAD_LEFT || c == WR_PAD_DPAD_RIGHT;
}

/* MODERN's buttons a function may go on (sticks moved and START stay as they are) */
static int modern_button(int c)
{
    return c == WR_PAD_SOUTH || c == WR_PAD_EAST || c == WR_PAD_WEST || c == WR_PAD_NORTH || c == WR_PAD_LB ||
           c == WR_PAD_RB || c == WR_PAD_LT || c == WR_PAD_RT || c == WR_PAD_LSTICK || c == WR_PAD_RSTICK ||
           c == WR_PAD_BACK;
}
static int g_mbind[MS_N];   /* the button each function is on, -1 none */
static int g_mbind_loaded;

/* ---- PROFILE 1..3 (maintainer 2026-11-27): each keeps a whole mapping - MODERN's layout, CUSTOM's
 * controller bindings, the keyboard. settings.ini bind_profile; profile 1 keeps the names of before
 * (pad_bind_<n>, modern_bind_<n>, key_sprint), 2 and 3 add "p<N>_". The keyboard's live keys are the
 * game's own (registry Control\0..11): on a switch they are kept as p<N>_key_<n> and the new ones put in. */
/* + PROFILE STOCK (0, maintainer 2026-11-27): the port's own defaults, fixed - nothing is stored for it and
 * a binding is refused there. The menus' own buttons never change with any profile. */
#define N_PROFILES 3
static int g_profile = -1;   /* 0 STOCK, 1..3; -1 = not read yet */

static int profile(void)
{
    if (g_profile < 0) {
        g_profile = wr_settings_get_int("bind_profile", 1);
        if (g_profile < 0 || g_profile > N_PROFILES)
            g_profile = 1;
    }
    return g_profile;
}

static int stock(void) { return profile() == 0; }

static void pkey(char *buf, size_t cap, const char *base, int i)
{
    int p = profile();
    if (p == 1)
        snprintf(buf, cap, "%s_%d", base, i);
    else
        snprintf(buf, cap, "p%d_%s_%d", p, base, i);
}

static const char *sprint_key(void)
{
    static char b[24];
    if (profile() == 1)
        return "key_sprint";
    snprintf(b, sizeof b, "p%d_key_sprint", profile());
    return b;
}

static int is_rstick(int c)
{
    return c == WR_PAD_RIGHT_STICK_UP || c == WR_PAD_RIGHT_STICK_DOWN || c == WR_PAD_RIGHT_STICK_LEFT ||
           c == WR_PAD_RIGHT_STICK_RIGHT;
}
/* -1 = none; the right stick's four directions too (PR #7's vehicle fire as a choice, maintainer 2026-11-28) - but only
 * for IN A CAR functions (gta2_bind_set_pad): on foot the right stick aims */
static int mslot_takes(int c) { return c == -1 || modern_button(c) || is_dpad(c) || is_rstick(c); }

static void mbind_load(void)
{
    if (g_mbind_loaded)
        return;
    g_mbind_loaded = 1;
    for (int i = 0; i < MS_N; i++) {
        char key[24];
        pkey(key, sizeof key, "mbind", i);
        int c = stock() ? k_mslot[i].ctl : wr_settings_get_int(key, k_mslot[i].ctl);
        g_mbind[i] = mslot_takes(c) ? c : k_mslot[i].ctl;
    }
}

/* the MODERN function on button c in this situation (on foot / in a car), -1 none */
static int modern_slot(int c, int driving)
{
    if (c < 0)
        return -1;
    mbind_load();
    int ctx = driving ? CTX_CAR : CTX_FOOT;
    for (int i = 0; i < MS_N; i++)
        if (g_mbind[i] == c && (k_mslot[i].ctx & ctx))
            return i;
    return -1;
}

/* The PR's ANALOGUE layout (maverick-hunter) split into its parts (maintainer 2026-11-28) - every scheme keeps its own
 * buttons; on top, for every scheme (single player only), -2 = not changed:
 *   - analogue pedals: when gas is mapped to R2 / brake to L2 the trigger's press is left to gta2_feel_car_pre
 *   - CAMERA PAN: with SPECIAL 2 held (Select) the right stick is the four directions of the game's look-around
 *   - TURRET AIM: in a turret vehicle the right stick's left / right fire (gta2_turret_pre aims) */
static int extra_act(int c, int driving)
{
    if (wr_rd32(0x00673E2Cu))
        return -2;   /* a network game: the original */
    switch (c) {
    case WR_PAD_RT: return driving && gta2_gas_on_rt() ? -1 : -2;
    case WR_PAD_LT: return driving && gta2_brake_on_lt() ? -1 : -2;
    case WR_PAD_RIGHT_STICK_UP: return gta2_pan_held() ? ACT_UP : -2;
    case WR_PAD_RIGHT_STICK_DOWN: return gta2_pan_held() ? ACT_DOWN : -2;
    case WR_PAD_RIGHT_STICK_LEFT:
        return gta2_pan_held() ? ACT_LEFT : driving && gta2_turret_active() ? ACT_ATTACK : -2;
    case WR_PAD_RIGHT_STICK_RIGHT:
        return gta2_pan_held() ? ACT_RIGHT : driving && gta2_turret_active() ? ACT_ATTACK : -2;
    default: return -2;
    }
}

/* MODERN: the game action for control c - its function if one is on it, else the sticks / d-pad / START
 * of the layout above; a MODERN button without a function does nothing. ACT_SPRINT without SPRINT on is
 * a jump (as before); PUNCH without ENHANCEMENTS > PUNCH does nothing. */
static int modern_act(int c, int driving)
{
    int s = modern_slot(c, driving);
    if (s >= 0) {
        int a = k_mslot[s].act;
        if (a == ACT_PUNCH && !opt_get(OPT_PUNCH))
            return -1;
        return a == ACT_SPRINT && !opt_get(OPT_SPRINT) ? ACT_JUMP : a;
    }
    if (modern_button(c))
        return -1;
    return modern_action(c, driving);
}

static int modern_fn_button(int slot)   /* the button a MODERN function is on (-1 none) */
{
    mbind_load();
    return slot >= 0 && slot < MS_N ? g_mbind[slot] : -1;
}

static uint8_t g_dik[ACT_N];
static uint32_t g_dik_time;

/* ---- CUSTOM (maintainer 2026-11-26): every function on a button of the
 * player's choice, one button per function; the left stick always acts
 * as the d-pad. Kept in settings.ini as pad_bind_<n> = control number. The
 * keyboard's bindings are the game's own (registry Control\0..11). */
static const struct { const char *label; int act; } k_fn[GTA2_BIND_N] = {
    { "FORWARD", ACT_UP }, { "BACKWARD", ACT_DOWN }, { "LEFT", ACT_LEFT }, { "RIGHT", ACT_RIGHT },
    { "ATTACK", ACT_ATTACK }, { "ENTER / EXIT", ACT_ENTER }, { "JUMP / HANDBRAKE", ACT_JUMP },
    { "PREVIOUS WEAPON", ACT_PREV }, { "NEXT WEAPON", ACT_NEXT }, { "SPECIAL 1", ACT_SPECIAL },
    { "SPECIAL 2", ACT_SPECIAL2 }, { "SPECIAL 3", ACT_STATUS }, { "SPRINT", ACT_SPRINT },
    { "RADIO NEXT", ACT_RADIO_NEXT }, { "RADIO PREVIOUS", ACT_RADIO_PREV }, { "MENU", ACT_ESCAPE },
    { "CAMERA", ACT_CAMERA }, { "ZOOM IN", ACT_ZOOM_IN }, { "ZOOM OUT", ACT_ZOOM_OUT },
};
static const int k_bind_default[GTA2_BIND_N] = {
    WR_PAD_DPAD_UP, WR_PAD_DPAD_DOWN, WR_PAD_DPAD_LEFT, WR_PAD_DPAD_RIGHT, WR_PAD_WEST, WR_PAD_SOUTH,
    WR_PAD_EAST, WR_PAD_LB, WR_PAD_RB, WR_PAD_NORTH, WR_PAD_LT, WR_PAD_RSTICK, WR_PAD_LSTICK,
    WR_PAD_RIGHT_STICK_RIGHT, WR_PAD_RIGHT_STICK_LEFT, WR_PAD_START,
    -1, -1, -1,   /* the camera: on no button until the player wants it */
};
#define BIND_CAM0 16   /* CAMERA, ZOOM IN, ZOOM OUT in k_fn */
static int g_bind[GTA2_BIND_N];
static int g_bind_loaded;

static void bind_load(void)
{
    if (g_bind_loaded)
        return;
    g_bind_loaded = 1;
    for (int i = 0; i < GTA2_BIND_N; i++) {
        char key[24];
        pkey(key, sizeof key, "pad_bind", i);
        int c = stock() ? k_bind_default[i] : wr_settings_get_int(key, k_bind_default[i]);
        g_bind[i] = (c >= 0 && c < WR_PAD_N) || c == -1 ? c : k_bind_default[i];   /* -1: on no button */
    }
}

#define BIND_SPRINT 12      /* the same index on both devices */
#define KEY_SPRINT_DEFAULT 0x2A   /* Left Shift: unused by the game */

static int pad_modern(int keyboard) { return !keyboard && SCHEME_MODERNISH(); }

/* ---- KEYBOARD MODERN's own mapping (maintainer 2026-11-27: "the full custom mapping layout for the keyboard
 * too", the MODERN layout by situation): every MODERN function (k_mslot: ON FOOT / IN A CAR / CAMERA) on a key of
 * the player's choice, plus MOVE (walk where it points on foot, steer in a car), RADIO in a car and MENU. One key
 * per function per situation; on foot and in a car are apart. Independent of the CONTROLLER scheme (before, the
 * keyboard was a fixed controller - I/J/K/L ... - and CONTROLLER decided). settings.ini kbind_<slot> = DirectInput
 * key code, 0 none (profiles as the pad: p<N>_kbind_<slot>; STOCK = these defaults). In play the keys reach the
 * game through kmod_hook; the menus keep their own keys (arrows, Return, Escape). */
enum { KM_UP = MS_N, KM_DOWN, KM_LEFT, KM_RIGHT, KM_RADIO_NEXT, KM_RADIO_PREV, KM_MENU, KS_N };
static const uint8_t k_kslot_default[KS_N] = {
    [MF_SPRINT] = 0x25 /* K */, [MF_ATTACK] = 0x19 /* P */, [MF_JUMP] = 0x26 /* L */, [MF_ENTER] = 0x17 /* I */,
    [MF_PREV] = 0x16 /* U */, [MF_NEXT] = 0x18 /* O */, [MF_SP1] = 0x21 /* F */, [MF_SP2] = 0x22 /* G */,
    [MF_SP3] = 0x2E /* C */,
    [MC_GAS] = 0x25 /* K */, [MC_BRAKE] = 0x24 /* J */, [MC_HB] = 0x26 /* L */, [MC_ATTACK] = 0x19 /* P */,
    [MC_EXIT] = 0x17 /* I */, [MC_PREV] = 0x16 /* U */, [MC_NEXT] = 0x18 /* O */, [MC_SP1] = 0x21 /* F */,
    [MC_SP2] = 0x22 /* G */, [MC_SP3] = 0x2E /* C */,
    [MK_CAMERA] = 0x2C /* Z */, [MK_ZIN] = 0, [MK_ZOUT] = 0, [MF_PUNCH] = 0x2D /* X */,
    [KM_UP] = 0x11 /* W */, [KM_DOWN] = 0x1F /* S */, [KM_LEFT] = 0x1E /* A */, [KM_RIGHT] = 0x20 /* D */,
    [KM_RADIO_NEXT] = 0xCD /* Right */, [KM_RADIO_PREV] = 0xCB /* Left */, [KM_MENU] = 0x1C /* Return */,
};
static uint8_t g_kbind[KS_N];
static int g_kbind_loaded;

/* maintainer 2026-11-27: the presets CLASSIC / MODERN / KEYBOARD + MOUSE stay; the keyboard's CUSTOM MAPPING page
 * opens only this layout by situation, and it drives the game with KEYBOARD CUSTOM (a key set there switches to it) */
#define KEY_SCHEME_CUSTOM 3
static int kb_modern(int keyboard) { return keyboard != 0; }
static int kb_custom(void) { return opt_get(OPT_KEY_SCHEME) == KEY_SCHEME_CUSTOM; }

static const char *kslot_label(int i)
{
    static const char *const k[] = { "UP", "DOWN", "LEFT", "RIGHT", "RADIO NEXT", "RADIO PREVIOUS", "MENU" };
    return i >= 0 && i < MS_N ? k_mslot[i].label : i >= MS_N && i < KS_N ? k[i - MS_N] : "";
}

static int kslot_ctx(int i)
{
    if (i >= 0 && i < MS_N)
        return k_mslot[i].ctx;
    return i == KM_RADIO_NEXT || i == KM_RADIO_PREV ? CTX_CAR : CTX_FOOT | CTX_CAR;
}

static void kbind_load(void)
{
    if (g_kbind_loaded)
        return;
    g_kbind_loaded = 1;
    for (int i = 0; i < KS_N; i++) {
        char key[24];
        pkey(key, sizeof key, "kbind", i);
        int v = stock() ? k_kslot_default[i] : wr_settings_get_int(key, k_kslot_default[i]);
        g_kbind[i] = v >= 0 && v < 256 && v != 0x01 ? (uint8_t)v : k_kslot_default[i];   /* Esc: the pause */
    }
}

static int kdown(const uint8_t *kb, int slot)   /* a KEYBOARD MODERN slot's key held */
{
    kbind_load();
    return g_kbind[slot] && (kb[g_kbind[slot]] & 0x80);
}

/* the keyboard's lines: the 12 game actions, SPRINT, then CAMERA / ZOOM IN / ZOOM OUT */
#define KB_N (ACT_N + 4)
static int kb_fn(int i) { return i <= BIND_SPRINT ? i : BIND_CAM0 + (i - BIND_SPRINT - 1); }   /* -> k_fn */
static int kb_act(int i) { return i < ACT_N ? i : i == BIND_SPRINT ? BIND_SPRINT : k_fn[kb_fn(i)].act; }

int gta2_bind_count(int keyboard)
{
    return kb_modern(keyboard) ? KS_N : keyboard ? KB_N : pad_modern(0) ? MS_N : GTA2_BIND_N;
}
const char *gta2_bind_label(int keyboard, int i)
{
    if (kb_modern(keyboard))
        return kslot_label(i);
    if (pad_modern(keyboard))
        return i >= 0 && i < MS_N ? k_mslot[i].label : "";
    if (keyboard)
        return i >= 0 && i < KB_N ? k_fn[kb_fn(i)].label : "";
    return i >= 0 && i < GTA2_BIND_N ? k_fn[i].label : "";
}

static int is_cam_act(int a) { return a == ACT_CAMERA || a == ACT_ZOOM_IN || a == ACT_ZOOM_OUT; }

/* ---- CUSTOM MAPPING's lines in groups with a heading and an empty line between (maintainer 2026-11-27):
 * a row is a heading (head != NULL; "" = the empty line) or a binding index of gta2_bind_label / _value */
typedef struct { const char *head; int bind; } bind_row;
#define H(t) { t, -1 }
#define B(i) { NULL, i }
static const bind_row k_rows_modern[] = {
    H("ON FOOT"), B(MF_SPRINT), B(MF_ATTACK), B(MF_JUMP), B(MF_ENTER), B(MF_PREV), B(MF_NEXT),
    B(MF_SP1), B(MF_SP2), B(MF_SP3), B(MF_PUNCH), H(""),
    H("IN A CAR"), B(MC_GAS), B(MC_BRAKE), B(MC_HB), B(MC_ATTACK), B(MC_EXIT), B(MC_PREV), B(MC_NEXT),
    B(MC_SP1), B(MC_SP2), B(MC_SP3), B(MC_RADIO_NEXT), B(MC_RADIO_PREV), H(""),
    H("CAMERA"), B(MK_CAMERA), B(MK_ZIN), B(MK_ZOUT),
};
static const bind_row k_rows_custom[] = {   /* k_fn indices */
    H("MOVE"), B(0), B(1), B(2), B(3), H(""),
    H("ACTIONS"), B(4), B(5), B(6), B(12), H(""),
    H("WEAPONS"), B(7), B(8), H(""),
    H("SPECIAL"), B(9), B(10), B(11), H(""),
    H("RADIO + MENU"), B(13), B(14), B(15), H(""),
    H("CAMERA"), B(16), B(17), B(18),
};
static const bind_row k_rows_kmodern[] = {   /* KEYBOARD MODERN (kslot) */
    H("MOVE"), B(KM_UP), B(KM_DOWN), B(KM_LEFT), B(KM_RIGHT), H(""),
    H("ON FOOT"), B(MF_SPRINT), B(MF_ATTACK), B(MF_JUMP), B(MF_ENTER), B(MF_PREV), B(MF_NEXT),
    B(MF_SP1), B(MF_SP2), B(MF_SP3), B(MF_PUNCH), H(""),
    H("IN A CAR"), B(MC_GAS), B(MC_BRAKE), B(MC_HB), B(MC_ATTACK), B(MC_EXIT), B(MC_PREV), B(MC_NEXT),
    B(MC_SP1), B(MC_SP2), B(MC_SP3), B(KM_RADIO_NEXT), B(KM_RADIO_PREV), H(""),
    H("CAMERA"), B(MK_CAMERA), B(MK_ZIN), B(MK_ZOUT), H(""),
    H("GAME"), B(KM_MENU),
};
static const bind_row k_rows_keys[] = {     /* keyboard lines (kb_fn) */
    H("MOVE"), B(0), B(1), B(2), B(3), H(""),
    H("ACTIONS"), B(4), B(5), B(6), B(12), H(""),
    H("WEAPONS"), B(7), B(8), H(""),
    H("SPECIAL"), B(9), B(10), B(11), H(""),
    H("CAMERA"), B(13), B(14), B(15),
};
#undef H
#undef B

static const bind_row *bind_rows(int keyboard, int *n)
{
    if (kb_modern(keyboard)) {
        *n = (int)(sizeof k_rows_kmodern / sizeof k_rows_kmodern[0]);
        return k_rows_kmodern;
    }
    if (keyboard) {
        *n = (int)(sizeof k_rows_keys / sizeof k_rows_keys[0]);
        return k_rows_keys;
    }
    if (SCHEME_MODERNISH()) {
        *n = (int)(sizeof k_rows_modern / sizeof k_rows_modern[0]);
        return k_rows_modern;
    }
    *n = (int)(sizeof k_rows_custom / sizeof k_rows_custom[0]);
    return k_rows_custom;
}

int gta2_bind_rows(int keyboard)
{
    int n;
    bind_rows(keyboard, &n);
    return n;
}

const char *gta2_bind_row(int keyboard, int r, int *bind)
{
    int n;
    const bind_row *rows = bind_rows(keyboard, &n);
    *bind = -1;
    if (r < 0 || r >= n)
        return "";
    if (rows[r].head)
        return rows[r].head;
    *bind = rows[r].bind;
    return NULL;   /* a binding: label and value from gta2_bind_label / gta2_bind_value */
}

/* act: a game action 0..11, BIND_SPRINT or a camera action; 0 = no key */
static uint8_t key_of(int act)
{
    if (act == BIND_SPRINT) {   /* the port's own key: settings.ini (STOCK: the default) */
        int v = stock() ? KEY_SPRINT_DEFAULT : wr_settings_get_int(sprint_key(), KEY_SPRINT_DEFAULT);
        return v > 0 && v < 256 ? (uint8_t)v : KEY_SPRINT_DEFAULT;
    }
    if (is_cam_act(act)) {      /* the camera: settings.ini, none by default (STOCK: none) */
        char key[24];
        pkey(key, sizeof key, "key_cam", act - ACT_CAMERA);
        int v = stock() ? 0 : wr_settings_get_int(key, 0);
        return v > 0 && v < 256 ? (uint8_t)v : 0;
    }
    char name[8];
    uint32_t v;
    snprintf(name, sizeof name, "%d", act);
    return wr_reg_get_dword("HKEY_LOCAL_MACHINE\\SOFTWARE\\DMA Design Ltd\\GTA2\\Control", name, &v) && v < 256
               ? (uint8_t)v : k_default_dik[act];
}

void gta2_bind_value(int keyboard, int i, char *out, size_t cap)
{
    bind_load();
    int c = -1;
    if (kb_modern(keyboard)) {
        kbind_load();
        uint8_t k = i >= 0 && i < KS_N ? g_kbind[i] : 0;
        if (k)
            wr_key_name(k, out, cap);
        else
            snprintf(out, cap, "-");
        return;
    }
    if (keyboard) {
        uint8_t k = i >= 0 && i < KB_N ? key_of(kb_act(i)) : 0;
        if (k)
            wr_key_name(k, out, cap);
        else
            snprintf(out, cap, "-");
        return;
    }
    if (pad_modern(0)) {
        mbind_load();
        c = i >= 0 && i < MS_N ? g_mbind[i] : -1;
    } else
        c = i >= 0 && i < GTA2_BIND_N ? g_bind[i] : -1;
    snprintf(out, cap, "%s", c >= 0 ? wr_pad_control_name(c) : "-");
}

/* a pad control for function i; the function that had it gets i's old one. With CONTROLLER MODERN: MODERN's
 * slot i (only MODERN's buttons; a stick, the d-pad or START is refused). Returns 0 if refused. */
int gta2_bind_set_pad(int i, int control)
{
    char key[24];
    if (stock())
        return 0;   /* STOCK stays as it is */
    if (pad_modern(0)) {
        if (i < 0 || i >= MS_N || control < 0 || !mslot_takes(control))
            return 0;
        if (is_rstick(control) && k_mslot[i].ctx != CTX_CAR)
            return 0;   /* the right stick only for IN A CAR functions (on foot it aims) */
        mbind_load();
        /* the function that had this button in the same situation gets i's old one (on foot and in a
         * car are apart; the camera counts for both) */
        for (int j = 0; j < MS_N; j++)
            if (j != i && g_mbind[j] == control && (k_mslot[j].ctx & k_mslot[i].ctx)) {
                g_mbind[j] = g_mbind[i];
                pkey(key, sizeof key, "mbind", j);
                wr_settings_set_int(key, g_mbind[j]);
            }
        g_mbind[i] = control;
        pkey(key, sizeof key, "mbind", i);
        wr_settings_set_int(key, control);
        return 1;
    }
    bind_load();
    for (int j = 0; j < GTA2_BIND_N; j++)
        if (j != i && g_bind[j] == control) {
            g_bind[j] = g_bind[i];
            pkey(key, sizeof key, "pad_bind", j);
            wr_settings_set_int(key, g_bind[j]);
        }
    g_bind[i] = control;
    pkey(key, sizeof key, "pad_bind", i);
    wr_settings_set_int(key, control);
    return 1;
}

/* store a key for act (a game action: registry Control\<act>; SPRINT / camera: settings.ini) */
static void key_store(int act, uint8_t dik)
{
    char name[24];
    if (act == BIND_SPRINT) {
        wr_settings_set_int(sprint_key(), dik);
    } else if (is_cam_act(act)) {
        pkey(name, sizeof name, "key_cam", act - ACT_CAMERA);
        wr_settings_set_int(name, dik);
    } else {
        snprintf(name, sizeof name, "%d", act);
        wr_reg_set_dword("HKEY_LOCAL_MACHINE\\SOFTWARE\\DMA Design Ltd\\GTA2\\Control", name, dik);
    }
}

/* a key for keyboard line i (kb_act); the line that had it gets i's old one. The caller makes the game
 * read its controls again (sub_0044BF30). */
void gta2_bind_set_key(int i, uint8_t dik)
{
    if (stock())
        return;   /* STOCK stays as it is */
    if (kb_modern(1)) {   /* KEYBOARD MODERN: the slot that had this key in the same situation gets i's old one */
        if (i < 0 || i >= KS_N || !dik || dik == 0x01)
            return;
        kbind_load();
        char key[24];
        for (int j = 0; j < KS_N; j++)
            if (j != i && g_kbind[j] == dik && (kslot_ctx(j) & kslot_ctx(i))) {
                g_kbind[j] = g_kbind[i];
                pkey(key, sizeof key, "kbind", j);
                wr_settings_set_int(key, g_kbind[j]);
            }
        g_kbind[i] = dik;
        pkey(key, sizeof key, "kbind", i);
        wr_settings_set_int(key, dik);
        if (!kb_custom())
            opt_set(OPT_KEY_SCHEME, KEY_SCHEME_CUSTOM);   /* the change takes effect at once */
        return;
    }
    if (i < 0 || i >= KB_N)
        return;
    int act = kb_act(i);
    uint8_t old = key_of(act);
    for (int j = 0; j < KB_N; j++)
        if (j != i && key_of(kb_act(j)) == dik)
            key_store(kb_act(j), old);
    key_store(act, dik);
    g_dik_time = 0;   /* the pad follows at once */
}

void gta2_bind_defaults(int keyboard)
{
    if (stock())
        return;   /* STOCK is the defaults already */
    if (kb_modern(keyboard)) {
        for (int i = 0; i < KS_N; i++) {
            char key[24];
            pkey(key, sizeof key, "kbind", i);
            g_kbind[i] = k_kslot_default[i];
            wr_settings_set_int(key, g_kbind[i]);
        }
        g_kbind_loaded = 1;
        return;
    }
    if (keyboard) {
        const char *k = "HKEY_LOCAL_MACHINE\\SOFTWARE\\DMA Design Ltd\\GTA2\\Control";
        char name[8];
        for (int j = 0; j < ACT_N; j++) {
            snprintf(name, sizeof name, "%d", j);
            wr_reg_set_dword(k, name, k_default_dik[j]);
        }
        wr_settings_set_int(sprint_key(),KEY_SPRINT_DEFAULT);
        for (int a = ACT_CAMERA; a <= ACT_ZOOM_OUT; a++)
            key_store(a, 0);   /* the camera: on no key */
        g_dik_time = 0;
        return;
    }
    if (pad_modern(0)) {   /* MODERN's own layout */
        mbind_load();
        for (int i = 0; i < MS_N; i++) {
            char key[24];
            pkey(key, sizeof key, "mbind", i);
            g_mbind[i] = k_mslot[i].ctl;
            wr_settings_set_int(key, g_mbind[i]);
        }
        return;
    }
    bind_load();
    for (int i = 0; i < GTA2_BIND_N; i++) {
        char key[24];
        pkey(key, sizeof key, "pad_bind", i);
        g_bind[i] = k_bind_default[i];
        wr_settings_set_int(key, g_bind[i]);
    }
}

int gta2_bind_profile(void) { return profile(); }

/* PROFILE p (0 STOCK first, then 1..3; maintainer 2026-11-27): the keyboard's live keys are kept for the
 * old profile (not for STOCK) and the new one's put in - STOCK and a new profile get the game's defaults.
 * The caller makes the game read its controls again (sub_0044BF30). */
void gta2_bind_profile_set(int p)
{
    if (p < 0 || p > N_PROFILES || p == profile())
        return;
    const char *k = "HKEY_LOCAL_MACHINE\\SOFTWARE\\DMA Design Ltd\\GTA2\\Control";
    char name[8], key[24];
    if (!stock())
        for (int i = 0; i < ACT_N; i++) {   /* keep the old profile's keys */
            snprintf(key, sizeof key, "p%d_key_%d", g_profile, i);
            wr_settings_set_int(key, key_of(i));
        }
    g_profile = p;
    wr_settings_set_int("bind_profile", p);
    for (int i = 0; i < ACT_N; i++) {
        snprintf(name, sizeof name, "%d", i);
        snprintf(key, sizeof key, "p%d_key_%d", p, i);
        int v = p == 0 ? k_default_dik[i] : wr_settings_get_int(key, k_default_dik[i]);
        wr_reg_set_dword(k, name, (uint32_t)(v > 0 && v < 256 ? v : k_default_dik[i]));
    }
    g_bind_loaded = 0;
    g_mbind_loaded = 0;
    g_kbind_loaded = 0;
    g_dik_time = 0;
    wr_log("controls: profile %s", p ? (p == 1 ? "1" : p == 2 ? "2" : "3") : "STOCK");
}

static int custom_action(int c)
{
    bind_load();
    switch (c) {   /* the stick as the d-pad */
    case WR_PAD_LEFT_STICK_UP: c = WR_PAD_DPAD_UP; break;
    case WR_PAD_LEFT_STICK_DOWN: c = WR_PAD_DPAD_DOWN; break;
    case WR_PAD_LEFT_STICK_LEFT: c = WR_PAD_DPAD_LEFT; break;
    case WR_PAD_LEFT_STICK_RIGHT: c = WR_PAD_DPAD_RIGHT; break;
    default: break;
    }
    for (int i = 0; i < GTA2_BIND_N; i++)
        if (g_bind[i] == c)
            return k_fn[i].act;
    return -1;
}

static uint8_t map(int control)
{
    if (gta2_pause_open())   /* the pause menu: START closes it again (it opened it) */
        return control == WR_PAD_START ? 0x01 : map_menu(control);
    if (g_gta2_frontend_ms && wr_time_ms() - g_gta2_frontend_ms < 250)
        return map_menu(control);
    int driving = gta2_player_driving();
    /* MODERN: the player's own functions (modern_act); the camera buttons: gta2_camera_keys */
    int a = extra_act(control, driving);   /* analogue pedals, CAMERA PAN, TURRET AIM: every scheme */
    if (a == -2)
        a = SCHEME_MODERNISH() ? modern_act(control, driving) : SCHEME_IS(SCHEME_CUSTOM) ? custom_action(control) : pad_action(control);
    if (a < 0 || is_cam_act(a))
        return 0;
    if (a == ACT_ESCAPE)
        return 0x01;
    if (a == ACT_SPRINT)
        return 0;   /* read by the sprint itself (gta2_sprint_pressed) */
    if (a == ACT_PUNCH)
        a = ACT_ATTACK;   /* the game only knows "attack"; gta2_punch_swap makes it a punch (gta2_state.c) */
    if (a == ACT_RADIO_NEXT || a == ACT_RADIO_PREV)
        return a == ACT_RADIO_NEXT ? 0x3C : 0x3B;   /* F2 / F1 */
    uint32_t now = wr_time_ms();
    if (!g_dik_time || now - g_dik_time > 1000) {   /* follow the game's settings */
        g_dik_time = now;
        for (int i = 0; i < ACT_N; i++) {
            char name[8];
            uint32_t v;
            snprintf(name, sizeof name, "%d", i);
            g_dik[i] = wr_reg_get_dword("HKEY_LOCAL_MACHINE\\SOFTWARE\\DMA Design Ltd\\GTA2\\Control", name, &v) && v < 256
                           ? (uint8_t)v : k_default_dik[i];
        }
    }
    return g_dik[a];
}

void gta2_input_register(void) { wr_pad_set_map(map); }

/* the pad button of a camera function under the current CONTROLLER scheme, -1 none */
static int cam_pad_button(int act)
{
    if (SCHEME_MODERNISH()) {
        mbind_load();   /* (a brake on L2 moves CAMERA to the brake's old button: one function per button) */
        for (int i = 0; i < MS_N; i++)
            if (k_mslot[i].act == act)
                return g_mbind[i];
    } else if (SCHEME_IS(SCHEME_CUSTOM)) {
        bind_load();
        return g_bind[BIND_CAM0 + (act - ACT_CAMERA)];
    }
    return -1;
}

/* The camera buttons (maintainer 2026-11-27): CAMERA and ZOOM OUT one CAMERA DISTANCE step out, ZOOM IN one
 * in, each going round at the end, so one button alone reaches every step. Nothing is on them by default
 * except MODERN's LT (CAMERA). Once per frame from the HUD. */
void gta2_camera_keys(void)
{
    static int was[3];
    if (gta2_pause_open() || !opt_get(OPT_MANUAL_CAMERA)) {   /* MANUAL CAMERA off: the buttons do nothing */
        was[0] = was[1] = was[2] = 1;   /* a press held over the menu does not count when it closes */
        return;
    }
    uint8_t kb[256];
    wr_keys_keyboard_dik(kb);
    uint64_t pad = wr_pad_controls();
    int c = opt_get(OPT_CAMERA_DISTANCE), n = opt_count(OPT_CAMERA_DISTANCE);
    for (int k = 0; k < 3; k++) {
        int act = ACT_CAMERA + k, b = cam_pad_button(act);
        uint8_t key = key_of(act);
        int down = (key && (kb[key] & 0x80)) || (b >= 0 && (pad >> b & 1)) ||
                   (kb_custom() && kdown(kb, MK_CAMERA + k));   /* KEYBOARD CUSTOM */
        if (down && !was[k])
            c = act == ACT_ZOOM_IN ? (c + n - 1) % n : (c + 1) % n;
        was[k] = down;
    }
    if (c != opt_get(OPT_CAMERA_DISTANCE))
        opt_set(OPT_CAMERA_DISTANCE, c);
}

/* SPRINT held (GAMEPLAY SPRINT on): MODERN A, CLASSIC L3, CUSTOM its binding;
 * on the keyboard its key (Left Shift by default). */
int gta2_sprint_pressed(void)
{
    if (!opt_get(OPT_SPRINT))
        return 0;
    uint8_t kb[256];
    wr_keys_keyboard_dik(kb);
    if (kb_custom() ? kdown(kb, MF_SPRINT) : (kb[key_of(BIND_SPRINT)] & 0x80) != 0)
        return 1;   /* KEYBOARD CUSTOM: its SPRINT key; else the keyboard's sprint key (Left Shift) */
    uint64_t c = wr_pad_controls();
    bind_load();
    int ctl = SCHEME_MODERNISH() ? modern_fn_button(MF_SPRINT) : SCHEME_IS(SCHEME_CUSTOM) ? g_bind[BIND_SPRINT] : WR_PAD_LSTICK;
    return ctl >= 0 && (int)(c >> ctl & 1);
}

/* CAMERA PAN (ENHANCEMENTS; PR #7, its own switch since 2026-11-28): while the SPECIAL 2 button of this situation is held
 * (MODERN: Select by default, CUSTOM: its binding, CLASSIC: L2) the right stick is the four directions of the game's
 * Special 2 look-around, and its aim (on foot) / fire (in a car) is off. Single player only. */
int gta2_pan_held(void)
{
    if (!opt_get(OPT_CAMERA_PAN) || wr_rd32(0x00673E2Cu) || gta2_pause_open())
        return 0;
    int c;
    if (SCHEME_MODERNISH())
        c = modern_fn_button(gta2_player_driving() ? MC_SP2 : MF_SP2);
    else if (SCHEME_IS(SCHEME_CUSTOM)) {
        bind_load();
        c = g_bind[10];   /* SPECIAL 2 */
    } else
        c = WR_PAD_LT;    /* CLASSIC: special 2 on L2 */
    return c >= 0 && (int)(wr_pad_controls() >> c & 1);
}

/* PUNCH held (ENHANCEMENTS > PUNCH, on foot, single player): MODERN's PUNCH button (X by default, CUSTOM MAPPING moves
 * it) or KEYBOARD CUSTOM's PUNCH key. The same press is the game's ATTACK (map / kmod_hook); gta2_state.c puts the
 * weapon away meanwhile. */
int gta2_punch_pressed(void)
{
    if (!opt_get(OPT_PUNCH) || wr_rd32(0x00673E2Cu) || gta2_pause_open() || gta2_player_driving())
        return 0;
    if (SCHEME_MODERNISH()) {
        int c = modern_fn_button(MF_PUNCH);
        if (c >= 0 && (wr_pad_controls() >> c & 1))
            return 1;
    }
    if (kb_custom()) {
        uint8_t kb[256];
        wr_keys_keyboard_dik(kb);
        if (kdown(kb, MF_PUNCH))
            return 1;
    }
    return 0;
}

/* analogue pedals (PR #7): gas mapped to R2 / brake to L2 in the current scheme */
int gta2_gas_on_rt(void)
{
    if (SCHEME_MODERNISH()) {
        mbind_load();
        return g_mbind[MC_GAS] == WR_PAD_RT;
    }
    if (SCHEME_IS(SCHEME_CUSTOM)) {
        bind_load();
        return g_bind[0] == WR_PAD_RT;   /* FORWARD = the gas in a car */
    }
    return 0;
}

int gta2_brake_on_lt(void)
{
    if (SCHEME_MODERNISH()) {
        mbind_load();
        return g_mbind[MC_BRAKE] == WR_PAD_LT;
    }
    if (SCHEME_IS(SCHEME_CUSTOM)) {
        bind_load();
        return g_bind[1] == WR_PAD_LT;   /* BACKWARD = brake / reverse */
    }
    return 0;
}

/* ---- KEYBOARD schemes (maintainer 2026-11-26) -----------------------------------------------
 *   (maintainer 2026-11-26: MODERN = keyboard only, the old MODERN is KEYBOARD + MOUSE)
 *   CLASSIC  the game's own keys (Control\0..11, CUSTOM MAPPING keyboard)
 *   MODERN   the keyboard as a controller (option-c's layout, runtime wr_pad):
 *            WASD stick, arrows d-pad, Enter START, Backspace BACK, I/J/K/L = Y/X/A/B,
 *            U/O = LB/RB, Z/P = LT/RT - the CONTROLLER scheme then applies
 *   KEYBOARD + MOUSE:
 *            on foot  WASD walk that way, the ped faces the mouse pointer
 *            in a car W gas, S brake, A / D steer, arrows left / right radio
 *            left button attack, right button or Space jump / handbrake, E enter / exit,
 *            Q / R or the wheel previous / next weapon, F / G / C special 1 / 2 / 3,
 *            Shift sprint, Esc the menu */

static int kbm_active(void)
{
    return opt_get(OPT_KEY_SCHEME) == 2 && !gta2_pause_open() &&
           !(g_gta2_frontend_ms && wr_time_ms() - g_gta2_frontend_ms < 250);
}

static int key_down(const bool *k, int n, int sc) { return sc < n && k[sc]; }

static void kbm_hook(uint8_t st[256])
{
    if (!kbm_active())
        return;
    int n = 0;
    const bool *k = SDL_WasInit(SDL_INIT_VIDEO) ? SDL_GetKeyboardState(&n) : NULL;
    if (!k)
        return;
    float mx, my;
    SDL_MouseButtonFlags mb = SDL_GetMouseState(&mx, &my);
    int driving = gta2_player_driving();
    /* the raw letters of this layout are not the game's keys */
    static const uint8_t taken[] = { 0x11, 0x1E, 0x1F, 0x20, 0x12, 0x10, 0x13, 0x21, 0x22, 0x2E, 0x39 };
    for (size_t i = 0; i < sizeof taken; i++)
        st[taken[i]] = 0;
    if (driving) {
        st[0xCB] = st[0xCD] = 0;   /* arrows left / right: the radio */
        if (key_down(k, n, SDL_SCANCODE_W)) st[key_of(ACT_UP)] = 0x80;
        if (key_down(k, n, SDL_SCANCODE_S)) st[key_of(ACT_DOWN)] = 0x80;
        if (key_down(k, n, SDL_SCANCODE_A)) st[key_of(ACT_LEFT)] = 0x80;
        if (key_down(k, n, SDL_SCANCODE_D)) st[key_of(ACT_RIGHT)] = 0x80;
        if (key_down(k, n, SDL_SCANCODE_LEFT)) st[0x3B] = 0x80;    /* F1 */
        if (key_down(k, n, SDL_SCANCODE_RIGHT)) st[0x3C] = 0x80;   /* F2 */
    }
    if (mb & SDL_BUTTON_LMASK) st[key_of(ACT_ATTACK)] = 0x80;
    if ((mb & SDL_BUTTON_RMASK) || key_down(k, n, SDL_SCANCODE_SPACE)) st[key_of(ACT_JUMP)] = 0x80;
    if (key_down(k, n, SDL_SCANCODE_E)) st[key_of(ACT_ENTER)] = 0x80;
    if (key_down(k, n, SDL_SCANCODE_Q)) st[key_of(ACT_PREV)] = 0x80;
    if (key_down(k, n, SDL_SCANCODE_R)) st[key_of(ACT_NEXT)] = 0x80;
    if (key_down(k, n, SDL_SCANCODE_F)) st[key_of(ACT_SPECIAL)] = 0x80;
    if (key_down(k, n, SDL_SCANCODE_G)) st[key_of(ACT_SPECIAL2)] = 0x80;
    if (key_down(k, n, SDL_SCANCODE_C)) st[key_of(ACT_STATUS)] = 0x80;
    /* the wheel: one weapon step per notch, held for a few polls so the game sees it */
    static int pulse_prev, pulse_next;
    if (g_wr_mouse_wheel > 0) { pulse_next = 4; g_wr_mouse_wheel = 0; }
    if (g_wr_mouse_wheel < 0) { pulse_prev = 4; g_wr_mouse_wheel = 0; }
    if (pulse_next > 0) { if (pulse_next-- > 1) st[key_of(ACT_NEXT)] = 0x80; }
    if (pulse_prev > 0) { if (pulse_prev-- > 1) st[key_of(ACT_PREV)] = 0x80; }
}

/* KEYBOARD CUSTOM in play (not in the frontend or the pause menu: their own keys) */
static int kmod_active(void)
{
    return kb_custom() && !gta2_pause_open() &&
           !(g_gta2_frontend_ms && wr_time_ms() - g_gta2_frontend_ms < 250);
}

/* KEYBOARD MODERN: the player's keys -> the game's own keys for this situation. The game's twelve action keys
 * and every bound key are taken out first (only this mapping drives); Escape stays the pause. On foot MOVE is the
 * walking direction (gta2_kbm_sticks -> the MODERN on-foot step), in a car LEFT / RIGHT steer. SPRINT and the
 * camera read their keys themselves (gta2_sprint_pressed, gta2_camera_keys). */
static void kmod_hook(uint8_t st[256])
{
    if (!kmod_active())
        return;
    uint8_t raw[256];
    memcpy(raw, st, sizeof raw);
    kbind_load();
    for (int a = 0; a < ACT_N; a++)
        st[key_of(a)] = 0;
    for (int i = 0; i < KS_N; i++)
        if (g_kbind[i])
            st[g_kbind[i]] = 0;
    int driving = gta2_player_driving(), ctx = driving ? CTX_CAR : CTX_FOOT;
    for (int i = 0; i < KS_N; i++) {
        if (!(kslot_ctx(i) & ctx) || !kdown(raw, i))
            continue;
        int a;
        switch (i) {
        case KM_UP: case KM_DOWN: a = -1; break;   /* on foot: the walking direction; in a car: nothing (as the pad) */
        case KM_LEFT: a = driving ? ACT_LEFT : -1; break;
        case KM_RIGHT: a = driving ? ACT_RIGHT : -1; break;
        case KM_RADIO_NEXT: a = ACT_RADIO_NEXT; break;
        case KM_RADIO_PREV: a = ACT_RADIO_PREV; break;
        case KM_MENU: a = ACT_ESCAPE; break;
        default:
            a = k_mslot[i].act;
            if (a == ACT_SPRINT)
                a = opt_get(OPT_SPRINT) ? -1 : ACT_JUMP;   /* SPRINT off: a jump (as the pad) */
            if (a == ACT_PUNCH)   /* PUNCH: the game's attack; the weapon is put away meanwhile (gta2_punch_pressed) */
                a = opt_get(OPT_PUNCH) ? ACT_ATTACK : -1;
            break;
        }
        if (a < 0 || is_cam_act(a))
            continue;
        if (a == ACT_ESCAPE)
            st[0x01] = 0x80;
        else if (a == ACT_RADIO_NEXT || a == ACT_RADIO_PREV)
            st[a == ACT_RADIO_NEXT ? 0x3C : 0x3B] = 0x80;   /* F2 / F1 */
        else if (a < ACT_N)
            st[key_of(a)] = 0x80;
    }
}

void gta2_keys_apply(void)
{
    int sc = opt_get(OPT_KEY_SCHEME);
    wr_pad_keyboard_as_pad(sc == 1);   /* MODERN: the keyboard as a fixed controller (preset, unchanged) */
    g_wr_dik_hook = sc == 2 ? kbm_hook : sc == KEY_SCHEME_CUSTOM ? kmod_hook : NULL;
}

/* MODERN keyboard on foot: WASD as the left stick, the mouse pointer (from
 * the game picture's centre) as the right one; 0 = not this scheme */
int gta2_kbm_sticks(float *lx, float *ly, float *rx, float *ry)
{
    *lx = *ly = *rx = *ry = 0;
    if (kmod_active()) {   /* KEYBOARD CUSTOM: MOVE as the left stick (walk where it points), no aiming stick */
        uint8_t kb[256];
        wr_keys_keyboard_dik(kb);
        *lx = (float)(kdown(kb, KM_RIGHT) - kdown(kb, KM_LEFT));
        *ly = (float)(kdown(kb, KM_DOWN) - kdown(kb, KM_UP));
        return 1;
    }
    if (!kbm_active())
        return 0;
    int n = 0;
    const bool *k = SDL_WasInit(SDL_INIT_VIDEO) ? SDL_GetKeyboardState(&n) : NULL;
    if (k) {
        *lx = (float)(key_down(k, n, SDL_SCANCODE_D) - key_down(k, n, SDL_SCANCODE_A));
        *ly = (float)(key_down(k, n, SDL_SCANCODE_S) - key_down(k, n, SDL_SCANCODE_W));
    }
    float mx, my;
    SDL_GetMouseState(&mx, &my);
    float cx, cy;
    if (gta2_game_centre(&cx, &cy)) {
        float dx = mx - cx, dy = my - cy, d = sqrtf(dx * dx + dy * dy);
        if (d > 12.0f) {   /* a pointer on the ped: keep the heading */
            *rx = dx / d;
            *ry = dy / d;
        }
    }
    return 1;
}

/* ---- CONTROL OVERVIEW: every function with its controller button and its key for
 * the schemes in use, on foot and in a car (maintainer 2026-11-26). Controller
 * tokens are wr_pad_control_name's; the PAD keyboard shows the key behind them. */
typedef struct ov_row {
    const char *label;
    const char *classic, *modern;   /* controller, " / " between alternatives */
    int act;                        /* CLASSIC keyboard + CUSTOM controller: ACT_* / bind index, -1 none */
    const char *kbm;                /* MODERN keyboard */
    int ms;                         /* MODERN: its function + 1 (the controller shows that binding), 0 none */
} ov_row;

static const ov_row k_ov[] = {
    { "ON FOOT", NULL, NULL, -1, NULL },
    { "WALK", "STICK / D-PAD", "STICK / D-PAD", -2, "W A S D" },
    { "TURN / AIM", "STICK / D-PAD", "R-STICK", -3, "MOUSE" },
    { "SPRINT", "L3", "A", BIND_SPRINT, "LEFT SHIFT", MF_SPRINT + 1 },
    { "ATTACK", "X / RT", "RT", ACT_ATTACK, "L MOUSE", MF_ATTACK + 1 },
    { "JUMP", "B", "B", ACT_JUMP, "R MOUSE / SPACE", MF_JUMP + 1 },
    { "ENTER VEHICLE", "A", "Y", ACT_ENTER, "E", MF_ENTER + 1 },
    { "PREVIOUS WEAPON", "LB", "LB", ACT_PREV, "Q / WHEEL", MF_PREV + 1 },
    { "NEXT WEAPON", "RB", "RB", ACT_NEXT, "R / WHEEL", MF_NEXT + 1 },
    { "SPECIAL 1", "Y", "R3", ACT_SPECIAL, "F", MF_SP1 + 1 },
    { "SPECIAL 2", "LT", "SELECT", ACT_SPECIAL2, "G", MF_SP2 + 1 },
    { "SPECIAL 3", "R3", "L3", ACT_STATUS, "C", MF_SP3 + 1 },
    { "MENU", "START", "START", -4, "ESC" },
    { "IN A CAR", NULL, NULL, -1, NULL },
    { "GAS", "STICK / D-PAD UP", "A", ACT_UP, "W", MC_GAS + 1 },
    { "BRAKE / REVERSE", "STICK / D-PAD DOWN", "X", ACT_DOWN, "S", MC_BRAKE + 1 },
    { "STEER", "STICK / D-PAD L R", "STICK / D-PAD L R", -5, "A / D" },
    { "HANDBRAKE", "B", "B", ACT_JUMP, "R MOUSE / SPACE", MC_HB + 1 },
    { "ATTACK", "X / RT", "RT", ACT_ATTACK, "L MOUSE", MC_ATTACK + 1 },
    { "EXIT VEHICLE", "A", "Y", ACT_ENTER, "E", MC_EXIT + 1 },
    { "PREVIOUS WEAPON", "LB", "LB", ACT_PREV, "Q / WHEEL", MC_PREV + 1 },
    { "NEXT WEAPON", "RB", "RB", ACT_NEXT, "R / WHEEL", MC_NEXT + 1 },
    { "SPECIAL 1", "Y", "R3", ACT_SPECIAL, "F", MC_SP1 + 1 },
    { "SPECIAL 2", "LT", "SELECT", ACT_SPECIAL2, "G", MC_SP2 + 1 },
    { "SPECIAL 3", "R3", "L3", ACT_STATUS, "C", MC_SP3 + 1 },
    { "RADIO", "R-STICK L R", "R-STICK L R", -6, "ARROWS L R" },
    /* the camera buttons; their values come from the bindings (gta2_overview_value) */
    { "CAMERA", NULL, NULL, -1, NULL },
    { "CAMERA", "-", "LT", ACT_CAMERA, "-", MK_CAMERA + 1 },
    { "ZOOM IN", "-", "-", ACT_ZOOM_IN, "-", MK_ZIN + 1 },
    { "ZOOM OUT", "-", "-", ACT_ZOOM_OUT, "-", MK_ZOUT + 1 },
};

/* a camera row's controller button and key under the schemes in use ("-" for none) */
static void cam_row(int act, char *pad, size_t pc, char *key, size_t kc)
{
    int b = cam_pad_button(act);
    snprintf(pad, pc, "%s", b >= 0 ? wr_pad_control_name(b) : "-");
    uint8_t k = key_of(act);
    if (k)
        wr_key_name(k, key, kc);
    else
        snprintf(key, kc, "-");
}

int gta2_overview_count(void) { return (int)(sizeof k_ov / sizeof k_ov[0]); }
const char *gta2_overview_label(int i) { return i >= 0 && i < gta2_overview_count() ? k_ov[i].label : ""; }
int gta2_overview_is_head(int i) { return i >= 0 && i < gta2_overview_count() && !k_ov[i].classic; }

/* the keyboard pad's key for a controller token */
static const char *pad_key(const char *t)
{
    static const char *const m[][2] = {
        { "A", "K" }, { "B", "L" }, { "X", "J" }, { "Y", "I" }, { "LB", "U" }, { "RB", "O" }, { "LT", "Z" },
        { "RT", "P" }, { "START", "ENTER" }, { "SELECT", "BACKSPACE" }, { "D-PAD", "ARROWS" },
        { "STICK", "W A S D" }, { "D-PAD UP", "ARROW UP" }, { "D-PAD DOWN", "ARROW DOWN" }, { "D-PAD L R", "ARROWS L R" },
        { "STICK / D-PAD UP", "W" }, { "STICK / D-PAD DOWN", "S" }, { "R-STICK L R", NULL },
        { "STICK UP", "W" }, { "STICK DOWN", "S" }, { "STICK L R", "A D" },
    };
    for (size_t i = 0; i < sizeof m / sizeof m[0]; i++)
        if (!strcmp(t, m[i][0]))
            return m[i][1];
    return NULL;   /* sticks clicked, the right stick: no key */
}

/* "X / RT" -> "J / P"; tokens without a key are left out */
static void to_pad_keys(const char *s, char *out, size_t cap)
{
    char buf[96];
    snprintf(buf, sizeof buf, "%s", s);
    out[0] = 0;
    for (char *t = strtok(buf, "/"); t; t = strtok(NULL, "/")) {
        while (*t == ' ') t++;
        size_t n = strlen(t);
        while (n && t[n - 1] == ' ') t[--n] = 0;
        char *paren = strstr(t, " (");
        if (paren) *paren = 0;
        if (!strcmp(t, "D-PAD UP") || !strcmp(t, "D-PAD DOWN") || !strcmp(t, "D-PAD L R")) {
            /* "STICK / D-PAD UP": the stick part was the same direction */
            char *prev = strrchr(out, '/');
            char *start = prev ? prev + 2 : out;
            if (!strcmp(start, "W A S D"))
                snprintf(start, cap - (size_t)(start - out), "%s", t[6] == 'U' ? "W" : t[6] == 'D' ? "S" : "A D");
        }
        const char *k = pad_key(t);
        if (k)
            snprintf(out + strlen(out), cap - strlen(out), "%s%s", out[0] ? " / " : "", k);
    }
    if (!out[0])
        snprintf(out, cap, "-");
}

static int bind_of_act(int act)
{
    for (int i = 0; i < GTA2_BIND_N; i++)
        if (k_fn[i].act == act || (act == BIND_SPRINT && i == BIND_SPRINT))
            return i;
    return -1;
}


void gta2_overview_value(int i, char *out, size_t cap)
{
    out[0] = 0;
    if (gta2_overview_is_head(i) || i < 0 || i >= gta2_overview_count())
        return;
    const ov_row *r = &k_ov[i];
    int ks = opt_get(OPT_KEY_SCHEME);
    char pad[96], key[96];
    if (is_cam_act(r->act)) {   /* the camera: from the bindings */
        cam_row(r->act, pad, sizeof pad, key, sizeof key);
        snprintf(out, cap, "%s   |   %s", pad, key);
        return;
    }
    if (SCHEME_IS(SCHEME_CUSTOM) && r->act >= 0 && bind_of_act(r->act) >= 0)
        gta2_bind_value(0, bind_of_act(r->act), pad, sizeof pad);
    else if (SCHEME_IS(SCHEME_CUSTOM) && r->act == -6)
        snprintf(pad, sizeof pad, "%s %s", g_bind[14] >= 0 ? wr_pad_control_name(g_bind[14]) : "-",
                 g_bind[13] >= 0 ? wr_pad_control_name(g_bind[13]) : "-");
    else if (SCHEME_MODERNISH() && r->ms) {   /* MODERN: the button its function is on */
        int b = modern_fn_button(r->ms - 1);
        snprintf(pad, sizeof pad, "%s", b >= 0 ? wr_pad_control_name(b) : "-");
    } else if (SCHEME_MODERNISH())
        snprintf(pad, sizeof pad, "%s", r->modern);
    else
        snprintf(pad, sizeof pad, "%s", r->classic);
    if (ks == 2)
        snprintf(key, sizeof key, "%s", r->kbm);
    else if (ks == KEY_SCHEME_CUSTOM && r->ms) {   /* KEYBOARD CUSTOM: the key its function is on */
        kbind_load();
        if (g_kbind[r->ms - 1])
            wr_key_name(g_kbind[r->ms - 1], key, sizeof key);
        else
            snprintf(key, sizeof key, "-");
    } else if (ks == 1)
        to_pad_keys(pad, key, sizeof key);
    else if (r->act >= 0)
        wr_key_name(key_of(r->act), key, sizeof key);
    else {
        static const char *const fixed[] = { "", "", "ARROWS", "ARROWS", "ESC", "ARROWS L R", "F1 / F2",
                                             "PAGE UP", "PAGE DOWN" };
        snprintf(key, sizeof key, "%s", fixed[-r->act]);
    }
    snprintf(out, cap, "%s   |   %s", pad, key);
}

/* ---- CONTROLLER LAYOUT labels (gta2_padview.c): "LT  SPECIAL 2" for the scheme in use ---- */
#include "gta2_padview.h"

static const char *act_name(int a, int driving)
{
    switch (a) {
    case ACT_UP: return driving ? "GAS" : "FORWARD";
    case ACT_DOWN: return driving ? "BRAKE" : "BACK";
    case ACT_LEFT: case ACT_RIGHT: return driving ? "STEER" : "TURN";
    case ACT_ATTACK: return "ATTACK";
    case ACT_PUNCH: return "PUNCH";
    case ACT_ENTER: return driving ? "EXIT CAR" : "ENTER CAR";
    case ACT_JUMP: return driving ? "HANDBRAKE" : "JUMP";
    case ACT_PREV: return "PREV WEAPON";
    case ACT_NEXT: return "NEXT WEAPON";
    case ACT_SPECIAL: return "SPECIAL 1";
    case ACT_SPECIAL2: return "SPECIAL 2";
    case ACT_STATUS: return "SPECIAL 3";
    case ACT_ESCAPE: return "MENU";
    case ACT_RADIO_NEXT: case ACT_RADIO_PREV: return driving ? "RADIO" : NULL;
    case ACT_SPRINT: return driving ? NULL : "SPRINT";
    case ACT_CAMERA: return "CAMERA";
    case ACT_ZOOM_IN: return "ZOOM IN";
    case ACT_ZOOM_OUT: return "ZOOM OUT";
    default: return NULL;
    }
}

static int control_act(int c, int driving)
{
    if (opt_get(OPT_SPRINT) && !driving) {   /* SPRINT's button (gta2_sprint_pressed) */
        bind_load();
        int sp = SCHEME_MODERNISH() ? modern_fn_button(MF_SPRINT) : SCHEME_IS(SCHEME_CUSTOM) ? g_bind[BIND_SPRINT] : WR_PAD_LSTICK;
        if (c == sp)
            return ACT_SPRINT;
    }
    if (SCHEME_MODERNISH())
        return modern_act(c, driving);
    if (SCHEME_IS(SCHEME_CUSTOM)) {
        bind_load();
        for (int i = 0; i < GTA2_BIND_N; i++)
            if (g_bind[i] == c && i != BIND_SPRINT)
                return k_fn[i].act;
        return -1;
    }
    return pad_action(c);
}

void gta2_pad_function(int id, int driving, char *out, size_t cap)
{
    static const struct { int ctl; const char *tok; } k[PV_N] = {
        [PV_LT] = { WR_PAD_LT, "LT" }, [PV_LB] = { WR_PAD_LB, "LB" }, [PV_LSTICK] = { -1, "STICK" },
        [PV_L3] = { WR_PAD_LSTICK, "L3" }, [PV_DPAD] = { -2, "D-PAD" }, [PV_SELECT] = { WR_PAD_BACK, "SELECT" },
        [PV_RT] = { WR_PAD_RT, "RT" }, [PV_RB] = { WR_PAD_RB, "RB" }, [PV_Y] = { WR_PAD_NORTH, "Y" },
        [PV_B] = { WR_PAD_EAST, "B" }, [PV_A] = { WR_PAD_SOUTH, "A" }, [PV_X] = { WR_PAD_WEST, "X" },
        [PV_RSTICK] = { -3, "R-STICK" }, [PV_R3] = { WR_PAD_RSTICK, "R3" }, [PV_START] = { WR_PAD_START, "START" },
    };
    if (id < 0 || id >= PV_N) {
        out[0] = 0;
        return;
    }
    int c = k[id].ctl;
    const char *f = NULL;
    if (c == -1 || c == -2) {   /* the left stick, the d-pad */
        if (SCHEME_MODERNISH())
            f = driving ? "STEER" : "WALK";
        else
            f = driving ? "DRIVE" : "MOVE";
    } else if (c == -3) {   /* the right stick */
        if (SCHEME_MODERNISH())
            f = driving ? "RADIO" : "AIM";
        else
            f = driving ? "RADIO" : NULL;
    } else
        f = act_name(control_act(c, driving), driving);
    snprintf(out, cap, "%s  %s", k[id].tok, f ? f : "-");
}
