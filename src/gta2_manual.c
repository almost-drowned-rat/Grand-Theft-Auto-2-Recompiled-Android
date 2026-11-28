/*
 * GTA 2 Recompiled - native replacements for individual guest functions.
 *
 * A `void sub_XXXXXXXX(RECOMP_PARAMS)` defined here is left out of the
 * generated code (game.toml [recomp] exclude_manual names this file), so
 * direct calls bind to it; wr_manual_register covers indirect calls.
 */
#include "wr.h"
#include "vk/wr_vk.h"
#include "gta2_menu.h"
#include "gta2_gameplay.h"
#include "gta2_framerate.h"
#include "gta2_mixtape.h"
#include "gta2_text.h"
#include "gta2_radar.h"
#include "gta2_input.h"
#include "gta2_daynight.h"
#include "gta2_state.h"
#include "gta2_options.h"
#include "gta2_saveany.h"
#include "platform/wr_platform.h"
#include <stdio.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


/* 0x004031C0 GetDXVersion(DWORD *version, DWORD *platform), stdcall: the DX
 * SDK's detection that creates DirectDraw/DirectInput objects to tell the
 * installed DirectX apart. The runtime replaces DirectX, so answer what the
 * game needs (>= 0x601): DirectX 8.1 on the NT platform. */
void sub_004031C0(RECOMP_PARAMS)
{
    (void)base;
    uint32_t ver = WR_ARG(0), plat = WR_ARG(1);
    if (ver)
        wr_wr32(ver, 0x801);
    if (plat)
        wr_wr32(plat, 2);
    WR_RET(0, 2);
}

/* Sleep until the clock reaches until. The clock is read ONCE per pass: with a second read for the
 * rest, a step past until in between made until - now wrap to ~2^64 ns and the thread slept for good
 * (Android 0.4.1: the picture stood, the music went on; rare, only when preempted right there). */
static void sleep_until(uint64_t until)
{
    for (;;) {
        uint64_t now = wr_time_ns();
        if (now >= until)
            return;
        uint64_t rest = until - now;
        wr_sleep_ns(rest > 900000 ? rest - 200000 : rest);
    }
}

/* 0x00462A30 the main loop's per-iteration step: one 33 ms logic tick, one
 * draw, or reading input - and otherwise nothing, so the original spins at
 * 100 % of a core until the next tick is due (docs/re/framerate.md). Wrapped:
 * an iteration that neither ticked nor drew sleeps until shortly before the
 * deadline [0x00662748] (timeGetTime ms). Game logic and timing unchanged. */
extern void sub_00462A30_gen(RECOMP_PARAMS);
extern uint64_t gta2_frames_presented(void);
static int g_fps_set_catchup;   /* FRAME RATE turned the game's catch-up pacing on */
void sub_00462A30(RECOMP_PARAMS)
{
    {   /* the pause menu (gta2_menu.c) between two loop steps */
        uint32_t ecx = ctx->r_ecx, eax = ctx->r_eax;
        gta2_pause_tick(ctx);
        gta2_punch_hold();   /* ANALOGUE: the punch button keeps the weapon from firing */
        ctx->r_ecx = ecx;
        ctx->r_eax = eax;
    }
    uint32_t next0 = MEM32(0x00662748u);
    uint64_t f0 = gta2_frames_presented();
    gta2_pan_pre();
    sub_00462A30_gen(ctx, base);
    uint32_t next = MEM32(0x00662748u);
    if (next != next0)
        gta2_pan_post();   /* ANALOGUE: the look-around made snappier */
    if (next != next0) {
        gta2_fps_after_tick();    /* FRAME RATE: the tick's positions for interpolation */
        wr_overlay_game_tick();   /* GAME SPEED */
        gta2_traffic_tick();      /* TRAFFIC: more spawn credit */
    }
    if (next != next0 && getenv("WR_TICK_LOG")) {
        static uint64_t last;
        uint64_t t = wr_time_ns();
        static int n;
        if (last && n++ < 200)
            wr_log("tick +%.3f ms (next %u, now %u)", (double)(t - last) / 1e6, next, wr_time_ms());
        last = t;
    }
    {   /* stall report: no tick and no picture for over a second (the picture stands) */
        static uint64_t last_progress;
        static int stalled, n_stall;
        uint64_t tn = wr_time_ns();
        if (next != next0 || gta2_frames_presented() != f0 || !last_progress) {
            if (stalled)
                wr_log("pacing: going again after %.1f s", (double)(tn - last_progress) / 1e9);
            last_progress = tn;
            stalled = 0;
        } else if (!stalled && tn - last_progress > 1000000000ull) {
            stalled = 1;
            if (n_stall++ < 20)
                wr_log("pacing: no tick, no picture for 1 s (next %u, now %u, limiter %u, fps %d, catch-up %u, "
                       "game %08X, flag 662740 %u)", next, wr_time_ms(), MEM8(0x00595010u), gta2_fps_target(),
                       MEM8(0x00673598u), MEM32(0x005EB4FCu), MEM8(0x00662740u));
        }
    }
    if (next != next0 || gta2_frames_presented() != f0 || !MEM8(0x00595010u))
        return;   /* ticked, drew, or the limiter is off: no waiting */
    if (gta2_fps_target() > 30 && MEM32(0x005EB4FCu) && !MEM8(0x00662740u)) {
        /* FRAME RATE 60 / 120: the tick is drawn; extra interpolated frames until the next one.
         * The schedule keeps its pace (accumulated, like the game's catch-up mode) so a frame
         * that runs late never slows the game down. */
        if (!MEM8(0x00673598u)) {
            MEM8(0x00673598u) = 1;
            g_fps_set_catchup = 1;
        }
        if (gta2_fps_extra_due()) {
            uint32_t r[6] = { ctx->r_eax, ctx->r_ecx, ctx->r_edx, ctx->r_ebx, ctx->r_esi, ctx->r_edi };
            wr_call_guest(ctx, 0x00461960u, 0, NULL);
            ctx->r_eax = r[0]; ctx->r_ecx = r[1]; ctx->r_edx = r[2];
            ctx->r_ebx = r[3]; ctx->r_esi = r[4]; ctx->r_edi = r[5];
            return;
        }
        /* sleep to the next frame or the tick, whichever comes first */
        uint64_t period = 1000000000ull / (uint64_t)gta2_fps_target();
        int32_t left_ms = (int32_t)(next - wr_time_ms());
        if (left_ms > 1)
            wr_sleep_ns(period / 4);
        return;
    } else if (gta2_fps_target() <= 30 && g_fps_set_catchup) {
        MEM8(0x00673598u) = 0;   /* back at 30: the original re-base pacing */
        g_fps_set_catchup = 0;
    }
    int32_t left = (int32_t)(next - wr_time_ms());
    if (left > 100) {
        /* a tick is 33 ms: a deadline further out is not the game's pacing (seen on Android: the
         * picture stood for seconds, the main thread asleep here) - wait at most 100 ms per step */
        static int n_far;
        if (n_far++ < 20)
            wr_log("pacing: deadline %d ms ahead (next %u, before the step %u, now %u, fps %d, catch-up %u)",
                   left, next, next0, wr_time_ms(), gta2_fps_target(), MEM8(0x00673598u));
        left = 100;
    }
    if (left > 2) {
        sleep_until(wr_time_ns() + (uint64_t)(left - 1) * 1000000u);
    }
}

/* 0x00461960 one in-game frame: render (sub_0045A5A0) + Vid_FlipBuffers. With
 * FRAME RATE 60 / 120 the camera and the sprites are blended between the last
 * two ticks for this frame only (gta2_framerate.c). */
extern void sub_00461960_gen(RECOMP_PARAMS);
void sub_00461960(RECOMP_PARAMS)
{
    gta2_fps_draw_begin();
    sub_00461960_gen(ctx, base);
    gta2_fps_draw_end();
    static int log = -1;
    if (log < 0)
        log = getenv("GTA2_FPS_LOG") != NULL;
    if (log) {
        static uint64_t t0;
        static int n;
        uint64_t t = wr_time_ns();
        if (!t0) t0 = t;
        n++;
        if (t - t0 >= 1000000000ull) {
            wr_log("frame rate: %d draws in %.2f s", n, (double)(t - t0) / 1e9);
            n = 0;
            t0 = t;
        }
    }
}

/* 0x0045A320 the frontend (menus) loop step: a frame when timeGetTime
 * reaches [this+0xC9C8], else it returns at once and WinMain calls it again
 * - a spin at 100 % of a core. Same wrapper: sleep up to the deadline. */
