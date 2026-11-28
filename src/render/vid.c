/*
 * GTA 2 Recompiled - native Dmavideo.dll (Vid_*), see docs/re/vid_api.md.
 *
 * The game keeps the VidState pointer and reads a few fields itself
 * (+0x40 mode, +0x48/+0x4C size, +0x50/+0x54 CPU surface, +0x58..+0x6C pixel
 * format, +0x134 for Bink). The state lives in guest memory with those
 * fields kept exactly as the original sets them; DirectDraw objects are not
 * needed (the renderer is native too), so their slots stay 0.
 *
 * Unlike the original, Vid_SetMode/Vid_CloseScreen do the renderer side
 * themselves: the original d3ddll patched gta2's slots to get that; here
 * there is nothing to patch.
 */
#include "gta2_render.h"
#include "../gta2_options.h"
#include "../gta2_shaders.h"
#include "platform/wr_platform.h"
#include "post/shader_pass.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int w32_window_client(uint32_t hwnd, int *w, int *h);

gta2_gfx g_gfx;

/* modes the game asked for with Vid_CheckMode; id = index + 1 */
static struct { uint32_t w, h; } g_modes[32];
static int g_nmodes;
static uint32_t g_device;       /* the one VidDevice record */

/* ---- frame helpers ------------------------------------------------------------------------ */

static void ensure_vk(void)
{
    if (g_gfx.vk || wr_platform_headless())
        return;
    SDL_Window *win = wr_main_window();
    if (!win)
        return;
    /* settings.ini (GTA2_<KEY> overrides; the old WR_* names still work) */
    const char *vs = getenv("WR_VSYNC");
    int vsync = vs && *vs ? *vs != '0' : opt_get(OPT_VSYNC);
    if (!wr_vk_init(win, vsync))
        return;
    wr_overlay_set_tick_rate(1000.0 / 33.0);   /* one logic tick per 33 ms (docs/re/framerate.md) */
    const char *preset = getenv("WR_SHADER_PRESET");   /* dev override, without saved parameters */
    if (preset && *preset)
        sp_set_preset(preset);
    else
        gta2_shaders_apply();   /* SHADERS page: the preset AND its saved parameters */
    g_gfx.nearest = wr_vk_sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    g_gfx.linear = wr_vk_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    g_gfx.vk = 1;
}

/* Render target = the game's screen times an integer scale that fills the
 * window (RENDER RESOLUTION "native"; WR_RENDER_SCALE overrides). */
static void ensure_target(void)
{
    if (!g_gfx.vk || !g_gfx.w || !g_gfx.h)
        return;
    int ww, wh;
    wr_main_window_size(&ww, &wh);
    uint32_t scale = 1;
    const char *e = getenv("WR_RENDER_SCALE");   /* dev override */
    int rh = opt_render_height();
    int shres = g_gfx.shader_on ? gta2_shader_resolution() : 2;
    if (e && *e) {
        scale = (uint32_t)atoi(e);
    } else if (shres < 2) {   /* SHADER RESOLUTION: ORIGINAL = the game's own size (as RetroArch), 2X */
        scale = (uint32_t)shres + 1;
    } else if (rh) {   /* RENDER RESOLUTION: the smallest scale reaching that height */
        scale = ((uint32_t)rh + g_gfx.h - 1) / g_gfx.h;
    } else {
        /* the smallest integer scale whose image covers the fitted rectangle:
         * the present pass then only ever scales down (sharp) */
        double fit = (double)ww / g_gfx.w < (double)wh / g_gfx.h ? (double)ww / g_gfx.w : (double)wh / g_gfx.h;
        scale = (uint32_t)fit;
        if ((double)scale < fit - 0.01)
            scale++;
    }
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    uint32_t tw = g_gfx.w * scale, th = g_gfx.h * scale;
    if (g_gfx.target && g_gfx.tw == tw && g_gfx.th == th)
        return;
    if (g_gfx.target)
        wr_vk_target_destroy(g_gfx.target);
    g_gfx.target = wr_vk_target_create(tw, th, g_gfx.linear);
    g_gfx.tw = tw;
    g_gfx.th = th;
}

