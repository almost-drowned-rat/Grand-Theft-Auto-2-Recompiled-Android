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
        ctx->r_ecx = ecx;
        ctx->r_eax = eax;
    }
    uint32_t next0 = MEM32(0x00662748u);
    uint64_t f0 = gta2_frames_presented();
    sub_00462A30_gen(ctx, base);
    uint32_t next = MEM32(0x00662748u);
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
    if (left > 2) {
        uint64_t until = wr_time_ns() + (uint64_t)(left - 1) * 1000000u;
        while (wr_time_ns() < until) {
            uint64_t rest = until - wr_time_ns();
            wr_sleep_ns(rest > 900000 ? rest - 200000 : rest);
        }
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
        uint64_t until = wr_time_ns() + (uint64_t)(left - 1) * 1000000u;
        while (wr_time_ns() < until) {
            uint64_t rest = until - wr_time_ns();
            wr_sleep_ns(rest > 900000 ? rest - 200000 : rest);
        }
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

/* ---- the pause menu over the HUD ---------------------------------------------------------- */
extern void sub_004CA440_gen(RECOMP_PARAMS);
void sub_004CA440(RECOMP_PARAMS)   /* draws the HUD each frame (thiscall HUD) */
{
    sub_004CA440_gen(ctx, base);
    uint32_t eax = ctx->r_eax;
    gta2_radio_label_draw(ctx);   /* MIXTAPE: station + song for a moment */
    gta2_pause_draw(ctx);
    ctx->r_eax = eax;
}

extern void sub_004C9FA0_gen(RECOMP_PARAMS);
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
void sub_004454E0(RECOMP_PARAMS)   /* one ped's tick (thiscall ped) */
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
void sub_004BE060(RECOMP_PARAMS)   /* draws one sprite through gbh_DrawQuad (thiscall sprite) */
{
    g_gta2_sprite_draws++;
    float sc, ti, al;
    uint32_t spr = ctx->r_ecx;
    int moved = gta2_fps_sprite_before(spr);   /* FRAME RATE: blended position */
    gta2_sprite_look(spr, &sc, &ti, &al);
    if (sc == 1.0f && ti == 1.0f && al == 1.0f) {
        sub_004BE060_gen(ctx, base);
        if (moved)
            gta2_fps_sprite_after(spr);
        return;
    }
    g_port_quad_scale = sc;
    g_port_quad_tint = ti;
    g_port_quad_alpha = al;
    sub_004BE060_gen(ctx, base);   /* its quads are changed as they are emitted */
    g_port_quad_scale = g_port_quad_tint = g_port_quad_alpha = 1.0f;
    if (moved)
        gta2_fps_sprite_after(spr);
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