extern void sub_0045A320_gen(RECOMP_PARAMS);
uint32_t g_gta2_frontend_ms;   /* last frontend step: the menus are on screen */

void sub_0045A320(RECOMP_PARAMS)
{
    g_gta2_frontend_ms = wr_time_ms();
    /* in the menus Esc belongs to the game again (play keeps it for the pause menu;
     * the game loop that sets it does not run here) */
    g_wr_key_hidden[0x01] = 0;
    g_wr_input_captured = 0;
    uint32_t self = ctx->r_ecx;
    uint64_t f0 = gta2_frames_presented();
    sub_0045A320_gen(ctx, base);
    if (gta2_frames_presented() != f0 || !wr_mem_valid(self + 0xC9C8u, 8))
        return;
    if (MEM8(self + 0xC9CCu))
        return;   /* an update just ran: its frame is drawn by the next call */
    int32_t left = (int32_t)(MEM32(self + 0xC9C8u) - wr_time_ms());
    if (left > 2 && left < 1000) {
        sleep_until(wr_time_ns() + (uint64_t)(left - 1) * 1000000u);
    }
}

/* ==== Widescreen: a real wider view (docs/re/widescreen.md) ====================
 * Hor+: at W/H > 4:3 the city is shown at the 4:3 zoom for that height with
 * more of it left and right; the HUD is laid out in the game's virtual
 * 640x480 scaled by H/480 and anchored to the real edges. All changes are
 * gated on the aspect: at exactly 4:3 the original code runs unchanged.
 * All cameras (render, logic, aux) are widened, so nothing pops in at the
 * sides; the game's spawn/despawn ring follows the wider view. */

static inline int32_t fxmul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 14); }
static inline int32_t fxdiv(int32_t a, int32_t b) { return b ? (int32_t)(((int64_t)a << 14) / b) : 0; }

/* A = (W/H) / (4/3) in Q14 for a camera, 0x4000 at 4:3 (or anything unset) */
static int32_t cam_aspect(uint32_t cam)
{
    if (!cam || !wr_mem_valid(cam + 0x68, 8))
        return 0x4000;
    int32_t W = (int32_t)MEM32(cam + 0x68), H = (int32_t)MEM32(cam + 0x6C);
    if (W <= 0 || H <= 0 || (int64_t)W * 3 <= (int64_t)H * 4)
        return 0x4000;
    return (int32_t)(((int64_t)W * 3 << 14) / ((int64_t)4 * H));
}

static int32_t render_aspect(void) { return cam_aspect(MEM32(0x005E3CC4u)); }

/* 0x004CB150: refresh the cameras' viewport after a mode change. The original
 * keys the fullscreen test on the height only, so 640x480 -> 854x480 never
 * reached the cameras. Same logic, keyed on width and height. */
static uint32_t g_ws_last_w;
void sub_004CB150(RECOMP_PARAMS)
{
    uint32_t game = MEM32(0x005EB4FCu);
    if (!game) {
        g_esp += 4;
        return;
    }
    uint32_t vs = MEM32(0x00673D20u);
    uint32_t W, H, keep;
    int windowed = MEM32(vs + 0x40) == 0xFFFFFFFEu;
    if (windowed) {
        W = MEM32(0x00673578u);
        H = MEM32(0x006732E8u);
        keep = W;
    } else {
        W = MEM32(vs + 0x48);
        H = MEM32(vs + 0x4C);
        keep = H;
    }
    uint32_t last = MEM32(0x006735A0u);
    int changed = windowed ? last != W : (last != H || W != g_ws_last_w);
    if (changed) {
        uint32_t args[2] = { W, H };
        ctx->r_ecx = game;
        wr_call_guest(ctx, 0x0045A4F0u, 2, args);
    }
    g_ws_last_w = W;
    MEM32(0x006735A0u) = keep;
    wr_call_guest(ctx, 0x004619D0u, 0, NULL);
    wr_call_guest(ctx, 0x004619E0u, 0, NULL);
    g_esp += 4;
}

/* 0x0041EB70 set viewport (W, H): HUD scale +0xA8 = H/480 instead of W/640 */
extern void sub_0041EB70_gen(RECOMP_PARAMS);
void sub_0041EB70(RECOMP_PARAMS)
{
    uint32_t cam = ctx->r_ecx, W = MEM32(g_esp + 4), H = MEM32(g_esp + 8);
    sub_0041EB70_gen(ctx, base);
    if (H && (uint64_t)W * 3 > (uint64_t)H * 4)
        MEM32(cam + 0xA8) = (uint32_t)(((int64_t)H << 14) / 480);
}

/* 0x0041E7A0 per-frame projection scale + view rect: at W/H > 4:3 the scale
 * comes from the 4:3 width for this height, and the rect widens by A. */
extern void sub_0041E7A0_gen(RECOMP_PARAMS);
void sub_0041E7A0(RECOMP_PARAMS)
{
    uint32_t cam = ctx->r_ecx;
    sub_0041E7A0_gen(ctx, base);
    int32_t A = cam_aspect(cam);
    if (A == 0x4000)
        return;
    int32_t H = (int32_t)MEM32(cam + 0x6C);
    int32_t zoom = (int32_t)MEM32(cam + 0xA4);
    int32_t weff = (int32_t)(((int64_t)H * 640 / 480) << 14);
    MEM32(cam + 0x60) = (uint32_t)fxmul(weff, zoom);
    int32_t E = fxmul(fxmul(0x1C000 + (int32_t)MEM32(cam + 0xA0), fxdiv(0x4000, zoom)), 0x2000);
    int32_t Ex = fxmul(E, A);
    int32_t x = (int32_t)MEM32(cam + 0x98);
    int32_t x0 = x - Ex, x1 = x + Ex;
    if (x0 < 0) x0 = 0;
    if (x0 > 0x3FFFFF) x0 = 0x3FFFFF;
    if (x1 < 0) x1 = 0;
    if (x1 > 0x3FFFFF) x1 = 0x3FFFFF;
    MEM32(cam + 0x78) = (uint32_t)x0;
    MEM32(cam + 0x7C) = (uint32_t)x1;
    MEM32(cam + 0x20) = (uint32_t)(x0 - 0x3000);
    MEM32(cam + 0x24) = (uint32_t)(x1 + 0x3000);
}

/* hook 0x004721C2 (block renderer): tile x half-span [esp+0x18] *= A */
void gta2_ws_tile_span(RECOMP_PARAMS)
{
    int32_t A = render_aspect();
    if (A != 0x4000)
        MEM32(g_esp + 0x18) = (uint32_t)fxmul((int32_t)MEM32(g_esp + 0x18), A);
}

/* hook 0x0046BB9A: the per-layer tile list has 1500 slots and no bound */
static uint32_t g_ws_tile_peak;
int gta2_ws_tile_guard(RECOMP_PARAMS)
{
    uint32_t n = MEM32(ctx->r_edi + 0x2EFC);
    if (n > g_ws_tile_peak) {
        g_ws_tile_peak = n;
        if (n >= 1000)
            wr_log("widescreen: tile list peak %u of 1500", n);
    }
    return n >= 1500;   /* full: drop the tile (on_true -> the function's epilogue) */
}

/* hooks after `push 0x280` / `push 0x1E0` / `push &zero`: 640x480 bounds */
static uint32_t g_ws_lowbound;   /* guest Q14 for sub_0041E710's lower x bound */
void gta2_ws_vis_lo(RECOMP_PARAMS)       /* 0x0041E72E: x >= 320 - 320*A */
{
    int32_t A = render_aspect();
    if (A == 0x4000)
        return;
    if (!g_ws_lowbound)
        g_ws_lowbound = wr_galloc(4);
    MEM32(g_ws_lowbound) = (uint32_t)((320 << 14) - fxmul(320 << 14, A));
    MEM32(g_esp) = g_ws_lowbound;
}
void gta2_ws_vis_hi(RECOMP_PARAMS)       /* 0x0041E740: x < 320 + 320*A */
{
    int32_t A = render_aspect();
    if (A != 0x4000)
        MEM32(g_esp) = (uint32_t)(320 + ((320 * (int64_t)A) >> 14));
}
void gta2_ws_cull_w(RECOMP_PARAMS)       /* 0x004B9392: real width */
{
    uint32_t cam = MEM32(0x005E3CC4u);
    if (render_aspect() != 0x4000)
        MEM32(g_esp) = MEM32(cam + 0x68);
}
void gta2_ws_cull_h(RECOMP_PARAMS)       /* 0x004B93C0: real height */
{
    uint32_t cam = MEM32(0x005E3CC4u);
    if (render_aspect() != 0x4000)
        MEM32(g_esp) = MEM32(cam + 0x6C);
}