VkCommandBuffer gfx_begin_drawing(void)
{
    ensure_vk();
    if (!g_gfx.vk)
        return NULL;
    if (!g_gfx.cmd) {
        ensure_target();
        if (!g_gfx.target)
            return NULL;
        g_gfx.cmd = wr_vk_frame_begin();
        if (!g_gfx.cmd)
            return NULL;
    }
    if (!g_gfx.in_target) {
        wr_vk_target_begin(g_gfx.target, g_gfx.clear_pending, g_gfx.clear_rgba);
        g_gfx.clear_pending = 0;
        g_gfx.in_target = 1;
    }
    return g_gfx.cmd;
}

static float g_centre_px[2];

/* the game picture's centre in window points (the mouse's units) */
int gta2_game_centre(float *x, float *y)
{
    SDL_Window *win = wr_main_window();
    int pw = 0, ph = 0, ww = 0, wh = 0;
    if (!win || !g_centre_px[0] || !SDL_GetWindowSizeInPixels(win, &pw, &ph) || !SDL_GetWindowSize(win, &ww, &wh) || !pw)
        return 0;
    *x = g_centre_px[0] * (float)ww / (float)pw;
    *y = g_centre_px[1] * (float)wh / (float)ph;
    return 1;
}

void gfx_flip(void)
{
    g_gfx.frames++;
    /* WR_FPS_LOG=1: presented frames per second, every 5 s */
    static int fps_log = -1;
    static uint64_t fps_t0, fps_f0;
    if (fps_log < 0) {
        const char *e = getenv("WR_FPS_LOG");
        fps_log = e && *e == '1';
    }
    if (fps_log) {
        uint64_t t = wr_time_ns();
        if (!fps_t0) {
            fps_t0 = t;
            fps_f0 = g_gfx.frames;
        } else if (t - fps_t0 >= 5000000000ull) {
            wr_log("fps: %.2f", (double)(g_gfx.frames - fps_f0) * 1e9 / (double)(t - fps_t0));
            fps_t0 = t;
            fps_f0 = g_gfx.frames;
        }
    }
    if (!g_gfx.vk)
        return;
    if (!g_gfx.cmd && !gfx_begin_drawing())
        return;
    gbh_flush();
    if (g_gfx.in_target) {
        wr_vk_target_end();
        g_gfx.in_target = 0;
    }
    /* WR_DUMP_FRAMES=N: every N-th frame's game picture to data/frame_<n>.png - when the count CROSSES a
     * multiple of N: it also counts movie frames and interpolated flips, which never pass through here, so
     * "frames % N == 0" could miss every multiple for a whole run (no dumps at all) */
    static int dump_every = -1;
    static uint64_t dump_next;
    if (dump_every < 0) {
        const char *d = getenv("WR_DUMP_FRAMES");
        dump_every = d && *d ? atoi(d) : 0;
        dump_next = dump_every > 0 ? (uint64_t)dump_every : 0;
    }
    if (dump_every > 0 && g_gfx.frames >= dump_next) {
        dump_next = (g_gfx.frames / (uint64_t)dump_every + 1) * (uint64_t)dump_every;
        char path[1300];
        snprintf(path, sizeof path, "%s/frame_%06llu.png", g_wr_paths.data, (unsigned long long)g_gfx.frames);
        wr_vk_dump_target(g_gfx.target, path);
    }
    /* F12: a capture as option-p lays it out - data/captures/<time>-frame<N>/
     * screen.png (the game picture at the render resolution, no overlays),
     * shown.png (the window's picture: bars, shader, overlays) and info.txt */
    static char capture[1200];
    capture[0] = 0;
    if (g_wr_screenshot_request) {
        g_wr_screenshot_request = 0;
        if (wr_capture_dir(g_gfx.frames, capture, sizeof capture)) {
            wr_log("F12 capture: %s", capture);   /* a time mark in the log */
            char path[1300];
            snprintf(path, sizeof path, "%s/screen.png", capture);
            wr_vk_dump_target(g_gfx.target, path);
            snprintf(path, sizeof path, "%s/info.txt", capture);
            FILE *f = fopen(path, "w");
            if (f) {
                fprintf(f, "frame %llu\ngame mode %ux%u\nrender target %ux%u\nwindow %ux%u\n",
                        (unsigned long long)g_gfx.frames, g_gfx.w, g_gfx.h, g_gfx.tw, g_gfx.th,
                        g_vk.extent.width, g_vk.extent.height);
                fprintf(f, "aspect %.4f, display mode %d, texture filter %d, shader %s\n", opt_aspect_ratio(),
                        opt_display_mode(), opt_texture_filter(), g_gfx.shader_on ? "on" : "off");
                fclose(f);
            }
        } else {
            capture[0] = 0;
        }
    }
    /* SHADERS > TAKE TEST PICTURE: the paused game's picture (HUD, no menu - it is hidden for these frames) as
     * the SHADERS pages' test picture, data/shader_test.png (the file is written at this frame's end) */
    if (g_gta2_test_grab == -1) {
        gta2_shader_test_reload();
        g_gta2_test_grab = 0;
    } else if (g_gta2_test_grab == 1) {
        char path[1300];
        snprintf(path, sizeof path, "%s/shader_test.png", g_wr_paths.data);
        wr_vk_dump_target(g_gfx.target, path);
        wr_log("shaders: test picture taken from the game");
        g_gta2_test_grab = -1;
    } else if (g_gta2_test_grab > 1) {
        g_gta2_test_grab--;
    }
    int x, y, w, h;
    wr_vk_fit_rect(g_vk.extent.width, g_vk.extent.height, g_gfx.w, g_gfx.h, &x, &y, &w, &h);
    g_centre_px[0] = (float)x + (float)w * 0.5f;
    g_centre_px[1] = (float)y + (float)h * 0.5f;
    float black[4] = { 0, 0, 0, 1 };
    /* Slang shader chain (option-c's shader pass): game picture -> chain ->
     * swapchain image (the bars stay black), then a kept pass for overlays.
     * Without a preset: the plain scaled blit with the game's gamma. */
    static uint64_t last_ns;
    static int lx, ly, lw, lh;
    uint64_t now = wr_time_ns();
    uint32_t dt_ms = last_ns ? (uint32_t)((now - last_ns) / 1000000u) : 0;
    last_ns = now;
    int shaded = 0;
    if (g_gfx.shader_on) {
        VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b.image = wr_vk_swap_image();
        vkCmdPipelineBarrier(g_gfx.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, NULL, 0, NULL, 1, &b);
        int same = x == lx && y == ly && w == lw && h == lh;
        shaded = sp_frame(g_gfx.cmd, g_gfx.frames, g_gfx.target->color, VK_FORMAT_R8G8B8A8_UNORM, g_gfx.tw, g_gfx.th,
                          wr_vk_swap_image(), g_vk.format, g_vk.extent.width, g_vk.extent.height, x, y, w, h, same,
                          dt_ms > 0 && dt_ms < 1000 ? 1000.0f / (float)dt_ms : 30.0f, dt_ms);
        lx = x; ly = y; lw = w; lh = h;
    }
    /* the PERFORMANCE panel right of the HUD bars top left (they end at ~22 % of the game
     * picture's height; maintainer 2026-11-26) */
    if (wr_rd8(0x00595018u))   /* the menus (frontend up): the top left corner */
        wr_overlay_set_pos(-1, -1);
    else
        wr_overlay_set_pos(x + (int)(0.23f * (float)h), y + h / 96);
    wr_overlay_prepare();
    if (shaded) {
        wr_vk_present_begin(0, black);   /* keep the chain's picture; overlays go here */
    } else {
        wr_vk_present_begin(1, black);
        wr_vk_blit(g_gfx.cmd, g_gfx.target, x, y, w, h, g_gfx.gamma);
    }
    wr_overlay_draw(g_gfx.cmd);
    wr_vk_present_end();
    /* WR_DUMP_PRESENT=N: every N-th shown picture to data/present_<n>.png */
    static int dump_present = -1;
    if (dump_present < 0) {
        const char *d = getenv("WR_DUMP_PRESENT");
        dump_present = d && *d ? atoi(d) : 0;
    }
    if (capture[0]) {
        char path[1300];
        snprintf(path, sizeof path, "%s/shown.png", capture);
        wr_vk_dump_swap(path);
        wr_log("capture: %s", capture);
    }
    if (dump_present > 0 && g_gfx.frames % (uint64_t)dump_present == 0) {
        char path[1300];
        snprintf(path, sizeof path, "%s/present_%06llu.png", g_wr_paths.data, (unsigned long long)g_gfx.frames);
        wr_vk_dump_swap(path);
    }
    wr_vk_frame_end();
    g_gfx.cmd = NULL;
    /* the chain for the next frame (built on a worker; in place at a flip) */
    if (g_gfx.vk) {
        SpDevice d = { g_vk.instance, g_vk.phys, g_vk.dev, g_vk.queue, g_vk.qfamily, WR_VK_FRAMES };
        g_gfx.shader_on = sp_tick(&d);
    }
}

