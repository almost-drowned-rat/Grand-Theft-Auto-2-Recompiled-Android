/*
 * GTA 2 Recompiled - MINIMAP (GAMEPLAY > MINIMAP; maintainer 2026-11-27), see gta2_radar.h.
 *
 * The level's files: every path the game resolves goes past g_wr_read_map;
 * the last .gmp and level .sty (not the frontend's fstyle.sty) name the
 * district. Its picture (gta2_radar_map.c) is made on the first frame drawn
 * after a change and kept as one texture.
 *
 * Layout in fractions of the game screen (any aspect ratio, windowed too):
 * a square in the bottom left corner, north up (GTA 2's camera never turns),
 * the player in the middle; outside the map is black.
 */
#include "gta2_radar.h"
#include "gta2_radar_map.h"
#include "gta2_options.h"
#include "gta2_state.h"
#include "render/gta2_render.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PX 4   /* picture pixels per block: 1024 x 1024 */
#define MAP_N 256

static const char *(*g_prev_read_map)(const char *rel);
static char g_gmp[256], g_sty[256];      /* guest-relative, as the game named them */
static char g_built_gmp[256], g_built_sty[256];
static wr_vk_texture *g_tex;

static int ends_with_ci(const char *s, const char *end)
{
    size_t n = strlen(s), m = strlen(end);
    if (n < m)
        return 0;
    for (size_t i = 0; i < m; i++)
        if (tolower((unsigned char)s[n - m + i]) != tolower((unsigned char)end[i]))
            return 0;
    return 1;
}

static const char *watch_read(const char *rel)
{
    if (ends_with_ci(rel, ".gmp"))
        snprintf(g_gmp, sizeof g_gmp, "%s", rel);
    else if (ends_with_ci(rel, ".sty") && !ends_with_ci(rel, "fstyle.sty"))
        snprintf(g_sty, sizeof g_sty, "%s", rel);
    return g_prev_read_map ? g_prev_read_map(rel) : NULL;
}

void gta2_radar_init(void)
{
    if (g_wr_read_map == watch_read)
        return;
    g_prev_read_map = g_wr_read_map;
    g_wr_read_map = watch_read;
}

/* the picture of the level now loaded: made again when the files changed */
static int picture_ready(void)
{
    if (!g_gmp[0] || !g_sty[0])
        return 0;
    if (g_tex && !strcmp(g_gmp, g_built_gmp) && !strcmp(g_sty, g_built_sty))
        return 1;
    snprintf(g_built_gmp, sizeof g_built_gmp, "%s", g_gmp);
    snprintf(g_built_sty, sizeof g_built_sty, "%s", g_sty);
    if (g_tex) {
        wr_vk_texture_destroy(g_tex);
        g_tex = NULL;
    }
    char hg[1200], hs[1200];
    if (!wr_path_resolve_read(g_gmp, hg, sizeof hg) || !wr_path_resolve_read(g_sty, hs, sizeof hs)) {
        wr_log("radar: %s / %s not found", g_gmp, g_sty);
        return 0;
    }
    uint32_t t0 = wr_time_ms();
    uint32_t *img = gta2_radar_build_picture(hg, hs, PX);
    if (!img) {
        wr_log("radar: no map picture from %s + %s", g_gmp, g_sty);
        return 0;
    }
    g_tex = gbh_port_atlas_create(img, MAP_N * PX, MAP_N * PX);
    free(img);
    wr_log("radar: map picture of %s + %s in %u ms", g_gmp, g_sty, wr_time_ms() - t0);
    return g_tex != NULL;
}

/* ---- drawing ---------------------------------------------------------------------------------- */

typedef struct {
    float x, y, s;          /* the square on the game screen */
    float cx, cy, r;        /* the view: centre (blocks) and half width (blocks) */
} view;

static int to_screen(const view *v, float bx, float by, float *sx, float *sy)
{
    *sx = v->x + (bx - (v->cx - v->r)) / (2 * v->r) * v->s;
    *sy = v->y + (by - (v->cy - v->r)) / (2 * v->r) * v->s;
    return *sx >= v->x && *sx <= v->x + v->s && *sy >= v->y && *sy <= v->y + v->s;
}

static void draw_map(const view *v)
{
    gbh_port_rect(v->x, v->y, v->s, v->s, 0xFF000000u);
    float b0x = fmaxf(v->cx - v->r, 0), b1x = fminf(v->cx + v->r, MAP_N);
    float b0y = fmaxf(v->cy - v->r, 0), b1y = fminf(v->cy + v->r, MAP_N);
    if (b1x <= b0x || b1y <= b0y)
        return;
    float sx0, sy0, sx1, sy1;
    to_screen(v, b0x, b0y, &sx0, &sy0);
    to_screen(v, b1x, b1y, &sx1, &sy1);
    gbh_port_atlas_quad(g_tex, sx0, sy0, sx1 - sx0, sy1 - sy0, b0x / MAP_N, b0y / MAP_N, b1x / MAP_N, b1y / MAP_N,
                        0xFFFFFFFFu, 0);
}