/* HUD primitives: anchor the virtual-640 x to the real left/centre/right */
enum { ANC_LEFT, ANC_CENTRE, ANC_RIGHT, ANC_BYX };
static const struct { uint32_t ret; uint8_t anc; } k_hud_anchor[] = {
    { 0x4C758F, ANC_LEFT }, { 0x4C75C4, ANC_LEFT }, { 0x4C75F9, ANC_LEFT }, { 0x4C7650, ANC_LEFT },
    { 0x4C76B1, ANC_LEFT }, { 0x4C7757, ANC_LEFT }, { 0x4C7791, ANC_LEFT }, { 0x4C77D2, ANC_LEFT },
    { 0x4C7813, ANC_LEFT }, { 0x4C787C, ANC_LEFT }, { 0x4C9492, ANC_LEFT }, { 0x4C94E2, ANC_LEFT },
    { 0x4C7BFB, ANC_RIGHT }, { 0x4C7C51, ANC_RIGHT }, { 0x4C7CA9, ANC_RIGHT }, { 0x4C89F9, ANC_RIGHT },
    { 0x4C9AAC, ANC_RIGHT }, { 0x4C9B3B, ANC_RIGHT },
    /* sub_004C9C20's icons ($, x, ...) take their x from the rolling counter
     * they belong to, which sub_0044B500 already anchored: no second shift */
    { 0x4C9D16, ANC_LEFT }, { 0x4C9E3D, ANC_LEFT }, { 0x4C9F42, ANC_LEFT }, { 0x4C9F8C, ANC_LEFT },
    { 0x4C9DA9, ANC_LEFT }, { 0x4C9EBD, ANC_LEFT },
    { 0x4C9BAB, ANC_RIGHT }, { 0x4C9C16, ANC_RIGHT },
    { 0x4C7AEF, ANC_CENTRE }, { 0x4C87B0, ANC_CENTRE }, { 0x4C8810, ANC_CENTRE }, { 0x4C8872, ANC_CENTRE },
    { 0x4C8A8E, ANC_CENTRE }, { 0x4C8BDE, ANC_CENTRE }, { 0x4C9557, ANC_CENTRE }, { 0x4C9590, ANC_CENTRE },
    { 0x4C95FD, ANC_CENTRE }, { 0x4C9638, ANC_CENTRE }, { 0x4C967C, ANC_CENTRE }, { 0x4C96E2, ANC_CENTRE },
    { 0x4C9742, ANC_CENTRE }, { 0x4C9901, ANC_CENTRE }, { 0x4C9944, ANC_CENTRE }, { 0x4C998F, ANC_CENTRE },
    { 0x4C99D5, ANC_CENTRE }, { 0x4C9A36, ANC_CENTRE }, { 0x4CA012, ANC_CENTRE }, { 0x4CA04C, ANC_CENTRE },
    { 0x4CA085, ANC_CENTRE }, { 0x4CA0E2, ANC_CENTRE }, { 0x4CA1C8, ANC_CENTRE }, { 0x4CA41B, ANC_CENTRE },
    { 0x4C79B3, ANC_CENTRE },
};

static void hud_anchor(X86Ctx *ctx, uint32_t xoff)
{
    int32_t A = render_aspect();
    if (A == 0x4000)
        return;
    uint32_t ret = MEM32(g_esp);
    int anc = ANC_BYX;
    for (size_t i = 0; i < sizeof k_hud_anchor / sizeof k_hud_anchor[0]; i++)
        if (k_hud_anchor[i].ret == ret) {
            anc = k_hud_anchor[i].anc;
            break;
        }
    int32_t x = (int32_t)MEM32(g_esp + xoff);
    if (anc == ANC_BYX)   /* data-placed counters, unlisted callers: by position */
        anc = x >= (427 << 14) ? ANC_RIGHT : x >= (213 << 14) ? ANC_CENTRE : ANC_LEFT;
    if (getenv("WR_HUD_LOG") && gta2_frames_presented() == (uint64_t)atoi(getenv("WR_HUD_LOG")))
        wr_log("hud: ret 0x%08X x %.1f anchor %d", ret, x / 16384.0, anc);
    int32_t extra = fxmul(640 << 14, A) - (640 << 14);   /* virtual width beyond 640 */
    if (anc == ANC_RIGHT)
        x += extra;
    else if (anc == ANC_CENTRE)
        x += extra / 2;
    MEM32(g_esp + xoff) = (uint32_t)x;
}

extern void sub_004C71B0_gen(RECOMP_PARAMS);
void sub_004C71B0(RECOMP_PARAMS) { hud_anchor(ctx, 12); sub_004C71B0_gen(ctx, base); }   /* (type, frame, x, ...) */
extern void sub_004C7280_gen(RECOMP_PARAMS);
void sub_004C7280(RECOMP_PARAMS) { hud_anchor(ctx, 8); sub_004C7280_gen(ctx, base); }    /* (str, x, ...) */
extern void sub_004BA2C0_gen(RECOMP_PARAMS);
void sub_004BA2C0(RECOMP_PARAMS) { hud_anchor(ctx, 8); sub_004BA2C0_gen(ctx, base); }    /* (str, x, y, font) */

/* 0x0044B500 rolling counter draw (thiscall, x, y as virtual ints): all four
 * callers (0x004C9C94, 0x004C9DBB, 0x004C9F01, 0x004C9F4F) are the right-hand
 * HUD group (money, multiplier, lives counters) - anchor right. */
extern void sub_0044B500_gen(RECOMP_PARAMS);
void sub_0044B500(RECOMP_PARAMS)
{
    int32_t A = render_aspect();
    if (A != 0x4000)
        MEM32(g_esp + 4) += (uint32_t)((fxmul(640 << 14, A) - (640 << 14)) >> 14);
    sub_0044B500_gen(ctx, base);
}

void gta2_manual_register(void)
{
    wr_manual_register(0x004031C0u, sub_004031C0);
    wr_manual_register(0x004CB150u, sub_004CB150);
}

/* ---- OPTIONS menu in the frontend (gta2_menu.c, docs/re/frontend.md) ---------------------- */

extern void sub_00453E20_gen(RECOMP_PARAMS);
void sub_00453E20(RECOMP_PARAMS)   /* builds every frontend page */
{
    uint32_t self = ctx->r_ecx;
    sub_00453E20_gen(ctx, base);
    uint32_t eax = ctx->r_eax;
    gta2_menu_frontend_built(self);
    ctx->r_eax = eax;
}

extern void sub_00455F90_gen(RECOMP_PARAMS);
void sub_00455F90(RECOMP_PARAMS)   /* picks the background picture */
{
    sub_00455F90_gen(ctx, base);
    gta2_menu_background();
}

extern void sub_004597C0_gen(RECOMP_PARAMS);
void sub_004597C0(RECOMP_PARAMS)   /* menu input step (thiscall fe) */
{
    uint32_t self = ctx->r_ecx;
    gta2_menu_input(ctx);          /* page 12: takes and clears the edge flags */
    ctx->r_ecx = self;
    sub_004597C0_gen(ctx, base);
    gta2_menu_after_input();
}

extern void sub_00457920_gen(RECOMP_PARAMS);
void sub_00457920(RECOMP_PARAMS)   /* draws the page's lines (inside Begin/EndScene) */
{
    sub_00457920_gen(ctx, base);
    uint32_t eax = ctx->r_eax;
    gta2_menu_draw(ctx);
    ctx->r_eax = eax;
}

extern void sub_00451930_gen(RECOMP_PARAMS);
void sub_00451930(RECOMP_PARAMS)   /* reads the registry Debug flags at start */
{
    sub_00451930_gen(ctx, base);
    uint32_t eax = ctx->r_eax;
    gta2_menu_apply_cheats();
    ctx->r_eax = eax;
}

/* ---- MODERN controls on foot (gta2_state.c) ----------------------------------------------- */
void gta2_modern_on_foot(uint32_t player, uint32_t ped);

extern void sub_004A5C50_gen(RECOMP_PARAMS);
void sub_004A5C50(RECOMP_PARAMS)   /* on-foot control step (thiscall player, ped; ret 4) */
{
    uint32_t player = ctx->r_ecx, ped = MEM32(ctx->r_esp + 4);
    sub_004A5C50_gen(ctx, base);
    uint32_t eax = ctx->r_eax;
    gta2_modern_on_foot(player, ped);
    ctx->r_eax = eax;
}