/* The host window from DISPLAY MODE and ASPECT RATIO (gta2_options.c): set
 * up when the game first sets a mode and again whenever one of the two
 * changes. Windowed = 85 % of the desktop at that ratio; BORDERLESS = the
 * desktop; FULLSCREEN = exclusive at the display's native mode. Every game
 * mode is fitted into it (pillar/letterbox); it never jumps with the game's
 * own 640x480 menu / gameplay mode switches. */
static int g_window_shown;

void gfx_apply_window(void)
{
    SDL_Window *win = wr_main_window();
    if (!win)
        return;
    int mode = opt_display_mode();
#if defined(__ANDROID__)
    mode = 1;
#endif
    if (mode == 2) {
        const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(win));
        SDL_SetWindowFullscreenMode(win, dm);
        SDL_SetWindowFullscreen(win, true);
    } else if (mode == 1) {
        SDL_SetWindowFullscreenMode(win, NULL);
        SDL_SetWindowFullscreen(win, true);
    } else {
        int dw, dh;
        wr_display_size(&dw, &dh);
        double ar = opt_aspect_ratio();
        int h = (int)(dh * 0.85), w = (int)(h * ar + 0.5);
        if (w > (int)(dw * 0.85)) {
            w = (int)(dw * 0.85);
            h = (int)(w / ar + 0.5);
        }
        SDL_SetWindowFullscreen(win, false);
        SDL_SetWindowSize(win, w, h);
        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    if (!g_window_shown) {
        SDL_ShowWindow(win);
        g_window_shown = 1;
    }
    SDL_SyncWindow(win);
    if (g_gfx.vk)
        g_vk.need_recreate = 1;
}