static void draw_frame(const view *v)
{
    float t = fmaxf(1.0f, v->s / 90.0f);
    uint32_t c = 0xE0101010u;
    gbh_port_rect(v->x - t, v->y - t, v->s + 2 * t, t, c);
    gbh_port_rect(v->x - t, v->y + v->s, v->s + 2 * t, t, c);
    gbh_port_rect(v->x - t, v->y, t, v->s, c);
    gbh_port_rect(v->x + v->s, v->y, t, v->s, c);
}

/* ---- blips (docs/re/minimap.md) ----------------------------------------------------------------
 * Shapes are drawn here once into one texture: a white fill and a near-black rim, so the quad's colour
 * tints the fill and the rim stays dark. */

enum { SH_CIRCLE, SH_SQUARE, SH_DIAMOND, SH_UP, SH_DOWN, SH_CROSS, SH_RING, SH_X, SH_N };
#define SHAPE_PX 32
#define SHAPE_SS 4
static wr_vk_texture *g_shapes;

static int shape_in(int sh, float x, float y, float grow)   /* x, y in -1..1 */
{
    float ax = fabsf(x), ay = fabsf(y), g = grow;
    switch (sh) {
    case SH_CIRCLE: return x * x + y * y <= (0.62f + g) * (0.62f + g);
    case SH_SQUARE: return ax <= 0.52f + g && ay <= 0.52f + g;
    case SH_DIAMOND: return ax + ay <= 0.72f + g * 1.4f;
    case SH_UP: return y <= 0.55f + g && y >= -0.75f - g && ax <= (y + 0.75f) * 0.62f + g;
    case SH_DOWN: return y >= -0.55f - g && y <= 0.75f + g && ax <= (0.75f - y) * 0.62f + g;
    case SH_CROSS: return (ax <= 0.16f + g && ay <= 0.75f + g) ||            /* upright */
                          (fabsf(y + 0.2f) <= 0.16f + g && ax <= 0.5f + g);  /* cross bar, a bit high */
    case SH_RING: {
        float r = sqrtf(x * x + y * y);
        return r <= 0.66f + g && r >= 0.36f - g;
    }
    case SH_X: return (fabsf(x - y) <= 0.26f + g || fabsf(x + y) <= 0.26f + g) && ax <= 0.62f + g && ay <= 0.62f + g;
    }
    return 0;
}

static int shapes_ready(void)
{
    if (g_shapes)
        return 1;
    int w = SHAPE_PX * SH_N, h = SHAPE_PX;
    uint32_t *img = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if (!img)
        return 0;
    for (int sh = 0; sh < SH_N; sh++)
        for (int y = 0; y < SHAPE_PX; y++)
            for (int x = 0; x < SHAPE_PX; x++) {
                int fill = 0, edge = 0;
                for (int sy = 0; sy < SHAPE_SS; sy++)
                    for (int sx = 0; sx < SHAPE_SS; sx++) {
                        float px = ((float)x + ((float)sx + 0.5f) / SHAPE_SS) / SHAPE_PX * 2 - 1;
                        float py = ((float)y + ((float)sy + 0.5f) / SHAPE_SS) / SHAPE_PX * 2 - 1;
                        if (shape_in(sh, px, py, 0))
                            fill++;
                        else if (shape_in(sh, px, py, 0.17f))
                            edge++;
                    }
                float all = SHAPE_SS * SHAPE_SS, af = (float)fill / all, alpha = af + (float)edge / all;
                if (alpha <= 0)
                    continue;
                uint32_t g = (uint32_t)(20 + af / alpha * 235);
                img[(size_t)y * (size_t)w + (size_t)(sh * SHAPE_PX + x)] =
                    (uint32_t)(alpha * 255.0f + 0.5f) << 24 | g << 16 | g << 8 | g;
            }
    g_shapes = gbh_port_atlas_create(img, (uint32_t)w, (uint32_t)h);
    free(img);
    return g_shapes != NULL;
}

/* colours: A B G R (R in the low byte) */
#define RGB(r, g, b) (0xFF000000u | (uint32_t)(b) << 16 | (uint32_t)(g) << 8 | (uint32_t)(r))
#define C_MISSION RGB(255, 70, 255)
#define C_RED     RGB(255, 50, 50)
#define C_YELLOW  RGB(255, 220, 40)
#define C_GREEN   RGB(60, 225, 60)
#define C_CHURCH  RGB(255, 255, 255)
#define C_SPRAY   RGB(80, 160, 255)   /* garage block */
#define C_CRANE   RGB(175, 175, 175)
#define C_TOKEN   RGB(255, 205, 0)
#define C_WEAPON  RGB(255, 95, 60)
#define C_POWERUP RGB(70, 225, 255)
#define C_FRENZY  RGB(230, 25, 25)
#define C_POLICE  RGB(60, 110, 255)