/* ---- the ANALOGUE feel: analog car steering (gta2_feel.c) --------------------------------------- */
void gta2_feel_car_step(void);
void gta2_feel_car_pre(void);

/* ANALOGUE: in one of the three turret vehicles the right stick points the turret. The game only turns it one fixed step a
 * tick, left or right (sub_00423720: Special 1 held + left / right; the step is a u16 at 0x5E4D34 / 0x5E4D60 / 0x5E501C, left
 * adds it to the turret's Ang16 at (component + 0x10), right subtracts it; the component is found in the car's list by type id
 * 0x72 / 0x94 / 0xF8 with sub_004BE980). So each tick the angle left to the stick's direction decides left, right or nothing.
 * The stick's angle is a direction on the screen; the car's heading u16 [[car+0x50]+0] is subtracted because the turret's
 * angle is assumed relative to the body (not checked: settings.ini turret_world = 1 if it is the world's). The horn
 * edge byte is never touched. A push past the dead zone also fires. */
static int turret_type(X86Ctx *ctx, uint32_t car)   /* 0 = no turret, else 1..3 */
{
    uint32_t ecx = ctx->r_ecx, eax = ctx->r_eax;
    static const uint32_t fn[3] = { 0x004118F0u, 0x00411900u, 0x00411910u };
    int r = 0;
    for (int i = 0; i < 3 && !r; i++) {
        ctx->r_ecx = car;
        if (wr_call_guest(ctx, fn[i], 0, NULL) & 0xFF)
            r = i + 1;
    }
    ctx->r_ecx = ecx;
    ctx->r_eax = eax;
    return r;
}

static uint32_t turret_node(X86Ctx *ctx, uint32_t car, int type)
{
    static const uint32_t id[3] = { 0x72, 0x94, 0xF8 };
    uint32_t ecx = ctx->r_ecx, eax = ctx->r_eax;
    ctx->r_ecx = car;
    uint32_t arg = id[type - 1];
    uint32_t node = wr_call_guest(ctx, 0x004BE980u, 1, &arg);
    ctx->r_ecx = ecx;
    ctx->r_eax = eax;
    return node;
}

static int g_turret_on;
int gta2_turret_active(void) { return g_turret_on; }

static void gta2_turret_pre(X86Ctx *ctx)
{
    g_turret_on = 0;
    /* ENHANCEMENTS > TURRET AIM (PR #7's switch, on its own since 2026-11-28), single player only */
    if (!opt_get(OPT_TURRET_AIM) || wr_rd32(0x00673E2Cu) || gta2_pause_open() || gta2_pan_held() ||
        !gta2_player_driving())
        return;
    uint32_t ped = gta2_player_ped();
    if (!ped || !wr_mem_valid(ped, 0x170))
        return;
    uint32_t player = wr_rd32(ped + 0x15C), car = wr_rd32(ped + 0x16C);
    if (!player || !car || !wr_mem_valid(player, 0x90) || !wr_mem_valid(car, 0x90))
        return;
    uint32_t spr = wr_rd32(car + 0x50);
    if (!spr || !wr_mem_valid(spr, 4))
        return;
    int type = turret_type(ctx, car);
    if (!type)
        return;
    uint32_t node = turret_node(ctx, car, type);
    if (!node || !wr_mem_valid(node, 0x14))
        return;
    g_turret_on = 1;   /* the right stick's left / right now also press the game's own ATTACK (gta2_input.c) */
    static const uint32_t step_at[3] = { 0x005E4D34u, 0x005E4D60u, 0x005E501Cu };
    int step = (int)(wr_rd16(step_at[type - 1]) % 1440);
    if (step <= 0 || step > 200)
        step = 20;
    int turret = wr_rd16(node + 0x10) % 1440, heading = wr_rd16(spr) % 1440;

    static int world = -1;   /* settings.ini turret_world = 1 if the turret's angle is the world's, not the body's */
    if (world < 0)
        world = wr_settings_get_int("turret_world", 0) ? 1 : 0;

    float rx = 0, ry = 0;
    wr_pad_stick_right(&rx, &ry);
    float mag = sqrtf(rx * rx + ry * ry);
    if (mag < 0.3f)   /* the dead zone: it stays, nothing fires */
        return;
    wr_wr8(player + 0x7C, 1);   /* fires while the stick is out */
    int aim = ((int)lroundf(atan2f(rx, ry) * (720.0f / 3.14159265f)) % 1440 + 1440) % 1440;   /* the screen direction */
    int want = ((world ? aim + 720 : aim - heading + 720) % 1440 + 1440) % 1440;   /* + 720: the turret's zero is the other way round */
    int d = ((want - turret + 2160) % 1440) - 720;   /* + = turn left (the game adds the step), - = right */
    static int n;
    if (n++ < 12)   /* the first aims only (review 2026-11-28: every 60 ticks for ever was log spam in a hot path) */
        wr_log("turret: type %d stick aim %d heading %d turret %d want %d diff %d step %d (%s)", type, aim, heading, turret,
               want, d, step, world ? "world" : "relative");
    if (d > -step / 2 && d < step / 2)
        return;   /* close enough: no jitter */
    wr_wr8(player + 0x81, 1);                 /* Special 1 held */
    wr_wr8(player + (d > 0 ? 0x7A : 0x7B), 1);
}

extern void sub_004A4C40_gen(RECOMP_PARAMS);
void sub_004A4C40(RECOMP_PARAMS)   /* the player's car control step (docs/re/gameplay_input_pause.md 3.3) */
{
    gta2_feel_car_pre();   /* ANALOGUE: the trigger pulls become the held forward / back bytes first */
    gta2_turret_pre(ctx);  /* ANALOGUE: right stick left / right = the turret (Special 1 held + left / right) */
    sub_004A4C40_gen(ctx, base);
    uint32_t eax = ctx->r_eax;
    gta2_feel_car_step();   /* after the game set the steering byte; nothing happens with the ANALOGUE feel = DEFAULT */
    ctx->r_eax = eax;
}

/* ---- the pause menu over the HUD ---------------------------------------------------------- */
extern void sub_004CA440_gen(RECOMP_PARAMS);
void sub_004CA440(RECOMP_PARAMS)   /* draws the HUD each frame (thiscall HUD) */
{
    gta2_camera_keys();             /* CAMERA KEYS: Page Up / Down, MODERN d-pad up / down */
    gta2_camera_distance_apply();   /* CAMERA DISTANCE: writes only when the setting changed */
    gta2_fixed_camera_apply();      /* FIXED CAMERA: the fixed height of the player's cameras */
    gta2_saveany_frame(gta2_pause_open());   /* SAVE ANYWHERE: the height-still counter */
    gta2_saveany_game_running(ctx);          /* ... and the save name after a LOAD GAME */
    gbh_daynight_world_end();       /* DAY / NIGHT: the world is drawn - the HUD's sprites carry the same light flag */
    gta2_radar_draw(ctx);   /* MINIMAP: over the world, under all of the game's HUD (texts, bars, panels) */
    sub_004CA440_gen(ctx, base);
    uint32_t eax = ctx->r_eax;
    gta2_radio_label_draw(ctx);   /* MIXTAPE: station + song for a moment */
    gta2_sprint_bar_draw();       /* SPRINT BAR: stamina, on foot */
    gta2_pan_arrows_draw();       /* ANALOGUE: the pan arrows */
    gta2_pause_draw(ctx);
    ctx->r_eax = eax;
}

extern void sub_004C9FA0_gen(RECOMP_PARAMS);
/* The HUD's navigation arrows (thiscall arrow manager, plain ret; only drawing, docs/re/minimap.md 2.3):
 * NAVIGATION ARROWS decides which of the game's arrows show. */
extern void sub_004C84C0_gen(RECOMP_PARAMS);
void sub_004C84C0(RECOMP_PARAMS)
{
    uint32_t mgr = ctx->r_ecx;
    uint32_t hidden = gta2_radar_arrows_hide(mgr);   /* bit i: arrow i hidden for this draw */
    if (hidden == 0xFFFFFFFFu) {
        ctx->r_esp += 4;   /* ret: no arrows */
        return;
    }
    sub_004C84C0_gen(ctx, base);
    gta2_radar_arrows_restore(mgr, hidden);
}

