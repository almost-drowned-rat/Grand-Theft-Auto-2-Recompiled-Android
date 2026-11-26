/*
 * GTA 2 Recompiled - the SHADERS page: RetroArch slang shaders (option-c's
 * shader pass + shader packs; THPS 2X's menu as the model).
 *
 *   SHADER   OFF / ON           settings.ini shader_on
 *   PRESET   the preset in use  settings.ini shader_preset (path of the .slangp)
 * The browser lists the packs of <root>/shader (extracted to data/shaders by
 * shader_pack_start at start-up): folders first, presets with their pass
 * count, ".." to go up, NONE on top.
 */
#include "gta2_shaders.h"
#include "wr.h"
#include "post/shader_pass.h"
#include "post/wr_post.h"
#include "shaderpack/shader_pack.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

static char g_dir[512];
static const char *single_pack(void);   /* the browser's folder: "" = the packs, else "<pack>/<path>" */

/* settings key of a parameter: kept per preset (as the N64 recomps) */
static void param_key(const char *preset, const char *name, char *out, size_t cap)
{
    uint32_t h = 2166136261u;
    for (const char *c = preset; *c; c++)
        h = (h ^ (uint8_t)*c) * 16777619u;
    snprintf(out, cap, "shader_param_%08x_%s", h, name);
}

static int g_nparams;
static WrPostShaderParam g_params[128];
static unsigned g_params_gen;

void gta2_shaders_apply(void)
{
    const char *p = wr_settings_get("shader_preset", NULL);
    int on = wr_settings_get_int("shader_on", 0) && p && *p;
    sp_set_preset(on ? p : NULL);
    if (!on)
        return;
    /* the saved parameters of this preset: every key shader_param_<hash>_<name> */
    char prefix[64];
    param_key(p, "", prefix, sizeof prefix);
    size_t pl = strlen(prefix);
    for (int i = 0; ; i++) {
        const char *k = wr_settings_key_at(i);
        if (!k)
            break;
        if (!strncmp(k, prefix, pl)) {
            const char *v = wr_settings_get(k, NULL);
            if (v)
                sp_set_param(k + pl, (float)atof(v));
        }
    }
}

/* ---- parameters of the preset in use ------------------------------------------------ */

int gta2_shader_params(void)
{
    g_nparams = wr_post_shader_params(g_params, 128, &g_params_gen);
    return g_nparams;
}

const char *gta2_shader_param_label(int i)
{
    static char lab[128][32];
    if (i < 0 || i >= g_nparams)
        return "";
    const char *d = g_params[i].desc[0] ? g_params[i].desc : g_params[i].name;
    size_t j = 0;
    for (; d[j] && j < 26; j++)
        lab[i][j] = (char)toupper((unsigned char)d[j]);
    lab[i][j] = 0;
    return lab[i];
}

static float param_value(int i)
{
    const char *p = wr_settings_get("shader_preset", "");
    char key[160];
    param_key(p, g_params[i].name, key, sizeof key);
    const char *v = wr_settings_get(key, NULL);
    return v ? (float)atof(v) : g_params[i].initial;
}

void gta2_shader_param_value(int i, char *out, size_t cap)
{
    if (i < 0 || i >= g_nparams) { out[0] = 0; return; }
    float v = param_value(i);
    if (g_params[i].step >= 1.0f || (float)(int)v == v)
        snprintf(out, cap, "%d", (int)v);
    else
        snprintf(out, cap, "%.2f", v);
}

void gta2_shader_param_step(int i, int d)
{
    if (i < 0 || i >= g_nparams)
        return;
    float st = g_params[i].step > 0 ? g_params[i].step : (g_params[i].maximum - g_params[i].minimum) / 20.0f;
    float v = param_value(i) + (float)d * st;
    if (v < g_params[i].minimum) v = g_params[i].minimum;
    if (v > g_params[i].maximum) v = g_params[i].maximum;
    char key[160], val[32];
    param_key(wr_settings_get("shader_preset", ""), g_params[i].name, key, sizeof key);
    snprintf(val, sizeof val, "%g", v);
    wr_settings_set(key, val);
    sp_set_param(g_params[i].name, v);
}