/* object models (docs/re/minimap.md 3.2; COLLECT_n = 200 + n, names after the GTA 2 script compiler's
 * list: 00..23 weapons incl. car weapons, 28..40 power-ups) */
#define M_PHONE_RED0 0xB0
#define M_PHONE_GREEN1 0xB5
#define M_GARAGE 130
#define M_CRANE 137
#define M_CRUSHER 143
#define M_WEAPON0 200
#define M_WEAPON1 223
#define M_POWERUP0 228
#define M_POWERUP1 240
#define M_TOKEN 266
/* the KILL_FRENZY badge (docs/re/minimap.md 3.3: every frenzy start command of wil/ste/bil is bound to a
 * model 286 object or to a parked car); GTA2_RADAR_KF_MODEL may override it for tests */
static int g_kf_model = -1;   /* -1 = not read yet */
#define M_KF 286
#define OP_KF_START 0x1A6
#define OP_PARKED_CAR 426

#define OBJ_POOL_PTR 0x00665784u
#define OBJ_N 3825
#define OBJ_SIZE 0x2C
#define HUD_PTR 0x00672F40u
#define ARROW_MGR 0x1F18
#define ARROW_N_GAME 17
#define ARROW_SIZE 0x7C
#define SCRIPT_PTR 0x006644BCu
#define CAR_POOL_PTR 0x005E4CA0u   /* +4 active list head, link car+0x4C, 306 cars */
#define CAR_N 306
#define OP_CHURCH 0x1B6

static float q14(uint32_t v) { return (float)(int32_t)v / 16384.0f; }

/* a car that is destroyed or going: damage s16 car+0x74 at its cap 32000 (blown up - the game's own test,
 * mission script 0x0047B834, police 0x004B139F) or car+0x88 5/6 (taken off the lists); docs/re/minimap.md 9.1b.
 * A wreck keeps its model. */
static int car_wrecked(uint32_t c)
{
    uint32_t st = wr_rd32(c + 0x88);
    return (int16_t)wr_rd16(c + 0x74) >= 0x7D00 || st == 5 || st == 6;
}

typedef struct { float x, y, z; int shape; uint32_t colour; int keep_on_edge; float size; } blip;
#define MAX_BLIPS 512
static blip g_blips[MAX_BLIPS];
static int g_nblips;

static void add_blip(float x, float y, float z, int shape, uint32_t colour, int edge, float size)
{
    if (g_nblips < MAX_BLIPS)
        g_blips[g_nblips++] = (blip){ x, y, z, shape, colour, edge, size };
}

/* churches: the level script's command 0x1B6 (trigger rect centre at cmd+0x0C / +0x10), read once per script */
static float g_church[16][2];
static int g_nchurch;
static uint32_t g_church_script;

/* frenzy cars: a frenzy start command (0x1A6) bound (+0x0C) to a parked-car entry (426); the made Car* is
 * kept at entry+8 [I, docs/re/minimap.md 3.3] */
static uint32_t g_kf_entry[16];
static int g_nkf_car;

static uint32_t script_cmd(uint32_t s, uint32_t i)
{
    if (!i || i >= 6000)
        return 0;
    uint32_t off = wr_rd16(s + 0x46C + 2 * i);
    if (!off)
        return 0;
    uint32_t cmd = s + 0x334C + off;
    return wr_mem_valid(cmd, 0x20) ? cmd : 0;
}

static void scan_churches(void)
{
    uint32_t s = wr_rd32(SCRIPT_PTR);
    if (s == g_church_script)
        return;
    g_church_script = s;
    g_nchurch = 0;
    g_nkf_car = 0;
    if (!s || !wr_mem_valid(s + 0x46C, 2 * 6000))
        return;
    for (uint32_t i = 1; i < 6000; i++) {
        uint32_t cmd = script_cmd(s, i);
        if (!cmd)
            continue;
        uint32_t op = wr_rd16(cmd + 2);
        if (op == OP_CHURCH && g_nchurch < 16) {
            if (wr_settings_get_int("radar_log", 0)) {   /* the command's words, to check the layout */
                char t[512];
                size_t n = 0;
                for (uint32_t k = 0; k < 0x30 && n < sizeof t - 16; k += 4)
                    n += (size_t)snprintf(t + n, sizeof t - n, " %08X", wr_rd32(cmd + k));
                uint32_t ped = gta2_player_ped();
                wr_log("radar: church cmd %u:%s; player %.2f,%.2f,%.2f", i, t,
                       ped ? q14(wr_rd32(ped + 0x1AC)) : 0.0f, ped ? q14(wr_rd32(ped + 0x1B0)) : 0.0f,
                       ped ? q14(wr_rd32(ped + 0x1B4)) : 0.0f);
            }
            g_church[g_nchurch][0] = q14(wr_rd32(cmd + 0x0C));
            g_church[g_nchurch][1] = q14(wr_rd32(cmd + 0x10));
            g_nchurch++;
        } else if (op == OP_KF_START && g_nkf_car < 16) {
            uint32_t e = script_cmd(s, wr_rd16(cmd + 0x0C));
            if (e && wr_rd16(e + 2) == OP_PARKED_CAR)
                g_kf_entry[g_nkf_car++] = e;
        }
    }
    wr_log("radar: %d church(es), %d frenzy car(s) in the level script", g_nchurch, g_nkf_car);
}