void sub_004C9FA0(RECOMP_PARAMS)   /* the game's PAUSED panel (thiscall, plain ret) */
{
    if (gta2_pause_hide_panel()) {
        ctx->r_esp += 4;   /* ret */
        return;
    }
    sub_004C9FA0_gen(ctx, base);
}

/* ---- twin-stick walking (gta2_state.c) ----------------------------------------------------- */
int gta2_twin_move_angle(uint32_t obj);
float gta2_sprint_factor(uint32_t obj);

extern void sub_0041FC20_gen(RECOMP_PARAMS);
void sub_0041FC20(RECOMP_PARAMS)   /* (u16 *angle, speed *, out dx *, out dy *) stdcall: a vector along an angle */
{
    uint32_t ret = MEM32(ctx->r_esp);
    /* the player ped's walk step / velocity from its heading (object in
     * esi, angle = [esi+0x40]); measured: 0x0049ACBA and 0x0049AEA7 */
    if ((ret == 0x0049ACBAu || ret == 0x0049AEA7u) && MEM32(ctx->r_esp + 4) == ctx->r_esi + 0x40u) {
        float f = gta2_sprint_factor(ctx->r_esi);
        if (f != 1.0f) {   /* SPRINT: the speed (arg 2, Q14) scaled for this call */
            static uint32_t sp_tmp;
            if (!sp_tmp)
                sp_tmp = wr_galloc(16);
            MEM32(sp_tmp) = (uint32_t)(int32_t)((float)(int32_t)MEM32(MEM32(ctx->r_esp + 8)) * f);
            MEM32(ctx->r_esp + 8) = sp_tmp;
        }
    }
    if ((ret == 0x0049ACBAu || ret == 0x0049AEA7u || ret == 0x0049A80Bu) &&
        MEM32(ctx->r_esp + 4) == ctx->r_esi + 0x40u) {
        int a = gta2_twin_move_angle(ctx->r_esi);
        if (a >= 0) {
            static uint32_t tmp;
            if (!tmp)
                tmp = wr_galloc(16);
            MEM16(tmp) = (uint16_t)a;
            MEM32(ctx->r_esp + 4) = tmp;   /* move along the left stick, keep facing the right one */
        }
    }
    sub_0041FC20_gen(ctx, base);
}

/* ---- GAMEPLAY: KEEP BODIES, MORE BLOOD (gta2_gameplay.c) ----------------------------------- */

extern void sub_004454E0_gen(RECOMP_PARAMS);
static void ped_tick_inner(RECOMP_PARAMS)
{
    uint32_t ped = ctx->r_ecx;
    int keep = gta2_body_before_tick(ped);
    if (keep == 2) {   /* KEEP BODIES: its time is up - the game's own removal */
        uint32_t esp = ctx->r_esp;
        ctx->r_ecx = ped;
        wr_call_guest(ctx, 0x0043E650u, 0, NULL);
        ctx->r_esp = esp;
        ctx->r_ecx = ped;
        keep = 0;
    }
    if (!keep) {
        sub_004454E0_gen(ctx, base);
        return;
    }
    uint8_t all = MEM8(0x005E5BEAu);
    uint16_t shown = MEM16(0x005E5BF4u);
    ctx->r_ecx = ped;
    sub_004454E0_gen(ctx, base);
    MEM8(0x005E5BEAu) = all;      /* a kept body does not count against the ped limits */
    MEM16(0x005E5BF4u) = shown;
}

void sub_004454E0(RECOMP_PARAMS)   /* one ped's tick (thiscall ped) */
{
    uint32_t ped = ctx->r_ecx;
    int punch = gta2_punch_swap(ped);   /* ANALOGUE: PUNCH puts the weapon away for this tick */
    ctx->r_ecx = ped;
    ped_tick_inner(ctx, base);
    if (punch)
        gta2_punch_restore(ped);
}

extern void sub_0048BE60_gen(RECOMP_PARAMS);
void sub_0048BE60(RECOMP_PARAMS)   /* the blood pool particle's tick (thiscall particle) */
{
    uint32_t p = ctx->r_ecx;
    gta2_blood_before_tick(p);
    ctx->r_ecx = p;
    sub_0048BE60_gen(ctx, base);
}


#include "render/gta2_render.h"

extern void sub_004BE060_gen(RECOMP_PARAMS);
uint32_t g_gta2_sprite_draws;   /* sprites drawn (frame-rate experiments) */

/* DAY / NIGHT: headlights off in the day (maintainer 2026-11-27) - the game's own "light broken" mechanism, at draw
 * time only. A car's delta mask car+8 holds the lights: bit 6 / 23 = left / right headlight, 11-14 / 28-31 the
 * pop-up frames (car info +0xD flag 2); a frontal crash (sub_00422F00 corners 0 / 1) clears them and the game never
 * sets them again for that corner. sub_004BE060 itself saves and restores car+8 around its draw (0x004BE224,
 * 0x004BE2E4 / 0x004BE518), so masking it here around the call is never seen by the simulation: at night the real
 * mask is back, damaged corners stay dark, brake lights (5 / 22) are untouched. Sprite = car when spr+0x30 == 2,
 * car = spr+8; car info = [[style 0x670684]+0x5C][model car+0x84]. */
static uint32_t daylight_mask_begin(uint32_t spr, uint32_t *saved)
{
    if (!gta2_daynight_lights_off() || !wr_mem_valid(spr + 0x30, 4) || MEM32(spr + 0x30) != 2)
        return 0;
    uint32_t car = MEM32(spr + 8);
    if (!car || !wr_mem_valid(car, 0x88))
        return 0;
    /* maintainer 2026-11-27: in the day every lamp off EXCEPT the sirens and the rear lights (they are the brake
     * lights too). docs/re/car_lights.md: keep 5 / 22 (brake), 16 + 15 on siren cars (the two siren phases), 17 / 18
     * (rear emergency lamps); off 6 / 23 (headlights), the pop-up frames (car info +0xD & 2), bit 15 when it is a
     * roof lamp (+0xC & 4: bus, limos, tow truck) or a taxi sign (+0xC & 0x20) - never on a siren car (+0xC & 2 or
     * model 0x54) or a decal car (+0xC & 0x40: painted, not a lamp). Lamps painted into a base sprite (U-Jerk Truck,
     * model 69) have no bit and stay. */
    uint32_t off = (1u << 6) | (1u << 23);   /* left / right headlight */
    uint32_t style = MEM32(0x670684), tab = style ? MEM32(style + 0x5C) : 0;
    if (tab) {
        uint32_t model = MEM8(car + 0x84), ci = MEM32(tab + 4u * model);
        if (ci && wr_mem_valid(ci + 0xC, 2)) {
            uint8_t f1 = MEM8(ci + 0xC), f2 = MEM8(ci + 0xD);
            if (f2 & 2)
                off |= 0x7800u | 0xF0000000u;   /* pop-up frames 11-14 and 28-31 */
            int siren_car = (f1 & 0x02) || model == 0x54;
            if (!siren_car && !(f1 & 0x40) && (f1 & (0x04 | 0x20)))
                off |= 1u << 15;                 /* roof lamp / taxi sign */
        }
    }
    *saved = MEM32(car + 8);
    MEM32(car + 8) = *saved & ~off;
    return car;
}

/* ---- SAVE ANYWHERE: LOAD GAME's slot name (issue #6; gta2_saveany.c) ------------------------------------------
 * sub_004528A0: void __stdcall (u8 slot, char *out), ret 8 - writes "player\plyslotN.svg"; while LOAD GAME runs
 * the frontend's own load path, the save-anywhere slot "player\anyslotN.svg" instead. */
extern void sub_004528A0_gen(RECOMP_PARAMS);
void sub_004528A0(RECOMP_PARAMS)
{
    if (!gta2_saveany_redirect()) {
        sub_004528A0_gen(ctx, base);
        return;
    }
    uint32_t sp = ctx->r_esp, out = MEM32(sp + 8);
    char p[40];
    snprintf(p, sizeof p, "player\\anyslot%u.svg", MEM32(sp + 4) & 0xFF);
    if (out && wr_mem_valid(out, (uint32_t)strlen(p) + 1))
        memcpy(wr_ptr(out), p, strlen(p) + 1);
    ctx->r_esp = sp + 12;   /* the original is not called (the name is ours); ret 8: return address + both args */
}