void gfx_apply_vsync(int on)
{
    if (g_gfx.vk)
        wr_vk_set_vsync(on);
}

static void setup_window(void)
{
    if (!g_window_shown)
        gfx_apply_window();
}

void gfx_set_mode(uint32_t w, uint32_t h)
{
    g_gfx.w = w;
    g_gfx.h = h;
    setup_window();
    gbh_flush();
    if (g_gfx.cmd) {
        if (g_gfx.in_target) {
            wr_vk_target_end();
            g_gfx.in_target = 0;
        }
        wr_vk_frame_end();
        g_gfx.cmd = NULL;
    }
    ensure_vk();
    ensure_target();
    g_gfx.clear_pending = 1;
    memset(g_gfx.clear_rgba, 0, sizeof g_gfx.clear_rgba);
    g_gfx.clear_rgba[3] = 1.0f;
}

void gfx_shutdown(void)
{
    gbh_flush();
    if (g_gfx.cmd) {
        if (g_gfx.in_target)
            wr_vk_target_end();
        wr_vk_frame_end();
    }
    if (g_gfx.target)
        wr_vk_target_destroy(g_gfx.target);
    if (g_gfx.vk)
        wr_vk_shutdown();
    memset(&g_gfx, 0, sizeof g_gfx);
}

/* ---- Vid_ exports --------------------------------------------------------------------------- */