/* a frenzy car still standing: its sprite place, 0 if the car is gone */
static int kf_car_place(uint32_t e, float *x, float *y, float *z)
{
    uint32_t car = wr_rd32(e + 8);
    if (!car || !wr_mem_valid(car, 0xBC) || car_wrecked(car))
        return 0;
    uint32_t spr = wr_rd32(car + 0x50);
    if (!spr || !wr_mem_valid(spr, 0x20))
        return 0;
    *x = q14(wr_rd32(spr + 0x14));
    *y = q14(wr_rd32(spr + 0x18));
    *z = q14(wr_rd32(spr + 0x1C));
    return *x > 0 && *y > 0 && *x < MAP_N && *y < MAP_N;
}

/* GTA2_RADAR_LOG=1: once per level, every model in the object pool with its count and first place */
static void log_models(uint32_t pool)
{
    static uint32_t logged_for;
    if (!wr_settings_get_int("radar_log", 0) || logged_for == pool)
        return;
    logged_for = pool;
    int count[1024] = { 0 };
    float fx[1024], fy[1024];
    char gl[2048];   /* garage triggers: x,y=obj+0x26 */
    size_t gn = 0;
    for (int k = 0; k < OBJ_N; k++) {
        uint32_t o = pool + 8 + (uint32_t)k * OBJ_SIZE, spr = wr_rd32(o + 4), m = wr_rd32(o + 0x18);
        if (!spr || m >= 1024 || !wr_mem_valid(spr, 0x20))
            continue;
        if (m == M_GARAGE && gn < sizeof gl - 32)
            gn += (size_t)snprintf(gl + gn, sizeof gl - gn, " %.1f,%.1f=%02X", q14(wr_rd32(spr + 0x14)),
                                   q14(wr_rd32(spr + 0x18)), wr_rd8(o + 0x26));
        if (!count[m]++) {
            fx[m] = q14(wr_rd32(spr + 0x14));
            fy[m] = q14(wr_rd32(spr + 0x18));
        }
    }
    char line[4096];   /* one line: the log folds lines that look alike */
    size_t n = 0;
    for (int m = 0; m < 1024 && n < sizeof line - 40; m++)
        if (count[m])
            n += (size_t)snprintf(line + n, sizeof line - n, " %d:x%d@%.0f,%.0f", m, count[m], fx[m], fy[m]);
    line[n < sizeof line ? n : sizeof line - 1] = 0;
    wr_log("radar: models (model:count@first x,y):%s", line);
    gl[gn < sizeof gl ? gn : sizeof gl - 1] = 0;
    wr_log("radar: garage triggers (x,y=obj+0x26):%s", gl);
}