/* ---- FIXED CAMERA (issue #6; gta2_gameplay.c) ------------------------------------------------------------------
 * sub_0041EBF0 (thiscall camera; car, &x, &y, &z; ret 0x10) is the follow camera's car part: above a speed
 * threshold it adds the speed zoom (z += 16 x |v|) and the look-ahead (x / y += the lead turned by the velocity
 * heading). With FIXED CAMERA on, for the player's own cameras, the target goes back to what it was before the
 * call: no speed zoom, no lead - the car in the middle. GTA2_CAM_LOG=1 logs the largest speed raise (to tune the
 * fixed height, gta2_gameplay.c FIXED_HEIGHT_Q14). */
extern void sub_0041EBF0_gen(RECOMP_PARAMS);
void sub_0041EBF0(RECOMP_PARAMS)
{
    uint32_t cam = ctx->r_ecx, sp = ctx->r_esp;
    uint32_t px = 0, py = 0, pz = 0, x0 = 0, y0 = 0, z0 = 0;
    int ok = wr_mem_valid(sp + 4, 0x10);
    if (ok) {
        px = MEM32(sp + 8);
        py = MEM32(sp + 0xC);
        pz = MEM32(sp + 0x10);
        ok = px && py && pz && wr_mem_valid(px, 4) && wr_mem_valid(py, 4) && wr_mem_valid(pz, 4);
    }
    if (ok) {
        x0 = MEM32(px);
        y0 = MEM32(py);
        z0 = MEM32(pz);
    }
    sub_0041EBF0_gen(ctx, base);
    if (!ok || !gta2_is_player_camera(cam))
        return;
    static int log = -1;
    static int32_t peak;
    if (log < 0)
        log = getenv("GTA2_CAM_LOG") != NULL;
    int32_t raise = (int32_t)MEM32(pz) - (int32_t)z0;
    if (log && raise > peak) {
        peak = raise;
        wr_log("camera: speed raise peak %.3f (Q14 0x%X)", peak / 16384.0, (unsigned)peak);
    }
    if (!gta2_fixed_camera_on())
        return;
    uint32_t eax = ctx->r_eax;
    MEM32(px) = x0;   /* no lead: the car in the middle */
    MEM32(py) = y0;
    MEM32(pz) = z0;   /* no speed zoom: the fixed height comes from cam+0x40 */
    ctx->r_eax = eax;
}

/* ---- painted headlamps (maintainer 2026-11-27: "the right one should be off") ---------------------------------
 * Some car sprites have their lamps painted into the base picture (U-Jerk Truck, model 69): the game switches the
 * headlight delta off (day, crash damage) but the painted lamp still shines. sub_0044AEA0 applies a car sprite's
 * deltas for every cache build and in-place patch (docs/re/car_lights.md); after it, for a headlight bit (6, or 23
 * mirrored) that is OFF in the full mask being built, the delta-6 pixel positions get the body pixel two rows
 * toward the car's middle (sprites face down: a smaller row; 8-bit palette indices, safe for every remap). When
 * the bit comes on later, the in-place patch writes delta 6 over exactly these pixels; when it goes off, the cache
 * must rebuild (no subset entry has the bit) - so the picture always follows the bit. Only with DAY / NIGHT on
 * (the original look otherwise), never on pop-up cars (their lowered look is the base sprite). */
static int g_lamp_patch;   /* set per car draw in sub_004BE060: patch this car's lamps */
static int g_lamp_model;   /* that car's model (car+0x84) */

/* Models whose lamps are painted beyond the headlight delta (maintainer: "the U-Jerk Truck: all 4 lamps lit at
 * night, none in the day"): per side the painted lamp rectangles in sprite pixels (front at the bottom; the
 * image's right half is the car's left = bit 6, the left half bit 23). Off = filled from the body two rows
 * toward the middle. Rendered from wil.sty sprite 62 (docs/re/car_lights.md). */
typedef struct { uint8_t x0, x1, y0, y1; } lamp_rect;
typedef struct { int model; lamp_rect left[2], right[2]; } lamp_model;   /* left = bit 6, right = bit 23 */
static const lamp_model k_lamp_models[] = {
    { 69, { { 31, 33, 53, 55 }, { 36, 38, 52, 55 } },   /* U-Jerk Truck: inner lamp, outer corner lamp */
          { { 20, 22, 53, 55 }, { 15, 17, 52, 55 } } },
};

static const lamp_model *lamp_model_of(int model)
{
    for (size_t i = 0; i < sizeof k_lamp_models / sizeof k_lamp_models[0]; i++)
        if (k_lamp_models[i].model == model)
            return &k_lamp_models[i];
    return NULL;
}

/* body layer: the lamp area gets the nearest body pixel above it in its column (2..8 rows up, skipping transparent
 * ones - the outer corners are rounded, two rows up is outside the car there; maintainer test 11-27) */
static void lamp_rects_off(const lamp_rect *r, int n, uint32_t dst)
{
    for (int k = 0; k < n; k++)
        for (uint32_t c = r[k].x0; c <= r[k].x1; c++) {
            uint8_t v = 0;
            for (uint32_t up = 2; up <= 8 && !v && r[k].y0 >= up; up++)
                v = MEM8(dst + (r[k].y0 - up) * 256u + c);
            if (!v)   /* nothing above in this column: the neighbour column's choice */
                for (uint32_t up = 2; up <= 8 && !v && r[k].y0 >= up; up++)
                    v = MEM8(dst + (r[k].y0 - up) * 256u + (c > r[k].x0 ? c - 1 : c + 1));
            if (!v)
                continue;
            for (uint32_t y = r[k].y0; y <= r[k].y1; y++)
                if (MEM8(dst + y * 256u + c))   /* keep transparent pixels (a dent may have cut the corner) */
                    MEM8(dst + y * 256u + c) = v;
        }
}

/* GTA2_LAMP_DUMP=1: a lamp-model car's picture before / after the lamp patch as data/lampdump_<n>_<layer>_<pre|post>.pgm
 * (8-bit palette indices as grey, 64 rows; the first 24 builds) - which pixels the patch really changed */
static void lamp_dump(uint32_t dst, uint32_t w, int light_layer, const char *when)
{
    static int on = -1, n;
    if (on < 0)
        on = getenv("GTA2_LAMP_DUMP") != NULL;
    if (!on || n >= 48 || !w || w > 256)
        return;
    char path[1200];
    snprintf(path, sizeof path, "%s/lampdump_%02d_%s_%s.pgm", g_wr_paths.data, n / 2, light_layer ? "light" : "body",
             when);
    if (when[1] == 'o')   /* "post": the pair is complete */
        n += 2;
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P5\n%u 64\n255\n", w);
    for (uint32_t y = 0; y < 64; y++)
        for (uint32_t x = 0; x < w; x++)
            fputc(MEM8(dst + y * 256u + x), f);
    fclose(f);
}

/* the lamp's lit colour: the first non-zero pixel of the sprite's headlight delta 6 (the game's own lit lamp) */
static uint8_t lamp_lit_index(uint32_t spr)
{
    uint32_t style = MEM32(0x670684), tbl = style ? MEM32(style + 0x54) : 0, e;
    if (!tbl || spr >= MEM16(tbl) || !(e = MEM32(tbl + 4 + 4 * spr)) || MEM8(e + 2) <= 6)
        return 0;
    uint32_t rec = e + 4 + 6 * 8, p = MEM32(rec), end = p + MEM16(rec + 4);
    if (!p || !wr_mem_valid(p, end - p))
        return 0;
    while (p + 3 <= end) {
        uint32_t len = MEM8(p + 2);
        for (uint32_t j = 0; j < len; j++)
            if (MEM8(p + 3 + j))
                return MEM8(p + 3 + j);
        p += 3 + len;
    }
    return 0;
}

/* light layer (lighting on: sub_004BE060 draws the body, then a layer of only the light deltas over a transparent
 * base): a side whose headlight is on gets its whole painted lamps there in the lit colour - all four lamps glow
 * at night, not only the delta's few pixels */
static void lamp_rects_on(const lamp_rect *r, int n, uint32_t dst, uint8_t lit)
{
    for (int k = 0; k < n; k++)
        for (uint32_t y = r[k].y0; y <= r[k].y1; y++)
            for (uint32_t c = r[k].x0; c <= r[k].x1; c++)
                MEM8(dst + y * 256u + c) = lit;
}