static uint32_t g_version;

WR_HANDLER(Vid_GetVersion)
{
    (void)base;
    if (!g_version) {
        g_version = wr_galloc(64);
        float v = 1.81f;
        memcpy(wr_ptr(g_version), &v, 4);
        wr_put_str(g_version + 4, 60, "winrecomp native video (Vulkan)");
    }
    WR_RET(g_version, 0);
}

static uint32_t g_slot_table, g_hmodule;

WR_HANDLER(Vid_InitDLL)
{
    (void)base;
    g_hmodule = WR_ARG(0);
    g_slot_table = WR_ARG(1);
    WR_RET(0, 2);
}

WR_HANDLER(Vid_Init_SYS)
{
    (void)base;
    uint32_t st = wr_galloc(VS_SIZE);
    wr_wr32(st + 0x000, 1);
    wr_wr32(st + VS_FLAGS, (WR_ARG(1) & 0x40u) | 0x220u);
    wr_wr32(st + 0x014, 2);            /* next mode id */
    wr_wr32(st + 0x018, 2);            /* next device id */
    wr_wr32(st + 0x020, 1);            /* one device */
    wr_wr32(st + VS_APPINST, WR_ARG(0));
    wr_wr32(st + VS_HMODULE, g_hmodule);
    wr_wr32(st + VS_SLOTTBL, g_slot_table);
    if (!g_device) {
        /* VidDevice: id 1 (primary), flags bit0 = can render windowed */
        g_device = wr_galloc(0x60);
        wr_wr32(g_device + 0x00, 1);
        wr_wr32(g_device + 0x04, 1);
        wr_put_str(g_device + 0x2C, 16, "display");
        wr_put_str(g_device + 0x3C, 32, "Vulkan");
        wr_wr32(g_device + 0x08, g_device + 0x2C);
        wr_wr32(g_device + 0x0C, g_device + 0x3C);
    }
    wr_wr32(st + 0x02C, g_device);
    wr_wr32(st + 0x030, g_device);
    g_gfx.state = st;
    g_gfx.gamma = 1.0f;
    wr_log("Vid_Init_SYS -> state 0x%08X", st);
    WR_RET(st, 2);
}

WR_HANDLER(Vid_FindDevice)
{
    (void)base;
    WR_RET(WR_ARG(1) == 1 ? g_device : 0, 2);
}

WR_HANDLER(Vid_SetDevice)
{
    (void)base;
    uint32_t st = WR_ARG(0), id = WR_ARG(1);
    if (st)
        wr_wr32(st + VS_CURDEV, id == 1 ? 1u : 0u);
    WR_RET(id == 0 || id == 1 ? 0 : 1, 2);
}

static uint32_t mode_id(uint32_t w, uint32_t h)
{
    for (int i = 0; i < g_nmodes; i++)
        if (g_modes[i].w == w && g_modes[i].h == h)
            return (uint32_t)i + 1;
    if (g_nmodes == 32)
        return 0;
    g_modes[g_nmodes].w = w;
    g_modes[g_nmodes].h = h;
    return (uint32_t)++g_nmodes;
}

WR_HANDLER(Vid_CheckMode)
{
    (void)base;
    uint32_t st = WR_ARG(0), w = WR_ARG(1), h = WR_ARG(2), bpp = WR_ARG(3);
    uint32_t id = 0;
    if (bpp == 16 && w >= 320 && h >= 200 && w <= 7680 && h <= 4320)
        id = mode_id(w, h);
    if (st) {
        wr_wr32(st + VS_RES_W, id ? w : 0);
        wr_wr32(st + VS_RES_H, id ? h : 0);
        wr_wr32(st + VS_RES_BPP, id ? bpp : 0);
    }
    wr_log("Vid_CheckMode(%ux%u, %u bpp) -> %u", w, h, bpp, id);
    WR_RET(id, 4);
}