static void collect_blips(float pz)
{
    g_nblips = 0;
    int missions = opt_get(OPT_BLIP_MISSIONS), places = opt_get(OPT_BLIP_PLACES);
    /* objects */
    uint32_t pool = wr_rd32(OBJ_POOL_PTR);
    if (pool && wr_mem_valid(pool, 8 + OBJ_N * OBJ_SIZE)) {
        log_models(pool);
        for (int k = 0; k < OBJ_N; k++) {
            uint32_t o = pool + 8 + (uint32_t)k * OBJ_SIZE, spr = wr_rd32(o + 4);
            if (!spr)
                continue;
            int m = (int)wr_rd32(o + 0x18);
            int shape = -1, on = 0;
            uint32_t col = 0;
            float size = 1.0f;
            if (m >= M_PHONE_RED0 && m <= M_PHONE_GREEN1) {
                static const uint32_t pc[3] = { C_RED, C_YELLOW, C_GREEN };
                on = missions, shape = SH_RING, col = pc[(m - M_PHONE_RED0) / 2], size = 1.15f;
            } else if (m == M_GARAGE) {
                /* a garage block: four doors side by side (log of wil: obj+0x26 = a respray colour, FA, FB,
                 * FE - the spray shop and three car-weapon shops), one blip per block */
                on = places, shape = SH_SQUARE, col = C_SPRAY, size = 1.1f;
            } else if (m == M_CRANE || m == M_CRUSHER)
                on = places, shape = SH_SQUARE, col = C_CRANE, size = 1.1f;
            else if (m == M_TOKEN)
                on = opt_get(OPT_BLIP_TOKENS), shape = SH_DIAMOND, col = C_TOKEN, size = 0.8f;
            else if (m >= M_WEAPON0 && m <= M_WEAPON1)
                on = opt_get(OPT_BLIP_WEAPONS), shape = SH_SQUARE, col = C_WEAPON, size = 0.7f;
            else if (m >= M_POWERUP0 && m <= M_POWERUP1)
                on = opt_get(OPT_BLIP_BONUSES), shape = SH_CIRCLE, col = C_POWERUP, size = 0.75f;
            else if (m == g_kf_model)
                on = opt_get(OPT_BLIP_FRENZY), shape = SH_X, col = C_FRENZY, size = 1.0f;
            if (!on || shape < 0 || !wr_mem_valid(spr, 0x20))
                continue;
            if (m == M_GARAGE || m == M_CRANE || m == M_CRUSHER) {
                /* one garage / crane has several trigger objects (24 in Downtown): one blip per place */
                float x = q14(wr_rd32(spr + 0x14)), y = q14(wr_rd32(spr + 0x18));
                int dup = 0;   /* the doors of one block lie within 6 blocks */
                for (int i = 0; i < g_nblips && !dup; i++)
                    dup = g_blips[i].colour == col && fabsf(g_blips[i].x - x) < 7 && fabsf(g_blips[i].y - y) < 7;
                if (dup)
                    continue;
            }
            add_blip(q14(wr_rd32(spr + 0x14)), q14(wr_rd32(spr + 0x18)), q14(wr_rd32(spr + 0x1C)), shape, col, 0,
                     size);
        }
    }
    /* churches */
    scan_churches();
    if (places)
        for (int i = 0; i < g_nchurch; i++)
            add_blip(g_church[i][0], g_church[i][1], pz, SH_CROSS, C_CHURCH, 0, 1.2f);
    /* frenzy cars - not while a frenzy runs ([[0x6644BC]+0xC1E2D]): then the player is in it or past it */
    uint32_t s = wr_rd32(SCRIPT_PTR);
    if (opt_get(OPT_BLIP_FRENZY) && s && wr_mem_valid(s + 0xC1E2D, 1) && !wr_rd8(s + 0xC1E2D))
        for (int i = 0; i < g_nkf_car; i++) {
            float x, y, z;
            if (kf_car_place(g_kf_entry[i], &x, &y, &z))
                add_blip(x, y, z, SH_X, C_FRENZY, 0, 1.0f);
        }
    /* POLICE (maintainer 2026-11-27: patrol cars and SWAT vans): the active car list (docs/re/minimap.md 9) -
     * cop car 12, SWAT van 52, special agent car 84 (the game's own police test sub_0041F6E0); the army
     * (22 / 54 / 3) is left out; each car checked on its own: no wrecks (car_wrecked); crewed (driver
     * car+0x54) bigger than parked */
    uint32_t cars = wr_rd32(CAR_POOL_PTR);
    if (opt_get(OPT_BLIP_POLICE) && cars && wr_mem_valid(cars, 8)) {
        uint32_t c = wr_rd32(cars + 4);
        for (int k = 0; c && k < CAR_N; k++, c = wr_rd32(c + 0x4C)) {
            if (!wr_mem_valid(c, 0xBC))
                break;
            uint32_t m = wr_rd32(c + 0x84);
            if ((m != 12 && m != 52 && m != 84) || car_wrecked(c))
                continue;
            uint32_t spr = wr_rd32(c + 0x50);
            if (!spr || !wr_mem_valid(spr, 0x20))
                continue;
            add_blip(q14(wr_rd32(spr + 0x14)), q14(wr_rd32(spr + 0x18)), q14(wr_rd32(spr + 0x1C)), SH_CIRCLE,
                     C_POLICE, 0, wr_rd32(c + 0x54) ? 0.85f : 0.55f);
        }
    }
    /* mission targets: the game's own script arrows (gang arrows point at the phones drawn above) */
    uint32_t hud = wr_rd32(HUD_PTR);
    if (missions && hud && wr_mem_valid(hud + ARROW_MGR, ARROW_N_GAME * ARROW_SIZE))
        for (int i = 0; i < ARROW_N_GAME; i++) {
            uint32_t a = hud + ARROW_MGR + (uint32_t)i * ARROW_SIZE;
            if (!wr_rd8(a + 0x2E) || !wr_rd8(a + 0x2D) || wr_rd32(a + 0x28))
                continue;
            uint32_t t = wr_rd32(a + 0x78);
            if (!t || !wr_mem_valid(t, 0x24) || !wr_rd32(t + 0x10))
                continue;
            add_blip(q14(wr_rd32(t + 0x14)), q14(wr_rd32(t + 0x18)), q14(wr_rd32(t + 0x1C)), SH_CIRCLE, C_MISSION,
                     1, 1.35f);
        }
}

