/*
 * GTA 2 Recompiled - SAVE ANYWHERE (GTA 2 issue #6; maintainer 2026-11-28): SAVE GAME in the pause menu, LOAD GAME
 * in the main menu. docs/re/save_anywhere.md.
 *
 * The game's own save writer (sub_0047EF40, as the church - script opcode 0x1B6 - calls it) already stores the
 * player's real position; on load the player is created there (sub_00477140). So SAVE GAME calls that writer with
 * a slot of our own, player\anyslotN.svg (N = the profile slot) - the church save player\plyslotN.svg and the
 * game's RESUME SAVED STATUS stay untouched, a death still goes back to the church save. Free (the church costs
 * $50,000). Refused (maintainer): on a job, wanted, in a car, in water or falling - and conservatively anything but
 * plain standing / walking on foot. LOAD GAME runs the frontend's own load path with the slot name redirected
 * (wrapper of sub_004528A0) and starts the game.
 */
#include "wr.h"
#include "recomp_types.h"
#include "gta2_state.h"
#include "gta2_saveany.h"
#include "gta2_options.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define FN_SAVE_GAME   0x0047EF40u   /* thiscall script [0x6644BC], (const char *path), ret 4 */
#define FN_HUD_MSG     0x004C6750u   /* thiscall [0x672F40]+0xE4, (1, char *gxt key): the HUD's message line */
#define FN_SAVE_NAME   0x0045E470u   /* thiscall 0x5EC070, (const char *name): the area's save name (+0x300) */
#define FN_SLOT_EXISTS 0x00452940u   /* thiscall fe, (u8 slot) -> al: the slot's file exists */
#define FN_LOAD_HEADER 0x00455850u   /* thiscall fe: reads the save's header, sets the level manager */
#define SCRIPT_PTR     0x006644BCu
#define HUD_PTR        0x00672F40u
#define GAME_SETUP     0x005EC070u   /* +0x300 save name, +0x402 bonus area, +0x403 profile slot */

static uint32_t call_this(X86Ctx *ctx, uint32_t fn, uint32_t self, int n, const uint32_t *args)
{
    uint32_t ecx = ctx->r_ecx;
    ctx->r_ecx = self;
    uint32_t r = wr_call_guest(ctx, fn, n, args);
    ctx->r_ecx = ecx;
    return r;
}

static uint32_t g_buf;   /* guest text buffer (path / gxt key) */

static uint32_t guest_text(const char *s)
{
    if (!g_buf)
        g_buf = wr_galloc(64);
    if (!g_buf)
        return 0;
    size_t n = strlen(s) + 1;
    memcpy(wr_ptr(g_buf), s, n > 64 ? 64 : n);
    return g_buf;
}

static unsigned profile_slot(void) { return wr_rd8(GAME_SETUP + 0x403); }

static void slot_path(char *out, size_t n) { snprintf(out, n, "player\\anyslot%u.svg", profile_slot()); }

/* ---- falling: the height must stand still for a while before the pause opened ---------------------------- */
static uint32_t g_zlast;
static int g_zstable;

void gta2_saveany_frame(int paused)
{
    if (paused)
        return;   /* frozen while paused: counts only the play before it */
    uint32_t ped = gta2_player_ped();
    uint32_t z = ped && wr_mem_valid(ped, 0x2A0) ? wr_rd32(ped + 0x1B4) : 0;
    g_zstable = ped && z == g_zlast ? (g_zstable < 1000 ? g_zstable + 1 : g_zstable) : 0;
    g_zlast = z;
}

/* NULL = a save is allowed now, else why not: a short English value for the pause line (it is right-aligned on the
 * line: a long one ran into "SAVE GAME", maintainer's screenshot 11-28; Recomp Options stay English) */
static const char *refusal(void);

const char *gta2_saveany_refusal(void)
{
    const char *why = refusal();
    static const char *last = "";
    if (why != last) {   /* the reason and the values behind it into the log, once per change */
        uint32_t ped = gta2_player_ped();
        if (ped && wr_mem_valid(ped, 0x2A0))
            wr_log("save anywhere: %s (state %u/%u, surface %u, car %08X, cop %d, health %d, still %d frames)",
                   why ? why : "allowed", wr_rd32(ped + 0x278), wr_rd32(ped + 0x27C), wr_rd32(ped + 0x254),
                   wr_rd32(ped + 0x16C), (int16_t)wr_rd16(ped + 0x20A), (int16_t)wr_rd16(ped + 0x216), g_zstable);
        last = why;
    }
    return why;
}