void gta2_shader_param_reset(int i)
{
    if (i < 0 || i >= g_nparams)
        return;
    char key[160], val[32];
    param_key(wr_settings_get("shader_preset", ""), g_params[i].name, key, sizeof key);
    snprintf(val, sizeof val, "%g", g_params[i].initial);
    wr_settings_set(key, val);
    sp_set_param(g_params[i].name, g_params[i].initial);
}

int gta2_shader_resolution(void) { return wr_settings_get_int("shader_resolution", 0); }   /* 0 original, 1 2x, 2 full */
void gta2_shader_set_resolution(int r) { wr_settings_set_int("shader_resolution", ((r % 3) + 3) % 3); }

int gta2_shader_on(void) { return wr_settings_get_int("shader_on", 0); }

void gta2_shader_set_on(int on)
{
    wr_settings_set_int("shader_on", on != 0);
    gta2_shaders_apply();
}

/* upper-case display name of a path's last part, without .slangp */
static void display(const char *path, char *out, size_t cap)
{
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    size_t len = strlen(b);
    if (len > 7 && !strcmp(b + len - 7, ".slangp"))
        len -= 7;   /* the extension first, then the cut */
    size_t j = 0;
    for (; j < len && j + 1 < cap; j++)
        out[j] = b[j] == '_' || b[j] == '.' ? ' ' : (char)toupper((unsigned char)b[j]);
    out[j] = 0;
}

void gta2_shader_preset_name(char *out, size_t cap)
{
    const char *p = wr_settings_get("shader_preset", NULL);
    if (!p || !*p)
        snprintf(out, cap, "OFF");
    else
        display(p, out, cap);
}

const char *gta2_shader_hint(void)
{
    int st = sp_status();
    if (st == WR_POST_SHADER_NO_LIBRARY)
        return "NEEDS LIBRASHADER IN THE GAME FOLDER";
    if (shader_pack_state() == SHADER_PACK_WORKING)
        return "PREPARING THE SHADER PACKS";
    const ShaderIndex *ix = shader_pack_index();
    if (!ix || !ix->n)
        return "NEEDS A SHADER PACK IN MEDIA SHADERS";
    if (st == WR_POST_SHADER_LOADING)
        return "LOADING";
    if (st == WR_POST_SHADER_FAILED)
        return "THIS PRESET DID NOT LOAD";
    return NULL;
}

/* ---- browser -------------------------------------------------------------------------------- */

#define MAX_ITEMS 1024
static gta2_shader_item g_items[MAX_ITEMS];
static char g_names[MAX_ITEMS][48];
static int g_nitems;

static int item_cmp(const void *a, const void *b)
{
    const gta2_shader_item *x = a, *y = b;
    if (x->kind != y->kind)
        return x->kind - y->kind;
    return strcasecmp(x->key, y->key);   /* the file names, as the N64 recomps */
}