static void draw_blips(const view *v, float pz)
{
    if (!g_nblips || !shapes_ready())
        return;
    /* as the minimap grows; the PAUSE MAP keeps the LARGE minimap's size */
    float base = fmaxf(7.0f, fminf(v->s, (float)g_gfx.h * 0.36f) / 20.0f);
    for (int pass = 0; pass < 2; pass++)   /* pickups first, mission targets on top */
        for (int i = 0; i < g_nblips; i++) {
            const blip *b = &g_blips[i];
            if (b->keep_on_edge != pass)
                continue;
            float sx, sy;
            int in = to_screen(v, b->x, b->y, &sx, &sy);
            float d = base * b->size;
            if (!in) {
                if (!b->keep_on_edge)
                    continue;
                /* outside: on the rim, towards it */
                float h = d / 2;
                sx = fminf(fmaxf(sx, v->x + h), v->x + v->s - h);
                sy = fminf(fmaxf(sy, v->y + h), v->y + v->s - h);
            }
            int shape = b->shape;
            if (b->keep_on_edge) {   /* height: above / below the player by more than one floor */
                if (b->z > pz + 1.2f)
                    shape = SH_UP;
                else if (b->z < pz - 1.2f)
                    shape = SH_DOWN;
            }
            float u0 = (float)shape / SH_N;
            gbh_port_atlas_quad(g_shapes, sx - d / 2, sy - d / 2, d, d, u0, 0, u0 + 1.0f / SH_N, 1, b->colour, 0);
        }
}

/* ---- NAVIGATION ARROWS ------------------------------------------------------------------------ */

#define NEAR_BLOCKS 14.0f   /* NEAR TARGET: an arrow shows within about two screens of its target */

uint32_t gta2_radar_arrows_hide(uint32_t mgr)
{
    int mode = opt_get(OPT_NAV_ARROWS);
    if (mode == 0)
        return 0xFFFFFFFFu;
    if (mode == 1)
        return 0;
    uint32_t ped = gta2_player_ped(), hidden = 0;
    if (!ped || !wr_mem_valid(ped, 0x1B8) || !wr_mem_valid(mgr, ARROW_N_GAME * ARROW_SIZE))
        return 0;
    float px = q14(wr_rd32(ped + 0x1AC)), py = q14(wr_rd32(ped + 0x1B0));
    uint32_t car = wr_rd32(ped + 0x16C);
    if (car && wr_mem_valid(car, 0x54)) {
        uint32_t spr = wr_rd32(car + 0x50);
        if (spr && wr_mem_valid(spr, 0x20)) {
            px = q14(wr_rd32(spr + 0x14));
            py = q14(wr_rd32(spr + 0x18));
        }
    }
    for (int i = 0; i < ARROW_N_GAME; i++) {
        uint32_t a = mgr + (uint32_t)i * ARROW_SIZE, t = wr_rd32(a + 0x78);
        if (!wr_rd8(a + 0x2D) || !t || !wr_mem_valid(t, 0x24))
            continue;
        float dx = q14(wr_rd32(t + 0x14)) - px, dy = q14(wr_rd32(t + 0x18)) - py;
        if (dx * dx + dy * dy > NEAR_BLOCKS * NEAR_BLOCKS) {
            wr_wr8(a + 0x2D, 0);
            hidden |= 1u << i;
        }
    }
    return hidden;
}

void gta2_radar_arrows_restore(uint32_t mgr, uint32_t hidden)
{
    for (int i = 0; i < ARROW_N_GAME; i++)
        if (hidden & (1u << i))
            wr_wr8(mgr + (uint32_t)i * ARROW_SIZE + 0x2D, 1);
}

/* ---- the player's arrow: drawn here once, ARROW_N turns side by side in one texture ----------- */

#define ARROW_PX 48      /* one turn's square */
#define ARROW_N  64      /* turns: 8 x 8 in a 384 x 384 texture */
#define ARROW_SS 4       /* supersampling per axis */
static wr_vk_texture *g_arrow;

static int in_tri(float x, float y, const float *t)   /* t: 3 points */
{
    float d0 = (t[2] - t[0]) * (y - t[1]) - (t[3] - t[1]) * (x - t[0]);
    float d1 = (t[4] - t[2]) * (y - t[3]) - (t[5] - t[3]) * (x - t[2]);
    float d2 = (t[0] - t[4]) * (y - t[5]) - (t[1] - t[5]) * (x - t[4]);
    return (d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0);
}

/* an arrowhead pointing up (-y) with a notch at the back, in units of the square (-1..1) */
static int arrow_shape(float x, float y, float grow)
{
    float tip = -0.80f - grow, back = 0.62f + grow, wing = 0.62f + grow, notch = 0.28f + grow * 0.3f;
    float l[6] = { 0, tip, -wing, back, 0, notch };
    float r[6] = { 0, tip, wing, back, 0, notch };
    return in_tri(x, y, l) || in_tri(x, y, r);
}