WR_HANDLER(Vid_FindMode)
{
    (void)base;
    /* not used by gta2; mode records are not kept */
    WR_RET(0, 2);
}

WR_HANDLER(Vid_FindFirstMode) { (void)base; WR_RET(0, 2); }
WR_HANDLER(Vid_FindNextMode) { (void)base; WR_RET(0, 1); }

static void close_screen(uint32_t st)
{
    if (!st)
        return;
    wr_wr32(st + VS_MODE, 0);
}

WR_HANDLER(Vid_CloseScreen)
{
    (void)base;
    close_screen(WR_ARG(0));
    WR_RET(0, 1);
}

WR_HANDLER(Vid_SetMode)
{
    (void)base;
    uint32_t st = WR_ARG(0), hwnd = WR_ARG(1), id = WR_ARG(2);
    if (!st)
        WR_RET(1, 3);
    uint32_t w, h;
    if (id == 0xFFFFFFFEu) {
        int cw = 640, ch = 480;
        w32_window_client(hwnd, &cw, &ch);
        w = (uint32_t)cw;
        h = (uint32_t)ch;
    } else if (id >= 1 && (int)id <= g_nmodes) {
        w = g_modes[id - 1].w;
        h = g_modes[id - 1].h;
    } else {
        wr_log("Vid_SetMode: unknown mode id %d", (int)id);
        WR_RET(1, 3);
    }
    wr_wr32(st + VS_HWND, hwnd);
    wr_wr32(st + VS_FULLSCR, id != 0xFFFFFFFEu);
    wr_wr32(st + VS_CURDEV, 1);
    wr_wr32(st + VS_BPP, 16);
    wr_wr32(st + VS_MODE, id);
    wr_wr32(st + VS_WIDTH, w);
    wr_wr32(st + VS_HEIGHT, h);
    /* 5-6-5 */
    wr_wr32(st + VS_RSHIFT, 11); wr_wr32(st + VS_RBITS, 5);
    wr_wr32(st + VS_GSHIFT, 5);  wr_wr32(st + VS_GBITS, 6);
    wr_wr32(st + VS_BSHIFT, 0);  wr_wr32(st + VS_BBITS, 5);
    wr_wr32(st + 0x070, 0);      wr_wr32(st + 0x074, 0);
    uint32_t flags = wr_rd32(st + VS_FLAGS) & ~(VSF_LOCKED | VSF_WRITES);
    wr_wr32(st + VS_FLAGS, flags | 0x40000000u);
    wr_wr32(st + VS_SURFACE, 0);
    if (g_gfx.cpu_buf)
        wr_heap_free(g_gfx.cpu_buf);
    g_gfx.cpu_buf = wr_galloc(w * h * 2u);
    wr_log("Vid_SetMode(%s %ux%u)", id == 0xFFFFFFFEu ? "windowed" : "fullscreen", w, h);
    gfx_set_mode(w, h);
    WR_RET(0, 3);
}

WR_HANDLER(Vid_FlipBuffers)
{
    (void)base;
    gfx_flip();
    WR_RET(0, 1);
}

static int lock_writes(uint32_t st)
{
    uint32_t f = wr_rd32(st + VS_FLAGS);
    if (f & VSF_WRITES)
        return 1;
    wr_wr32(st + VS_FLAGS, f | VSF_LOCKED | VSF_WRITES);
    wr_wr32(st + VS_SURFACE, g_gfx.cpu_buf);
    wr_wr32(st + VS_PITCH, wr_rd32(st + VS_WIDTH) * 2u);
    return 0;
}