static const char *refusal(void)
{
    uint32_t s = wr_rd32(SCRIPT_PTR), ped = gta2_player_ped(), g = gta2_game();
    if (!s || !g || wr_rd32(g) != 2 || wr_rd32(0x00673E2Cu))   /* not a paused single-player game */
        return "NOT NOW";
    uint32_t m = wr_mem_valid(s + 0x344, 4) ? wr_rd32(s + 0x344) : 0;
    if (m && wr_mem_valid(m, 4) && wr_rd32(m))   /* a job runs - the church's own test */
        return "ON A JOB";
    if (wr_rd8(GAME_SETUP + 0x402))   /* bonus area */
        return "BONUS AREA";
    if (!ped || !wr_mem_valid(ped, 0x2A0))
        return "NOT NOW";
    if (wr_rd32(ped + 0x16C) || !wr_rd32(ped + 0x168))   /* in a car (no on-foot sprite) */
        return "IN A CAR";
    if ((int16_t)wr_rd16(ped + 0x20A) >= 600)   /* a wanted level: the HUD's first head (sub_00434C00); below it the
                                                  * player sees no wanted level (play log 11-28: 300 points refused) */
        return "WANTED";
    if (wr_rd32(ped + 0x254) == 4)   /* the lid under the ped is water */
        return "IN WATER";
    /* plain on-foot states only (not falling, knocked down, dead, getting in / out): (0, 0) and (7, 0xE) - the
     * latter is the one the game reports for a player standing still on foot (play log 11-28: state 7/14, no car,
     * height still for 32 s); widen it from play logs (the log line names the state) */
    uint32_t s1 = wr_rd32(ped + 0x278), s2 = wr_rd32(ped + 0x27C);
    int plain = (s1 == 0 && s2 == 0) || (s1 == 7 && s2 == 0xE);
    if (!plain || (int16_t)wr_rd16(ped + 0x216) <= 0)
        return "NOT ON FOOT";
    if (g_zstable < 15)   /* still falling or just landed */
        return "MOVING";
    return NULL;
}

int gta2_saveany_save(X86Ctx *ctx)
{
    if (!opt_get(OPT_SAVE_ANYWHERE) || gta2_saveany_refusal())
        return 0;
    char p[40];
    slot_path(p, sizeof p);
    uint32_t a = guest_text(p);
    if (!a)
        return 0;
    call_this(ctx, FN_SAVE_GAME, wr_rd32(SCRIPT_PTR), 1, &a);
    wr_log("save anywhere: %s", p);
    uint32_t hud = wr_rd32(HUD_PTR);
    if (hud) {   /* the game's own "Your status is saved!" (svdone), as the church */
        uint32_t k = guest_text("svdone"), args[2] = { 1, k };
        call_this(ctx, FN_HUD_MSG, hud + 0xE4, 2, args);
    }
    return 1;
}

/* ---- LOAD GAME ------------------------------------------------------------------------------------------------ */
static int g_anyslot;      /* 1: sub_004528A0 names the save-anywhere slot */
static int g_clear_name;   /* after a LOAD GAME: the area's save name back to "" once the game runs */

int gta2_saveany_exists(void)
{
    char p[40], full[1200];
    snprintf(p, sizeof p, "player/anyslot%u.svg", profile_slot());
    snprintf(full, sizeof full, "%s/%s", g_wr_paths.saves, p);
    SDL_PathInfo pi;
    return SDL_GetPathInfo(full, &pi) && pi.type == SDL_PATHTYPE_FILE;
}

int gta2_saveany_load(X86Ctx *ctx, uint32_t fe)
{
    if (!fe || !gta2_saveany_exists())
        return 0;
    uint32_t slot = profile_slot();
    g_anyslot = 1;
    int ok = (call_this(ctx, FN_SLOT_EXISTS, fe, 1, &slot) & 0xFF) != 0;
    if (ok)
        call_this(ctx, FN_LOAD_HEADER, fe, 0, NULL);
    g_anyslot = 0;
    if (!ok)
        return 0;
    wr_wr8(fe + 0xEDF4, 0x10);   /* "start the game", as RESUME SAVED STATUS (0x459A11) */
    wr_wr32(fe + 0x110, 2);
    g_clear_name = 1;
    wr_log("load game: player\\anyslot%u.svg", slot);
    return 1;
}

/* once the game runs after a LOAD GAME: the area's save name back to "" - a church save then writes the church
 * slot again (sub_0047EF40 takes +0x300 when given no path) */
void gta2_saveany_game_running(X86Ctx *ctx)
{
    if (!g_clear_name || !gta2_player_ped())
        return;
    g_clear_name = 0;
    uint32_t a = guest_text("");
    call_this(ctx, FN_SAVE_NAME, GAME_SETUP, 1, &a);
}

/* the slot-name wrapper (sub_004528A0, in gta2_manual.c: the lifter takes wrappers from there) asks this */
int gta2_saveany_redirect(void) { return g_anyslot; }