static int arrow_ready(void)
{
    if (g_arrow)
        return 1;
    int cols = 8, w = ARROW_PX * cols;
    uint32_t *img = calloc((size_t)w * (size_t)w, sizeof(uint32_t));
    if (!img)
        return 0;
    for (int n = 0; n < ARROW_N; n++) {
        float a = (float)n * 6.2831853f / ARROW_N, ca = cosf(a), sa = sinf(a);
        int ox = n % cols * ARROW_PX, oy = n / cols * ARROW_PX;
        for (int y = 0; y < ARROW_PX; y++)
            for (int x = 0; x < ARROW_PX; x++) {
                int fill = 0, edge = 0;
                for (int sy = 0; sy < ARROW_SS; sy++)
                    for (int sx = 0; sx < ARROW_SS; sx++) {
                        float px = ((float)x + ((float)sx + 0.5f) / ARROW_SS) / ARROW_PX * 2 - 1;
                        float py = ((float)y + ((float)sy + 0.5f) / ARROW_SS) / ARROW_PX * 2 - 1;
                        /* turn the sample back by a: the arrow turns clockwise on screen by a */
                        float qx = px * ca + py * sa, qy = -px * sa + py * ca;
                        if (arrow_shape(qx, qy, 0))
                            fill++;
                        else if (arrow_shape(qx, qy, 0.16f))
                            edge++;
                    }
                int all = ARROW_SS * ARROW_SS;
                float af = (float)fill / all, ae = (float)edge / all;
                float alpha = af + ae;
                if (alpha <= 0)
                    continue;
                /* white fill, near-black edge, mixed at the boundary */
                float c = af / alpha;
                uint32_t g = (uint32_t)(20 + c * 235);
                uint32_t A = (uint32_t)(alpha * 255.0f + 0.5f);
                img[(size_t)(oy + y) * (size_t)w + (size_t)(ox + x)] = A << 24 | g << 16 | g << 8 | g;
            }
    }
    g_arrow = gbh_port_atlas_create(img, (uint32_t)w, (uint32_t)w);
    free(img);
    return g_arrow != NULL;
}

/* The player's heading as sub_004361B0 reads it (docs/re/gameplay_input_pause.md §3): on foot the ped
 * object's u16 at [ped+0x168]+0x40, in a car the car sprite's u16 at [car+0x50]+0. Ang16 0..1439,
 * 0 = screen down (+y), counter-clockwise on screen. Returned: radians clockwise from north. */
static float player_angle(uint32_t ped)
{
    uint32_t a = 0, obj = wr_rd32(ped + 0x168), car = wr_rd32(ped + 0x16C);
    if (obj && wr_mem_valid(obj, 0x44))
        a = wr_rd16(obj + 0x40);
    else if (car && wr_mem_valid(car, 0x54)) {
        uint32_t spr = wr_rd32(car + 0x50);
        if (spr && wr_mem_valid(spr, 2))
            a = wr_rd16(spr);
    }
    return 3.14159265f - (float)(a % 1440) * (6.2831853f / 1440.0f);
}

/* angle: radians, clockwise on screen from north (up) */
static void draw_player(const view *v, float bx, float by, float angle)
{
    float sx, sy;
    to_screen(v, bx, by, &sx, &sy);
    float d = fmaxf(12.0f, v->s / 9.0f);
    if (!arrow_ready()) {
        gbh_port_rect(sx - d / 6, sy - d / 6, d / 3, d / 3, 0xFFFFFFFFu);
        return;
    }
    float t = angle / 6.2831853f;
    t -= floorf(t);
    int n = (int)lroundf(t * ARROW_N) % ARROW_N;
    float u0 = (float)(n % 8) / 8.0f, v0 = (float)(n / 8) / 8.0f;
    gbh_port_atlas_quad(g_arrow, sx - d / 2, sy - d / 2, d, d, u0, v0, u0 + 0.125f, v0 + 0.125f, 0xFFFFFFFFu, 0);
}

/* the player's ped and place (in a car: the car's sprite, docs/re/minimap.md 5); 0 = not in play */
static uint32_t player_place(float *px, float *py, float *pz)
{
    uint32_t ped = gta2_player_ped();
    if (!ped || !wr_mem_valid(ped, 0x1B8) || !picture_ready())
        return 0;
    *px = q14(wr_rd32(ped + 0x1AC));
    *py = q14(wr_rd32(ped + 0x1B0));
    *pz = q14(wr_rd32(ped + 0x1B4));
    uint32_t car = wr_rd32(ped + 0x16C);
    if (car && wr_mem_valid(car, 0x54)) {
        uint32_t spr = wr_rd32(car + 0x50);
        if (spr && wr_mem_valid(spr, 0x20)) {
            *px = q14(wr_rd32(spr + 0x14));
            *py = q14(wr_rd32(spr + 0x18));
            *pz = q14(wr_rd32(spr + 0x1C));
        }
    }
    if (g_kf_model == -1)
        g_kf_model = wr_settings_get_int("radar_kf_model", M_KF);
    return ped;
}