static void lamp_off(uint32_t spr, uint32_t dst, uint32_t w, int mirror)
{
    uint32_t style = MEM32(0x670684), tbl = style ? MEM32(style + 0x54) : 0, e, cur = 0, n = 0;
    uint16_t pos[256];
    uint8_t top[256];
    if (!tbl || spr >= MEM16(tbl) || !(e = MEM32(tbl + 4 + 4 * spr)) || MEM8(e + 2) <= 6)
        return;
    uint32_t rec = e + 4 + 6 * 8, p = MEM32(rec), end = p + MEM16(rec + 4);
    if (!p || !wr_mem_valid(p, end - p))
        return;
    memset(top, 0xFF, sizeof top);
    while (p + 3 <= end && n < 256) {   /* {u16 skip, u8 len, len bytes} (sub_004C0290) */
        uint32_t len = MEM8(p + 2);
        cur += MEM16(p);
        for (uint32_t j = 0; j < len && n < 256; j++, cur++) {
            uint32_t r = cur >> 8, c = cur & 0xFF;
            pos[n++] = (uint16_t)cur;
            if (r < top[c])
                top[c] = (uint8_t)r;
        }
        p += 3 + len;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t r = pos[i] >> 8, c = pos[i] & 0xFF, cm = mirror ? w - 1 - c : c;   /* sub_004C02D0's mirror */
        if (top[c] < 2 || cm >= 256)
            continue;
        uint8_t v = MEM8(dst + (top[c] - 2u) * 256u + cm);
        if (v)   /* 0 = transparent: leave it */
            MEM8(dst + r * 256u + cm) = v;
    }
}

extern void sub_0044AEA0_gen(RECOMP_PARAMS);
void sub_0044AEA0(RECOMP_PARAMS)   /* applies a car sprite's deltas (stdcall 4 args, ecx = the full mask*) */
{
    uint32_t sp = ctx->r_esp;   /* the args before _gen pops them: +4 u16* sprite, +8 dest, +0xC mask*, +0x10 u8* w */
    int on = g_lamp_patch;
    uint32_t spr = 0, dst = 0, w = 0, full = 0;
    if (on && wr_mem_valid(sp + 4, 0x10) && ctx->r_ecx) {
        spr = MEM16(MEM32(sp + 4));
        dst = MEM32(sp + 8);
        w = MEM8(MEM32(sp + 0x10));
        full = MEM32(ctx->r_ecx);
    } else {
        on = 0;
    }
    sub_0044AEA0_gen(ctx, base);
    if (on && dst && w) {
        uint32_t eax = ctx->r_eax;
        const lamp_model *lm = lamp_model_of(g_lamp_model);
        if (lm) {   /* painted lamps known per model: the whole lamp per side follows its headlight bit */
            /* light layer = a transparent base with only light deltas: the middle column is empty from top to
             * bottom (lamps and brake lights sit off-centre); the body layer has the car there. (The sprite's
             * height byte after the width was not reliable: the body was taken for the light layer - test 11-27.) */
            int light_layer = 1;
            for (uint32_t y = 0; y < 64 && light_layer; y++)
                if (MEM8(dst + y * 256u + w / 2))
                    light_layer = 0;
            static int lamp_log = -1;
            if (lamp_log < 0)
                lamp_log = getenv("GTA2_LIGHT_LOG") != NULL;
            if (lamp_log)
                wr_log("lamps: model %d sprite %u w %u %s layer, mask %08X", g_lamp_model, spr, w,
                       light_layer ? "light" : "body", full);
            lamp_dump(dst, w, light_layer, "pre");
            if (light_layer) {
                /* all four only at full night; sunrise / sunset: the game's own headlight delta alone - the two
                 * middle lamps, as the original (maintainer 2026-11-27). The light layer's cache key holds the
                 * light bits only, so a change of phase must not reuse it: the hour decides per build, and the
                 * game rebuilds when the bits change (day <-> sunset); sunset -> night keeps the bits, so the
                 * sub_004BE060 wrapper sets bit 18 for the draw at full night (a lamp-model car has no delta 18
                 * and the game never sets it there: in the light mask 0xC78060, so the light layer's key changes) */
                uint8_t lit = (full & (1u << 18)) ? lamp_lit_index(spr) : 0;   /* bit 18 = full night marker */
                if (lit && (full & (1u << 6)))
                    lamp_rects_on(lm->left, 2, dst, lit);
                if (lit && (full & (1u << 23)))
                    lamp_rects_on(lm->right, 2, dst, lit);
            } else {
                if (!(full & (1u << 6)))
                    lamp_rects_off(lm->left, 2, dst);
                if (!(full & (1u << 23)))
                    lamp_rects_off(lm->right, 2, dst);
            }
            lamp_dump(dst, w, light_layer, "post");
        } else {    /* else: the headlight delta's own pixels */
            if (!(full & (1u << 6)))
                lamp_off(spr, dst, w, 0);
            if (!(full & (1u << 23)))
                lamp_off(spr, dst, w, 1);
        }
        ctx->r_eax = eax;
    }
}

/* GTA2_LIGHT_LOG=1: the player's car's delta mask (car+8), light flags (car+0xA4), model and car info flags in the
 * log whenever one changes - which bits a model lights, which ones a crash clears (docs/re/car_lights.md) */
static void light_log(uint32_t spr)
{
    static int on = -1;
    static uint32_t last_car, last_mask, last_a4;
    if (on < 0)
        on = getenv("GTA2_LIGHT_LOG") != NULL;
    if (!on || !wr_mem_valid(spr + 0x30, 4) || MEM32(spr + 0x30) != 2)
        return;
    uint32_t car = MEM32(spr + 8), ped = gta2_player_ped();
    if (!car || !ped || !wr_mem_valid(ped, 0x170) || MEM32(ped + 0x16C) != car || !wr_mem_valid(car, 0xA8))
        return;
    uint32_t mask = MEM32(car + 8), a4 = MEM32(car + 0xA4), model = MEM8(car + 0x84);
    if (car == last_car && mask == last_mask && a4 == last_a4)
        return;
    uint32_t style = MEM32(0x670684), tab = style ? MEM32(style + 0x5C) : 0, ci = tab ? MEM32(tab + 4u * model) : 0;
    int cic = ci && wr_mem_valid(ci + 0xC, 2) ? MEM8(ci + 0xC) : -1, cid = ci && wr_mem_valid(ci + 0xD, 1) ? MEM8(ci + 0xD) : -1;
    wr_log("lights: car %08X model %u mask %08X a4 %08X state %u ci+C %02X ci+D %02X (day off %d)", car, model, mask,
           a4, MEM32(car + 0x9C), cic & 0xFF, cid & 0xFF, gta2_daynight_lights_off());
    last_car = car;
    last_mask = mask;
    last_a4 = a4;
}

void sub_004BE060(RECOMP_PARAMS)   /* draws one sprite through gbh_DrawQuad (thiscall sprite) */
{
    g_gta2_sprite_draws++;
    float sc, ti, al;
    uint32_t spr = ctx->r_ecx;
    light_log(spr);
    uint32_t lights_saved = 0, lights_car = daylight_mask_begin(spr, &lights_saved);
    g_lamp_patch = 0;   /* painted headlamps follow the bits: DAY / NIGHT on, a car, not a pop-up car */
    if (opt_get(OPT_DAY_NIGHT) && wr_mem_valid(spr + 0x30, 4) && MEM32(spr + 0x30) == 2) {
        uint32_t car = MEM32(spr + 8), style = MEM32(0x670684), tab = style ? MEM32(style + 0x5C) : 0;
        uint32_t ci = car && tab && wr_mem_valid(car + 0x84, 1) ? MEM32(tab + 4u * MEM8(car + 0x84)) : 0;
        g_lamp_patch = ci && wr_mem_valid(ci + 0xD, 1) && !(MEM8(ci + 0xD) & 2);
        g_lamp_model = g_lamp_patch ? MEM8(car + 0x84) : -1;
        /* lamp-model cars, for this draw only: bit 20 always (the body layer's mask is then never 0 - with 0 the
         * game builds the body without sub_0044AEA0 and the painted lamps stayed in the day, test 11-27; no delta
         * 20 on these cars, not in the light mask), bit 18 at full night (the light layer paints all four lamps) */
        if (g_lamp_patch && lamp_model_of(g_lamp_model)) {
            if (!lights_car) {
                lights_car = car;
                lights_saved = MEM32(car + 8);
            }
            MEM32(car + 8) |= 1u << 20;
            if (gta2_daynight_full_night())
                MEM32(car + 8) |= 1u << 18;
        }
    }
    int moved = gta2_fps_sprite_before(spr);   /* FRAME RATE: blended position */
    gta2_sprite_look(spr, &sc, &ti, &al);
    if (sc == 1.0f && ti == 1.0f && al == 1.0f) {
        sub_004BE060_gen(ctx, base);
    } else {
        g_port_quad_scale = sc;
        g_port_quad_tint = ti;
        g_port_quad_alpha = al;
        sub_004BE060_gen(ctx, base);   /* its quads are changed as they are emitted */
        g_port_quad_scale = g_port_quad_tint = g_port_quad_alpha = 1.0f;
    }
    if (moved)
        gta2_fps_sprite_after(spr);
    if (lights_car)
        MEM32(lights_car + 8) = lights_saved;   /* the real lights back */
    g_lamp_patch = 0;
}