static int unlock_writes(uint32_t st)
{
    uint32_t f = wr_rd32(st + VS_FLAGS);
    int was = (f & VSF_WRITES) != 0;
    wr_wr32(st + VS_FLAGS, f & ~(VSF_LOCKED | VSF_WRITES));
    wr_wr32(st + VS_SURFACE, 0);
    return was ? 0 : 1;
}

WR_HANDLER(Vid_GrabSurface)
{
    (void)base;
    uint32_t st = WR_ARG(0);
    if (st)
        wr_wr32(st + VS_FLAGS, wr_rd32(st + VS_FLAGS) | VSF_LOCKED);
    WR_RET(0, 1);
}

WR_HANDLER(Vid_ReleaseSurface)
{
    (void)base;
    uint32_t st = WR_ARG(0);
    if (st)
        wr_wr32(st + VS_FLAGS, wr_rd32(st + VS_FLAGS) & ~VSF_LOCKED);
    WR_RET(0, 1);
}

WR_HANDLER(Vid_EnableWrites)
{
    (void)base;
    uint32_t st = WR_ARG(0);
    uint32_t f = st ? wr_rd32(st + VS_FLAGS) : 0;
    if (!st || !(f & VSF_LOCKED) || (f & VSF_WRITES))
        WR_RET(1, 1);
    WR_RET(lock_writes(st), 1);
}

WR_HANDLER(Vid_DisableWrites)
{
    (void)base;
    uint32_t st = WR_ARG(0);
    uint32_t f = st ? wr_rd32(st + VS_FLAGS) : 0;
    if (!st || !(f & VSF_WRITES))
        WR_RET(1, 1);
    wr_wr32(st + VS_FLAGS, f & ~VSF_WRITES);
    wr_wr32(st + VS_SURFACE, 0);
    WR_RET(0, 1);
}

WR_HANDLER(Vid_GetSurface)
{
    (void)base;
    uint32_t st = WR_ARG(0);
    if (!st || !g_gfx.cpu_buf)
        WR_RET(1, 1);
    WR_RET(lock_writes(st), 1);
}

WR_HANDLER(Vid_FreeSurface)
{
    (void)base;
    uint32_t st = WR_ARG(0);
    WR_RET(st ? (uint32_t)unlock_writes(st) : 1u, 1);
}

WR_HANDLER(Vid_ClearScreen)
{
    (void)base;
    uint32_t r = WR_ARG(1) & 0xFF, g = WR_ARG(2) & 0xFF, b = WR_ARG(3) & 0xFF;
    g_gfx.clear_rgba[0] = (float)r / 255.0f;
    g_gfx.clear_rgba[1] = (float)g / 255.0f;
    g_gfx.clear_rgba[2] = (float)b / 255.0f;
    g_gfx.clear_rgba[3] = 1.0f;
    /* gta2 always clears the whole screen right after a flip: the next
     * frame's target pass starts with this clear. Inside a pass: clear now. */
    if (g_gfx.in_target && g_gfx.cmd) {
        gbh_flush();
        VkClearAttachment ca = { VK_IMAGE_ASPECT_COLOR_BIT, 0, { { { 0 } } } };
        memcpy(ca.clearValue.color.float32, g_gfx.clear_rgba, 16);
        VkClearRect cr = { { { 0, 0 }, { g_gfx.tw, g_gfx.th } }, 0, 1 };
        vkCmdClearAttachments(g_gfx.cmd, 1, &ca, 1, &cr);
    } else {
        g_gfx.clear_pending = 1;
    }
    if (g_gfx.cpu_buf && g_gfx.w)
        memset(wr_ptr(g_gfx.cpu_buf), 0, g_gfx.w * g_gfx.h * 2u);
    WR_RET(0, 8);
}

WR_HANDLER(Vid_SetGamma)
{
    (void)base;
    float g;
    uint32_t bits = WR_ARG(1);
    memcpy(&g, &bits, 4);
    if (g > 0.05f && g < 10.0f)
        g_gfx.gamma = g;
    WR_RET(0, 4);
}