/* ---- PAUSE MAP legend --------------------------------------------------------------------------- */

typedef struct { const char *label; int opt; int shape; uint32_t colour; } legend_item;
#define SH_PLAYER (-1)
static const legend_item g_legend[] = {
    { "YOU", -1, SH_PLAYER, 0xFFFFFFFFu },
    { "MISSION", OPT_BLIP_MISSIONS, SH_CIRCLE, C_MISSION },
    { "GANG PHONE", OPT_BLIP_MISSIONS, SH_RING, C_YELLOW },
    { "CHURCH", OPT_BLIP_PLACES, SH_CROSS, C_CHURCH },
    { "GARAGE", OPT_BLIP_PLACES, SH_SQUARE, C_SPRAY },
    { "CRANE", OPT_BLIP_PLACES, SH_SQUARE, C_CRANE },
    { "TOKEN", OPT_BLIP_TOKENS, SH_DIAMOND, C_TOKEN },
    { "WEAPON", OPT_BLIP_WEAPONS, SH_SQUARE, C_WEAPON },
    { "POWER-UP", OPT_BLIP_BONUSES, SH_CIRCLE, C_POWERUP },
    { "KILL FRENZY", OPT_BLIP_FRENZY, SH_X, C_FRENZY },
    { "POLICE", OPT_BLIP_POLICE, SH_CIRCLE, C_POLICE },
};

const char *gta2_radar_legend(int n, float x, float y, float d)
{
    for (size_t i = 0; i < sizeof g_legend / sizeof g_legend[0]; i++) {
        const legend_item *it = &g_legend[i];
        if (it->opt >= 0 && !opt_get(it->opt))
            continue;
        if (n-- > 0)
            continue;
        if (it->shape == SH_PLAYER) {
            if (arrow_ready())
                gbh_port_atlas_quad(g_arrow, x - d * 0.75f, y - d * 0.75f, d * 1.5f, d * 1.5f, 0, 0, 0.125f, 0.125f,
                                    0xFFFFFFFFu, 0);
        } else if (shapes_ready()) {
            float u0 = (float)it->shape / SH_N;
            gbh_port_atlas_quad(g_shapes, x - d / 2, y - d / 2, d, d, u0, 0, u0 + 1.0f / SH_N, 1, it->colour, 0);
        }
        return it->label;
    }
    return NULL;
}

/* PAUSE MAP: the whole district in the square (x, y, s) of the game screen; 0 if there is none */
int gta2_radar_draw_full(float x, float y, float s)
{
    float px, py, pz;
    uint32_t ped = player_place(&px, &py, &pz);
    if (!ped)
        return 0;
    view v = { x, y, s, MAP_N / 2.0f, MAP_N / 2.0f, MAP_N / 2.0f };
    collect_blips(pz);
    draw_map(&v);
    draw_blips(&v, pz);
    float keep = v.s;   /* the arrow and blips as big as on a LARGE minimap */
    v.s = fminf(keep, (float)g_gfx.h * 0.36f);
    float sx, sy;
    to_screen(&(view){ x, y, keep, v.cx, v.cy, v.r }, px, py, &sx, &sy);
    view pv = { sx - v.s / 2, sy - v.s / 2, v.s, px, py, v.r };
    draw_player(&pv, px, py, player_angle(ped));
    v.s = keep;
    draw_frame(&v);
    return 1;
}

void gta2_radar_draw(X86Ctx *ctx)
{
    (void)ctx;
    if (!opt_get(OPT_MINIMAP))
        return;
    float px, py, pz;
    uint32_t ped = player_place(&px, &py, &pz);
    if (!ped)
        return;
    static const float size[] = { 0.22f, 0.28f, 0.36f };     /* of the screen height */
    static const float half[] = { 10.0f, 16.0f, 26.0f };     /* blocks from the middle to the edge */
    float sw = (float)g_gfx.w, sh = (float)g_gfx.h;
    view v;
    v.s = floorf(sh * size[opt_get(OPT_MINIMAP_SIZE)]);
    /* POSITION X / Y: 0..100 % of the free room; at 0 / 100 % it sits in the corner, only its frame
     * (drawn outside the square) kept on screen (maintainer 2026-11-27: "ins letzte Eck") */
    float m = fmaxf(1.0f, v.s / 90.0f);
    float fx = (float)opt_get(OPT_MINIMAP_X) / 40.0f, fy = (float)opt_get(OPT_MINIMAP_Y) / 40.0f;
    v.x = floorf(m + (sw - v.s - 2 * m) * fx);
    v.y = floorf(m + (sh - v.s - 2 * m) * fy);
    v.cx = px;
    v.cy = py;
    v.r = half[opt_get(OPT_MINIMAP_ZOOM)];
    collect_blips(pz);
    draw_map(&v);
    draw_blips(&v, pz);
    draw_player(&v, px, py, player_angle(ped));
    draw_frame(&v);
}