int gta2_shader_browse(const gta2_shader_item **items)
{
    g_nitems = 0;
    const ShaderIndex *ix = shader_pack_index();
    int first = g_nitems;
    size_t dl = strlen(g_dir);
    const char *cur = wr_settings_get("shader_preset", NULL);
    for (int i = 0; ix && i < ix->n && g_nitems < MAX_ITEMS; i++) {
        const char *rel = ix->p[i].rel;
        if (dl && (strncmp(rel, g_dir, dl) || rel[dl] != '/'))
            continue;
        const char *rest = dl ? rel + dl + 1 : rel;
        const char *slash = strchr(rest, '/');
        char name[48];
        int folder = slash != NULL;
        size_t n = folder ? (size_t)(slash - rest) : strlen(rest);
        if (n >= sizeof name)
            n = sizeof name - 1;
        memcpy(name, rest, n);
        name[n] = 0;
        if (folder) {   /* once per folder */
            int seen = 0;
            for (int k = first; k < g_nitems; k++)
                if (g_items[k].kind == SI_FOLDER && !strcmp(g_items[k].key, name))
                    seen = 1;
            if (seen)
                continue;
        }
        char *lab = g_names[g_nitems];
        int chosen = !folder && cur && !strcmp(ix->p[i].path, cur);
        if (chosen)
            snprintf(lab, 3, "* ");   /* the preset in use (the N64 recomps' bullet) */
        display(name, lab + (chosen ? 2 : 0), 44 - (chosen ? 2 : 0));
        if (folder)
            snprintf(lab + strlen(lab), sizeof g_names[0] - strlen(lab), " >");
        gta2_shader_item it = { lab, folder ? SI_FOLDER : SI_PRESET, folder ? -1 : i, "" };
        snprintf(it.key, sizeof it.key, "%s", name);
        g_items[g_nitems++] = it;
    }
    qsort(&g_items[first], (size_t)(g_nitems - first), sizeof g_items[0], item_cmp);
    *items = g_items;
    return g_nitems;
}

/* Enter on an item: 1 = the list changed (folder / up), 0 = a choice was made */
int gta2_shader_pick(const gta2_shader_item *it)
{
    const ShaderIndex *ix = shader_pack_index();
    switch (it->kind) {
    case SI_UP: {
        char *s = strrchr(g_dir, '/');
        if (s)
            *s = 0;
        else
            g_dir[0] = 0;
        return 1;
    }
    case SI_FOLDER: {
        size_t l = strlen(g_dir);
        snprintf(g_dir + l, sizeof g_dir - l, "%s%s", l ? "/" : "", it->key);
        return 1;
    }
    case SI_NONE:
        wr_settings_set("shader_preset", "");
        gta2_shaders_apply();
        return 0;
    default:
        if (ix && it->index >= 0 && it->index < ix->n) {
            wr_settings_set("shader_preset", ix->p[it->index].path);
            wr_settings_set_int("shader_on", 1);
            gta2_shaders_apply();
        }
        return 0;
    }
}

/* With one pack only, its folders are the top level (no extra "pack" step). */
static const char *single_pack(void)
{
    static char one[256];
    const ShaderIndex *ix = shader_pack_index();
    one[0] = 0;
    for (int i = 0; ix && i < ix->n; i++) {
        const char *rel = ix->p[i].rel, *sl = strchr(rel, '/');
        size_t n = sl ? (size_t)(sl - rel) : strlen(rel);
        if (!one[0]) {
            snprintf(one, sizeof one, "%.*s", (int)n, rel);
        } else if (strlen(one) != n || strncmp(one, rel, n)) {
            one[0] = 0;
            return "";   /* several packs: they are the top level */
        }
    }
    return one;
}

/* the browser opens on the folder of the preset in use */
void gta2_shader_browse_open(void)
{
    snprintf(g_dir, sizeof g_dir, "%s", single_pack());
    const char *p = wr_settings_get("shader_preset", NULL);
    const ShaderIndex *ix = shader_pack_index();
    for (int i = 0; p && *p && ix && i < ix->n; i++)
        if (!strcmp(ix->p[i].path, p)) {
            snprintf(g_dir, sizeof g_dir, "%s", ix->p[i].rel);
            char *s = strrchr(g_dir, '/');
            if (s)
                *s = 0;
            else
                g_dir[0] = 0;
        }
    if (!g_dir[0])
        snprintf(g_dir, sizeof g_dir, "%s", single_pack());
}

void gta2_shader_dir_title(char *out, size_t cap)
{
    if (!g_dir[0] || !strcmp(g_dir, single_pack())) {
        snprintf(out, cap, "SHADER SELECTION");
        return;
    }
    display(g_dir, out, cap);
}