/* ---- PEDESTRIANS 1x..4x (gta2_gameplay.c) ------------------------------------------------ */
extern void sub_004416B0_gen(RECOMP_PARAMS);
void sub_004416B0(RECOMP_PARAMS)   /* the ped manager's constructor: a new game */
{
    sub_004416B0_gen(ctx, base);
    uint32_t eax = ctx->r_eax;   /* the new manager (the caller stores it in [0x5E5BC0]) */
    gta2_peds_game_start(ctx, eax);
    ctx->r_eax = eax;
}

extern void sub_00403890_gen(RECOMP_PARAMS);
void sub_00403890(RECOMP_PARAMS)   /* ped pool pop (thiscall pool): it had no test for an empty pool */
{
    if (!MEM32(ctx->r_ecx)) {
        static int n;
        if (n++ < 5)
            wr_log("pedestrians: ped pool empty - no new ped");
        ctx->r_eax = 0;
        ctx->r_esp += 4;   /* ret */
        return;
    }
    sub_00403890_gen(ctx, base);
}

extern void sub_004415E0_gen(RECOMP_PARAMS);
void sub_004415E0(RECOMP_PARAMS)   /* the ambient ped spawner (plain ret) */
{
    int k = gta2_ped_factor();
    if (getenv("WR_GAMEPLAY_LOG")) {
        static int n;
        uint32_t m = MEM32(0x005E5BC0u);
        if (m && n++ % 150 == 0)
            wr_log("pedestrians: %u ambient, %u on screen (cap %u, %dx)", MEM8(0x005E5BEAu), MEM8(m + 6), MEM16(m), k);
    }
    if (k == 1) {
        sub_004415E0_gen(ctx, base);
        return;
    }
    uint32_t ret = MEM32(ctx->r_esp), mgr = MEM32(0x005E5BC0u);
    for (int i = 0; i < k && mgr; i++) {
        if (!gta2_peds_spawn_ok())
            break;   /* keep room for the game's unchecked allocations */
        uint8_t shown = MEM8(mgr + 6);
        MEM8(mgr + 6) = (uint8_t)(shown / k);   /* on-screen cap: density / 25 * K */
        uint32_t sp = ctx->r_esp;
        MEM32(ctx->r_esp) = ret;
        sub_004415E0_gen(ctx, base);           /* pops its return address */
        ctx->r_esp = sp;
        MEM8(mgr + 6) = shown;
    }
    ctx->r_esp += 4;   /* ret */
}

/* ---- TRAFFIC 1x..4x (gta2_gameplay.c) ------------------------------------------------------ */
extern void sub_004B4A60_gen(RECOMP_PARAMS);
void sub_004B4A60(RECOMP_PARAMS)   /* the traffic generator for one player camera */
{
    int k = gta2_traffic_factor();
    uint32_t m = MEM32(0x005E4CA4u);
    if (k == 1 || !m) {
        sub_004B4A60_gen(ctx, base);
        return;
    }
    /* the cap "+0x40 + +0x28 == 16" sees the counts divided by K */
    int32_t a = (int32_t)MEM32(m + 0x40), b = (int32_t)MEM32(m + 0x28), total = a + b;
    int32_t sa = 0, sb = total >= 16 * k ? 16 : total / k;
    MEM32(m + 0x40) = (uint32_t)sa;
    MEM32(m + 0x28) = (uint32_t)sb;
    sub_004B4A60_gen(ctx, base);
    uint32_t eax = ctx->r_eax;
    MEM32(m + 0x40) = (uint32_t)(a + ((int32_t)MEM32(m + 0x40) - sa));   /* plus what it spawned */
    MEM32(m + 0x28) = (uint32_t)(b + ((int32_t)MEM32(m + 0x28) - sb));
    ctx->r_eax = eax;
}


/* ==== MIXTAPE in the car radio (gta2_mixtape.c, docs/re/radio.md section 7) ==== */
extern void sub_004B2CD0_gen(RECOMP_PARAMS);
void sub_004B2CD0(RECOMP_PARAMS)   /* radio key (thiscall mgr, dir): posts the change request */
{
    sub_004B2CD0_gen(ctx, base);
    gta2_radio_after_key();
}

extern void sub_004B2420_gen(RECOMP_PARAMS);
void sub_004B2420(RECOMP_PARAMS)   /* the station change: MIXTAPE after the last station */
{
    gta2_radio_cycle(ctx, sub_004B2420_gen, base);
}

extern void sub_004B1E40_gen(RECOMP_PARAMS);
void sub_004B1E40(RECOMP_PARAMS)   /* radio volume / static per tick: the game station muted under MIXTAPE */
{
    gta2_radio_before_volume();
    sub_004B1E40_gen(ctx, base);
    if (gta2_mixtape_audible()) {
        uint32_t r[6] = { ctx->r_eax, ctx->r_ecx, ctx->r_edx, ctx->r_ebx, ctx->r_esi, ctx->r_edi };
        uint32_t a[2] = { 0, 0 };   /* slot 0, volume 0 */
        ctx->r_ecx = 0x0066C950u;
        wr_call_guest(ctx, 0x004B6890u, 2, a);
        ctx->r_eax = r[0]; ctx->r_ecx = r[1]; ctx->r_edx = r[2];
        ctx->r_ebx = r[3]; ctx->r_esi = r[4]; ctx->r_edi = r[5];
    }
}

extern void sub_004B2D50_gen(RECOMP_PARAMS);
void sub_004B2D50(RECOMP_PARAMS)   /* the radio's tick */
{
    sub_004B2D50_gen(ctx, base);
    gta2_radio_after_tick();
}

extern void sub_004B1D80_gen(RECOMP_PARAMS);
void sub_004B1D80(RECOMP_PARAMS)   /* the radio stops (level end, frontend music) */
{
    sub_004B1D80_gen(ctx, base);
    gta2_radio_stopped();
}


/* ==== text in languages the game fonts cannot draw (gta2_text.c, docs/re/text.md) ==== */
extern void sub_004CC100_gen(RECOMP_PARAMS);
void sub_004CC100(RECOMP_PARAMS)   /* the text drawer (stdcall, 9 args): our glyphs between the game's */
{
    gta2_text_draw(ctx);   /* draws a string with our characters itself and leaves the drawer an empty one */
    sub_004CC100_gen(ctx, base);
}

extern void sub_004BF570_gen(RECOMP_PARAMS);
void sub_004BF570(RECOMP_PARAMS)   /* a glyph's width (thiscall style, (u16 *font, u16 *ch), ret 8) */
{
    uint16_t w;
    if (gta2_text_width(ctx, &w)) {
        uint32_t ch = MEM32(ctx->r_esp + 8), keep = MEM16(ch);
        MEM16(ch) = 'A';                  /* a safe character for the original's table read ... */
        sub_004BF570_gen(ctx, base);      /* ... which does the ret 8 */
        MEM16(ch) = (uint16_t)keep;
        ctx->r_eax = (ctx->r_eax & 0xFFFF0000u) | w;
        return;
    }
    sub_004BF570_gen(ctx, base);
}

extern void sub_004C21A0_gen(RECOMP_PARAMS);
void sub_004C21A0(RECOMP_PARAMS)   /* upper case of one character (thiscall gxt, (u16 c), ret 4) */
{
    uint16_t u;
    if (gta2_text_upper((uint16_t)MEM32(ctx->r_esp + 4), &u)) {
        ctx->r_eax = (ctx->r_eax & 0xFFFF0000u) | u;
        ctx->r_esp += 8;   /* ret 4 */
        return;
    }
    sub_004C21A0_gen(ctx, base);
}

extern void sub_004C2620_gen(RECOMP_PARAMS);
void sub_004C2620(RECOMP_PARAMS)   /* the GXT object's ctor (thiscall): it accepts only e f g i s j */
{
    uint32_t self = ctx->r_ecx;
    sub_004C2620_gen(ctx, base);
    gta2_text_after_gxt_ctor(self);
}