WR_HANDLER(Vid_ShutDown_SYS)
{
    (void)base;
    close_screen(WR_ARG(0));
    gfx_shutdown();
    WR_RET(0, 1);
}

WR_HANDLER(Vid_WindowProc) { (void)base; WR_RET(0, 5); }

#define E(n) { #n, n }
static const wr_export k_vid_exports[] = {
    E(Vid_CheckMode), E(Vid_ClearScreen), E(Vid_CloseScreen), E(Vid_DisableWrites), E(Vid_EnableWrites),
    E(Vid_FindDevice), E(Vid_FindFirstMode), E(Vid_FindMode), E(Vid_FindNextMode), E(Vid_FlipBuffers),
    E(Vid_FreeSurface), E(Vid_GetSurface), E(Vid_GetVersion), E(Vid_GrabSurface), E(Vid_InitDLL),
    E(Vid_Init_SYS), E(Vid_ReleaseSurface), E(Vid_SetDevice), E(Vid_SetGamma), E(Vid_SetMode),
    E(Vid_ShutDown_SYS), E(Vid_WindowProc),
    { NULL, NULL }
};
#undef E

static wr_module k_vid_module = { "dmavideo.dll", k_vid_exports, 0 };

void gta2_register_video(void) { wr_module_register(&k_vid_module); }

uint64_t gta2_frames_presented(void) { return g_gfx.frames; }

/* A full frame from the CPU (Bink movies): BGRX pixels shown 4:3 in the window. */
void gfx_show_frame_bgrx(const uint8_t *px, uint32_t w, uint32_t h, uint32_t pitch)
{
    g_gfx.frames++;
    ensure_vk();
    if (!g_gfx.vk)
        return;
    static wr_vk_texture *tex;
    static uint32_t tw, th;
    static uint32_t *rgba;
    static size_t cap;
    if ((size_t)w * h > cap) {
        free(rgba);
        cap = (size_t)w * h;
        rgba = (uint32_t *)malloc(cap * 4);
    }
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *s = px + (size_t)y * pitch;
        uint32_t *d = rgba + (size_t)y * w;
        for (uint32_t x = 0; x < w; x++)
            d[x] = 0xFF000000u | (uint32_t)s[4 * x] << 16 | (uint32_t)s[4 * x + 1] << 8 | s[4 * x + 2];
    }
    if (g_gfx.cmd) {   /* a game frame in progress: finish it first */
        gbh_flush();
        if (g_gfx.in_target) { wr_vk_target_end(); g_gfx.in_target = 0; }
        wr_vk_frame_end();
        g_gfx.cmd = NULL;
    }
    if (!tex || tw != w || th != h) {
        if (tex)
            wr_vk_texture_destroy(tex);
        tex = wr_vk_texture_create(w, h, rgba, g_gfx.linear);
        tw = w;
        th = h;
    } else {
        wr_vk_texture_update(tex, 0, 0, w, h, rgba, w);
    }
    VkCommandBuffer cmd = wr_vk_frame_begin();
    if (!cmd)
        return;
    int x, y, ww, hh;
    wr_vk_fit_rect(g_vk.extent.width, g_vk.extent.height, w, h, &x, &y, &ww, &hh);
    float black[4] = { 0, 0, 0, 1 };
    wr_overlay_prepare();
    wr_vk_present_begin(1, black);
    wr_vk_blit_texture(cmd, tex, x, y, ww, hh, 1.0f);
    wr_overlay_draw(cmd);
    wr_vk_present_end();
    const char *d = getenv("WR_DUMP_PRESENT");
    if (d && *d && atoi(d) > 0 && g_gfx.frames % (uint64_t)atoi(d) == 0) {
        char path[1300];
        snprintf(path, sizeof path, "%s/movie_%06llu.png", g_wr_paths.data, (unsigned long long)g_gfx.frames);
        wr_vk_dump_swap(path);
    }
    wr_vk_frame_end();
}