/* ---- the test picture behind the SHADERS pages (frontend) ------------------------------ *
 * The player's data/shader_test.bmp when there is one (e.g. a screenshot of
 * his own game), else made here, no game data: colour bars, a grey ramp, fine line patterns,
 * a checkerboard, circles and a small pixel-art city block - what shows
 * scanlines, masks, sharpness, colour and gamma of a preset. */
#define TW 640
#define TH 480
static uint32_t g_test[TW * TH];

static uint32_t rgb(int r, int g, int b) { return (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | 0xFF000000u; }

/* the player's own test picture: data/shader_test.bmp (any size, scaled to 640x480) */
static int load_test_bmp(void)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/shader_test.bmp", g_wr_paths.data);
    SDL_Surface *s = SDL_LoadBMP(path);
    if (!s)
        return 0;
    SDL_Surface *c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!c)
        return 0;
    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++) {
            const uint8_t *p = (const uint8_t *)c->pixels + (size_t)(y * c->h / TH) * (size_t)c->pitch +
                               (size_t)(x * c->w / TW) * 4;
            g_test[y * TW + x] = rgb(p[0], p[1], p[2]);
        }
    SDL_DestroySurface(c);
    wr_log("shaders: test picture %s", path);
    return 1;
}

const uint32_t *gta2_shader_test_picture(void)
{
    static int made;
    if (made)
        return g_test;
    made = 1;
    if (load_test_bmp())
        return g_test;
    static const int bars[8][3] = { { 192, 192, 192 }, { 192, 192, 0 }, { 0, 192, 192 }, { 0, 192, 0 },
                                    { 192, 0, 192 }, { 192, 0, 0 }, { 0, 0, 192 }, { 16, 16, 16 } };
    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++) {
            uint32_t c;
            if (y < 120) {                                   /* colour bars */
                const int *b = bars[x * 8 / TW];
                c = rgb(b[0], b[1], b[2]);
            } else if (y < 160) {                            /* grey ramp */
                int v = x * 255 / (TW - 1);
                c = rgb(v, v, v);
            } else if (y < 200) {                            /* red / green / blue ramps */
                int v = (x % (TW / 3)) * 255 / (TW / 3 - 1), k = x * 3 / TW;
                c = rgb(k == 0 ? v : 0, k == 1 ? v : 0, k == 2 ? v : 0);
            } else if (y < 280) {                            /* fine lines: 1 px vertical, horizontal, checker */
                int seg = x * 4 / TW;
                int on = seg == 0 ? x & 1 : seg == 1 ? y & 1 : seg == 2 ? (x ^ y) & 1 : ((x >> 2) ^ (y >> 2)) & 1;
                c = on ? rgb(235, 235, 235) : rgb(20, 20, 20);
            } else {                                         /* a small pixel city: blocks, roads, circles */
                int bx = x / 40, by = (y - 280) / 40, ix = x % 40, iy = (y - 280) % 40;
                int road = ix < 6 || iy < 6;
                int shade = (bx * 7 + by * 13) % 5;
                c = road ? rgb(60, 60, 64) : rgb(90 + shade * 25, 50 + shade * 12, 40 + shade * 8);
                if (!road && (ix == 6 || iy == 6))
                    c = rgb(220, 200, 60);                   /* kerb */
                int dx = x - 480, dy = y - 380, r2 = dx * dx + dy * dy;
                if (r2 < 70 * 70 && r2 > 64 * 64)
                    c = rgb(255, 255, 255);                  /* circles: aspect and smoothing */
                dx = x - 160;
                r2 = dx * dx + dy * dy;
                if (r2 < 40 * 40)
                    c = rgb(200, 30, 30);
            }
            g_test[y * TW + x] = c;
        }
    return g_test;
}

/* B in the browser: one folder up (as the N64 recomps' pages); 0 at the top */
int gta2_shader_browse_up(void)
{
    if (!g_dir[0] || !strcmp(g_dir, single_pack()))
        return 0;
    char *s = strrchr(g_dir, '/');
    if (s)
        *s = 0;
    else
        g_dir[0] = 0;
    return 1;
}
