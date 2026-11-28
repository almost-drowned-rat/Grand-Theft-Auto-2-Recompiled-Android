/*
 * GTA 2 Recompiled - native d3ddll.dll (gbh_*), see docs/re/gbh_api.md.
 *
 * The original is a DirectX 6 renderer of pre-transformed D3DTLVERTEX
 * polygons: no z-buffer (painter's order), perspective-correct texturing,
 * three blend modes, 8-bit paletted textures in power-of-two cache slots,
 * CPU point lighting. This reproduces its behaviour on the winrecomp Vulkan
 * 1.1 backend, including what it writes back into the caller's vertex array
 * (rhw, colours, normalised UVs, tile UV permutation, sprite vertices) and
 * the FPU control word it sets for the whole process.
 *
 * Draws are batched: consecutive polygons with the same texture, sampler and
 * blend mode become one vkCmdDraw.
 */
#include "gta2_render.h"
#include "../gta2_options.h"
#include "../gta2_daynight.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t k_vs[] =
#include "shaders/gbh_vert.spv.h"
;
static const uint32_t k_fs[] =
#include "shaders/gbh_frag.spv.h"
;

/* ---- state ---------------------------------------------------------------------------------- */

enum { BLEND_OPAQUE, BLEND_ALPHA, BLEND_ADD, BLEND_N };

typedef struct gvert { float x, y, w, u, v; uint32_t rgba; } gvert;

typedef struct gpal { uint32_t rgba[256]; } gpal;

typedef struct gtex {
    uint32_t va;               /* guest GBH_Texture */
    uint16_t w, h;
    uint32_t pixels;           /* guest 8-bit indices, pitch 256 */
    gpal *pal;
    uint32_t size;             /* cache class edge (pow2) */
    float inv_size;
    wr_vk_texture *vk;
    int dirty;
    uint64_t used_frame;
} gtex;

#define MAX_PALS 16384
static gpal *g_pals[MAX_PALS];

/* guest GBH_Texture address -> host record: open addressing, grows at 50 %
 * load (GTA 2 registers thousands of textures per level). */
static gtex **g_tex_hash;
static uint32_t g_tex_cap, g_ntex, g_ntomb;
#define TOMB ((gtex *)1)

typedef struct light {
    uint32_t packed;
    float intensity, radius, radius2, inv_radius;
    float x, y, z;
    float r, g, b;
} light;
static light g_lights[256];
static int g_nlights;
static float g_ambient = 51.0f;
/* DAY / NIGHT (gta2_daynight.c): set per frame in gbh_SetAmbient; g_dn = the cycle changes this frame's light */
static int g_dn;
static float g_dn_amb[3];
static uint64_t g_dn_frame;   /* the frame it was set in: menus never call SetAmbient, so it must not linger */
static uint64_t g_dn_end_frame = ~0ull;   /* world_end's frame: the HUD's own SetAmbient(1.0) stays untouched */
static int g_trace = -1;   /* WR_GBH_TRACE=<frame>: every draw of that frame into the log */
void gbh_daynight_world_end(void)
{
    g_dn = 0;
    g_dn_end_frame = g_gfx.frames;
    if (g_trace > 0 && (uint32_t)g_trace == (uint32_t)g_gfx.frames)
        wr_log("trace: DAY / NIGHT world end (HUD next)");
}

static float g_win[4];             /* SetWindow reject rect: left, top, right, bottom */
static uint32_t g_globals;         /* guest GBH_Globals (0xA0 bytes) */
static uint16_t g_serial;
static uint16_t g_saved_cw;

/* Vulkan */
static VkPipelineLayout g_layout;
static VkPipeline g_pipes[BLEND_N];
static wr_vk_texture *g_white;
static int g_vk_ready;

/* current batch */
static VkDescriptorSet g_bset;
static int g_bblend = -1;
static VkDeviceSize g_boff;
static uint32_t g_bcount;
static VkCommandBuffer g_bcmd;

/* TEXTURE FILTERING override: -1 = as the game asks, 0 = nearest, 1 = linear */

/* ---- pipelines -------------------------------------------------------------------------------- */

static int pipes_init(void)
{
    if (g_vk_ready)
        return 1;
    if (!g_gfx.vk)
        return 0;
    VkPushConstantRange pc = { VK_SHADER_STAGE_VERTEX_BIT, 0, 16 };
    VkPipelineLayoutCreateInfo li = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    li.setLayoutCount = 1;
    li.pSetLayouts = &g_vk.tex_layout;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(g_vk.dev, &li, NULL, &g_layout) != VK_SUCCESS)
        return 0;
    VkShaderModule vs = wr_vk_shader(k_vs, sizeof k_vs), fs = wr_vk_shader(k_fs, sizeof k_fs);
    VkPipelineShaderStageCreateInfo st[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", NULL },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", NULL },
    };
    VkVertexInputBindingDescription vb = { 0, sizeof(gvert), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription va[4] = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 },
        { 1, 0, VK_FORMAT_R32_SFLOAT, 8 },
        { 2, 0, VK_FORMAT_R32G32_SFLOAT, 12 },
        { 3, 0, VK_FORMAT_R8G8B8A8_UNORM, 20 },
    };
    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 4;
    vi.pVertexAttributeDescriptions = va;
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    int ok = 1;
    for (int b = 0; b < BLEND_N; b++) {
        VkPipelineColorBlendAttachmentState ba = { 0 };
        ba.colorWriteMask = 0xF;
        if (b != BLEND_OPAQUE) {
            ba.blendEnable = VK_TRUE;
            ba.colorBlendOp = ba.alphaBlendOp = VK_BLEND_OP_ADD;
            if (b == BLEND_ALPHA) {
                ba.srcColorBlendFactor = ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                ba.dstColorBlendFactor = ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            } else {
                ba.srcColorBlendFactor = ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                ba.dstColorBlendFactor = ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            }
        }
        VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        cb.attachmentCount = 1;
        cb.pAttachments = &ba;
        VkGraphicsPipelineCreateInfo ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        ci.stageCount = 2;
        ci.pStages = st;
        ci.pVertexInputState = &vi;
        ci.pInputAssemblyState = &ia;
        ci.pViewportState = &vp;
        ci.pRasterizationState = &rs;
        ci.pMultisampleState = &ms;
        ci.pDepthStencilState = &ds;
        ci.pColorBlendState = &cb;
        ci.pDynamicState = &dy;
        ci.layout = g_layout;
        ci.renderPass = g_vk.target_clear;
        if (vkCreateGraphicsPipelines(g_vk.dev, VK_NULL_HANDLE, 1, &ci, NULL, &g_pipes[b]) != VK_SUCCESS)
            ok = 0;
    }
    vkDestroyShaderModule(g_vk.dev, vs, NULL);
    vkDestroyShaderModule(g_vk.dev, fs, NULL);
    uint32_t white = 0xFFFFFFFFu;
    g_white = wr_vk_texture_create(1, 1, &white, g_gfx.nearest);
    if (!ok || !g_white) {
        wr_log("gbh: pipeline creation failed");
        return 0;
    }
    g_vk_ready = 1;
    return 1;
}

extern float g_port_quad_scale, g_port_quad_tint, g_port_quad_alpha;

/* ---- batching --------------------------------------------------------------------------------- */

static void flush(void)
{
    if (!g_bcount || !g_bcmd)
        return;
    VkCommandBuffer cmd = g_bcmd;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipes[g_bblend]);
    VkViewport v = { 0, 0, (float)g_gfx.tw, (float)g_gfx.th, 0, 1 };
    VkRect2D sc = { { 0, 0 }, { g_gfx.tw, g_gfx.th } };
    vkCmdSetViewport(cmd, 0, 1, &v);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    float W = (float)g_gfx.w, H = (float)g_gfx.h;
    float xf[4] = { 2.0f / W, 2.0f / H, 1.0f / W - 1.0f, 1.0f / H - 1.0f };
    vkCmdPushConstants(cmd, g_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, xf);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0, 1, &g_bset, 0, NULL);
    VkBuffer vb = wr_vk_vertex_buffer();
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &g_boff);
    vkCmdDraw(cmd, g_bcount, 1, 0, 0);
    g_bcount = 0;
}

void gbh_flush(void)
{
    flush();
    g_bcmd = NULL;
}

static void emit(VkDescriptorSet set, int blend, const gvert *v, uint32_t n)
{
    VkCommandBuffer cmd = gfx_begin_drawing();
    if (!cmd || !pipes_init())
        return;
    if (cmd != g_bcmd || set != g_bset || blend != g_bblend) {
        flush();
        g_bcmd = cmd;
        g_bset = set;
        g_bblend = blend;
    }
    VkDeviceSize off = wr_vk_push_verts(v, n * sizeof(gvert));
    if (off == VK_WHOLE_SIZE) {
        WR_ONCE("gbh: frame vertex buffer full, polygons dropped");
        return;
    }
    if (g_bcount && off != g_boff + (VkDeviceSize)g_bcount * sizeof(gvert)) {
        flush();   /* not contiguous (alignment): start a new draw */
    }
    if (!g_bcount)
        g_boff = off;
    g_bcount += n;
}

/* ---- palettes / textures ---------------------------------------------------------------------- */

static uint32_t cache_size(uint32_t w, uint32_t h)
{
    uint32_t m = w > h ? w : h, s = 8;
    while (s < m && s < 4096)
        s <<= 1;
    return s;
}

static uint32_t tex_slot(uint32_t va) { return (va >> 5) * 2654435761u; }

static gtex *tex_find(uint32_t va)
{
    if (!g_tex_cap)
        return NULL;
    for (uint32_t i = tex_slot(va) & (g_tex_cap - 1), n = 0; n < g_tex_cap; i = (i + 1) & (g_tex_cap - 1), n++) {
        gtex *t = g_tex_hash[i];
        if (!t)
            return NULL;
        if (t != TOMB && t->va == va)
            return t;
    }
    return NULL;
}

static void tex_put(gtex **tab, uint32_t cap, gtex *t)
{
    for (uint32_t i = tex_slot(t->va) & (cap - 1);; i = (i + 1) & (cap - 1))
        if (!tab[i] || tab[i] == TOMB) {
            tab[i] = t;
            return;
        }
}

static void tex_insert(gtex *t)
{
    if ((g_ntex + g_ntomb + 1) * 2 > g_tex_cap) {
        uint32_t cap = g_tex_cap ? g_tex_cap : 4096;
        while ((g_ntex + 1) * 2 > cap / 2 * 1 + cap / 2 - cap / 4)
            cap *= 2;
        if (cap < 4096) cap = 4096;
        if ((g_ntex + 1) * 4 > cap) cap *= 2;
        gtex **tab = (gtex **)calloc(cap, sizeof *tab);
        for (uint32_t k = 0; k < g_tex_cap; k++)
            if (g_tex_hash[k] && g_tex_hash[k] != TOMB)
                tex_put(tab, cap, g_tex_hash[k]);
        free(g_tex_hash);
        g_tex_hash = tab;
        g_tex_cap = cap;
        g_ntomb = 0;
    }
    tex_put(g_tex_hash, g_tex_cap, t);
    g_ntex++;
}

static void tex_remove(gtex *t)
{
    if (!g_tex_cap)
        return;
    for (uint32_t i = tex_slot(t->va) & (g_tex_cap - 1), n = 0; n < g_tex_cap; i = (i + 1) & (g_tex_cap - 1), n++) {
        if (!g_tex_hash[i])
            return;
        if (g_tex_hash[i] == t) {
            g_tex_hash[i] = TOMB;
            g_ntex--;
            g_ntomb++;
            return;
        }
    }
}

/* Build the RGBA cache image the way d3ddll uploads it (docs/re/gbh_api.md
 * 2.4): texels at (0,0), column w := column w-1, row h := row h-1, last row :=
 * row 0; alpha = index != 0 (the alpha variant; opaque draws ignore it). */
static uint32_t *g_rgba;
static size_t g_rgba_cap;

static void tex_upload(gtex *t)
{
    uint32_t s = t->size, w = t->w, h = t->h;
    size_t need = (size_t)s * s;
    if (need > g_rgba_cap) {
        free(g_rgba);
        g_rgba = (uint32_t *)malloc(need * 4);
        g_rgba_cap = need;
    }
    memset(g_rgba, 0, need * 4);
    static const gpal black;
    const gpal *p = t->pal ? t->pal : &black;
    if (w > s) w = s;
    if (h > s) h = s;
    if (!wr_mem_valid(t->pixels, (h ? h - 1 : 0) * 256u + w)) {
        WR_ONCE("gbh: texture pixels outside guest memory");
        return;
    }
    const uint8_t *src = (const uint8_t *)wr_ptr(t->pixels);
    for (uint32_t y = 0; y < h; y++) {
        uint32_t *row = g_rgba + (size_t)y * s;
        const uint8_t *sr = src + (size_t)y * 256u;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t i = sr[x];
            row[x] = p->rgba[i] | (i ? 0xFF000000u : 0u);
        }
        if (w < s && w)
            row[w] = row[w - 1];
    }
    if (h < s && h) {
        memcpy(g_rgba + (size_t)h * s, g_rgba + (size_t)(h - 1) * s, (size_t)s * 4);
        memcpy(g_rgba + (size_t)(s - 1) * s, g_rgba, (size_t)s * 4);
    }
    /* A texture already drawn this frame gets a fresh image: uploads run
     * before the frame's draws, so updating in place would change the
     * earlier draws too. */
    if (t->vk && t->used_frame == g_gfx.frames) {
        wr_vk_texture_destroy(t->vk);
        t->vk = NULL;
    }
    if (!t->vk)
        t->vk = wr_vk_texture_create(s, s, g_rgba, g_gfx.linear);
    else
        wr_vk_texture_update(t->vk, 0, 0, s, s, g_rgba, s);
    t->dirty = 0;
}

/* The game edits its GBH_Texture directly (the rolling HUD digits change
 * height and pixel pointer per frame), and d3ddll reads the struct at each
 * upload: so do we. A change of size or source is a new upload. */
static void tex_sync(gtex *t)
{
    uint32_t va = t->va;
    uint16_t w = wr_rd16(va + 0x0E), h = wr_rd16(va + 0x10);
    uint32_t px = wr_rd32(va + 0x14) + ((uint32_t)wr_rd8(va + 0x0D) << 8) + wr_rd8(va + 0x0C);
    if (w != t->w || h != t->h || px != t->pixels) {
        t->w = w;
        t->h = h;
        t->pixels = px;
        uint32_t size = cache_size(w, h);
        if (size != t->size && t->vk) {
            wr_vk_texture_destroy(t->vk);
            t->vk = NULL;
        }
        t->size = size;
        t->inv_size = 1.0f / (float)size;
        t->dirty = 1;
    }
}

static VkDescriptorSet tex_set(gtex *t, int nearest)
{
    if (t->dirty || !t->vk)
        tex_upload(t);
    if (!t->vk)
        return g_white->set;
    t->used_frame = g_gfx.frames;
    int fo = opt_texture_filter();   /* TEXTURE FILTERING: -1 the game's choice */
    if (fo >= 0)
        nearest = !fo;
    return nearest ? wr_vk_texture_alt_set(t->vk, g_gfx.nearest) : t->vk->set;
}

/* ---- the draw core (Triangle / Quad / Tile) -------------------------------------------------- */

static inline float rdf(uint32_t va) { float f; uint32_t u = wr_rd32(va); memcpy(&f, &u, 4); return f; }
static inline void wrf(uint32_t va, float f) { uint32_t u; memcpy(&u, &f, 4); wr_wr32(va, u); }

static void apply_lights(uint32_t verts, int n, uint32_t shade)
{
    float k = (float)(shade & 0xFF) / 255.0f;
    for (int i = 0; i < n; i++) {
        uint32_t v = verts + 0x20u * (uint32_t)i;
        uint32_t wp = verts + 0x80u + 0x20u * (uint32_t)i;
        float px = rdf(wp), py = rdf(wp + 4), pz = rdf(wp + 8);
        float R = 0, G = 0, B = 0;
        for (int l = 0; l < g_nlights; l++) {
            const light *L = &g_lights[l];
            if ((L->packed & 0x30000u) != 0x10000u)
                continue;
            float dx = px - L->x, dy = py - L->y, dz = pz - L->z;
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > L->radius2)
                continue;
            float f = (L->radius - sqrtf(d2)) * L->inv_radius;
            if (f <= 0)
                continue;
            f *= L->intensity;
            R += L->r * f;
            G += L->g * f;
            B += L->b * f;
        }
        uint32_t c = wr_rd32(v + 0x10);
        float r = (float)((c >> 16) & 255) * k * R + g_ambient;
        float g = (float)((c >> 8) & 255) * k * G + g_ambient;
        float b = (float)(c & 255) * k * B + g_ambient;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        /* fistp in the DLL's round-down mode */
        uint32_t ir = (uint32_t)floorf(r), ig = (uint32_t)floorf(g), ib = (uint32_t)floorf(b);
        wr_wr32(v + 0x10, (c & 0xFF000000u) | ir << 16 | ig << 8 | ib);
    }
}

/* DAY / NIGHT: apply_lights with the ambient as a colour (r, g, b) - the original formula, so the day (the
 * game's own ambient, untinted) is the original picture to the bit. */
static void apply_lights_dn(uint32_t verts, int n, uint32_t shade)
{
    float k = (float)(shade & 0xFF) / 255.0f;
    for (int i = 0; i < n; i++) {
        uint32_t v = verts + 0x20u * (uint32_t)i;
        uint32_t wp = verts + 0x80u + 0x20u * (uint32_t)i;
        float px = rdf(wp), py = rdf(wp + 4), pz = rdf(wp + 8);
        float R = 0, G = 0, B = 0;
        for (int l = 0; l < g_nlights; l++) {
            const light *L = &g_lights[l];
            if ((L->packed & 0x30000u) != 0x10000u)
                continue;
            float dx = px - L->x, dy = py - L->y, dz = pz - L->z;
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > L->radius2)
                continue;
            float f = (L->radius - sqrtf(d2)) * L->inv_radius;
            if (f <= 0)
                continue;
            f *= L->intensity;
            R += L->r * f;
            G += L->g * f;
            B += L->b * f;
        }
        uint32_t c = wr_rd32(v + 0x10);
        float r = (float)((c >> 16) & 255) * k * R + g_dn_amb[0];
        float g = (float)((c >> 8) & 255) * k * G + g_dn_amb[1];
        float b = (float)(c & 255) * k * B + g_dn_amb[2];
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        /* fistp in the DLL's round-down mode, as apply_lights */
        uint32_t ir = (uint32_t)floorf(r), ig = (uint32_t)floorf(g), ib = (uint32_t)floorf(b);
        wr_wr32(v + 0x10, (c & 0xFF000000u) | ir << 16 | ig << 8 | ib);
    }
}

static void trace_draw(const char *what, uint32_t flags, uint32_t texva, uint32_t verts, int n)
{
    gtex *t = texva ? tex_find(texva) : NULL;
    char b[512];
    size_t o = (size_t)snprintf(b, sizeof b, "%s f=%05X tex=%ux%u dn=%d", what, flags, t ? t->w : 0, t ? t->h : 0,
                                g_dn && g_dn_frame == g_gfx.frames);
    for (int i = 0; i < n && o < sizeof b; i++) {
        uint32_t v = verts + 0x20u * (uint32_t)i;
        float x, y, z, u, vv;
        uint32_t a = wr_rd32(v), bb = wr_rd32(v + 4), c = wr_rd32(v + 8), d = wr_rd32(v + 0x18), e = wr_rd32(v + 0x1C);
        memcpy(&x, &a, 4); memcpy(&y, &bb, 4); memcpy(&z, &c, 4); memcpy(&u, &d, 4); memcpy(&vv, &e, 4);
        o += (size_t)snprintf(b + o, sizeof b - o, " (%.1f,%.1f z%.3f uv %.2f,%.2f)", x, y, z, u, vv);
    }
    wr_log("%s", b);
}

static void draw_core(uint32_t flags, uint32_t texva, uint32_t verts, uint32_t shade, int n, int quad)
{
    if (g_trace < 0) {
        const char *e = getenv("WR_GBH_TRACE");
        g_trace = e && *e ? atoi(e) : 0;
    }
    if (g_trace && (uint32_t)g_trace == (uint32_t)g_gfx.frames && wr_mem_valid(verts, 0x80))
        trace_draw(quad ? "quad" : "tri", flags, texva, verts, n);
    if (g_trace && (uint32_t)g_trace == (uint32_t)g_gfx.frames && (flags & 0x10000u) && rdf(verts) > 420.0f &&
        rdf(verts + 4) < 60.0f) {
        char b[256];
        size_t o = 0;
        for (uint32_t k = 0; k < 64 && o < sizeof b - 12; k++) {
            uint32_t w = MEM32(g_wr_main_ctx->r_esp + 4 * k);
            if (w >= 0x401000 && w < 0x56E000)
                o += (size_t)snprintf(b + o, sizeof b - o, " %08X", w);
        }
        wr_log("HUD sprite at %.1f,%.1f, guest stack code words:%s", rdf(verts), rdf(verts + 4), b);
    }
    if (!wr_mem_valid(verts, 0x20u * (uint32_t)n))
        return;
    if (rdf(verts + 8) <= 0.0f)
        return;
    float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
    for (int i = 0; i < n; i++) {
        float x = rdf(verts + 0x20u * (uint32_t)i), y = rdf(verts + 0x20u * (uint32_t)i + 4);
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
    }
    /* quad: sprite vertices are generated below; the original tests the
     * caller's array as it is (v1..v3 may be stale) - same here */
    if (minx > g_win[2] || maxx < g_win[0] || miny > g_win[3] || maxy < g_win[1])
        return;
    if (quad && (flags & 0x10000u))
        flags |= 0x20000u;
    int blend = !(flags & 0x380u) ? BLEND_OPAQUE : (flags & 0x200u) ? BLEND_ADD : BLEND_ALPHA;
    int nearest = (flags & 0x20000u) != 0;

    gtex *t = texva ? tex_find(texva) : NULL;
    if (t)
        tex_sync(t);
    float inv = t ? t->inv_size : 1.0f;

    if (quad && (flags & 0x10000u) && t) {
        float x0 = rdf(verts), y0 = rdf(verts + 4), z0 = rdf(verts + 8);
        float w = (float)t->w, h = (float)t->h, e1 = 0.001f, e2 = 0.001f;
        float px[4] = { x0, x0 + w - e1, x0 + w - e1, x0 };
        float py[4] = { y0, y0, y0 + h - e1, y0 + h - e1 };
        float pu[4] = { 0, w - e2, w - e2, 0 };
        float pv[4] = { 0, 0, h - e2, h - e2 };
        for (int i = 0; i < 4; i++) {
            uint32_t v = verts + 0x20u * (uint32_t)i;
            if (i) {
                wrf(v, px[i]);
                wrf(v + 4, py[i]);
                wrf(v + 8, z0);
            }
            wrf(v + 0x18, pu[i]);
            wrf(v + 0x1C, pv[i]);
        }
    }
    for (int i = 0; i < n; i++) {
        uint32_t v = verts + 0x20u * (uint32_t)i;
        wr_wr32(v + 0x0C, wr_rd32(v + 0x08));          /* rhw = z */
        wrf(v + 0x18, rdf(v + 0x18) * inv);
        wrf(v + 0x1C, rdf(v + 0x1C) * inv);
        if (!(flags & 0x2000u)) {
            uint32_t s = shade & 0xFF;
            wr_wr32(v + 0x10, 0xFF000000u | s << 16 | s << 8 | s);
        }
        wr_wr32(v + 0x14, 0);
    }
    if (flags & 0x8000u) {
        if (g_dn && g_dn_frame == g_gfx.frames)
            apply_lights_dn(verts, n, shade);
        else if (g_ambient != 255.0f)
            apply_lights(verts, n, shade);
    }

    gvert gv[6];
    gvert q[4];
    for (int i = 0; i < n; i++) {
        uint32_t v = verts + 0x20u * (uint32_t)i;
        float rhw = rdf(v + 0x0C);
        uint32_t c = wr_rd32(v + 0x10);
        q[i].x = rdf(v);
        q[i].y = rdf(v + 4);
        q[i].w = rhw > 0 ? 1.0f / rhw : 1.0f;
        q[i].u = rdf(v + 0x18);
        q[i].v = rdf(v + 0x1C);
        q[i].rgba = (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16);   /* ARGB -> RGBA bytes */
    }
    if (g_port_quad_tint != 1.0f || g_port_quad_alpha != 1.0f) {   /* MORE BLOOD: drying pools */
        for (int i = 0; i < n; i++) {
            uint32_t c = q[i].rgba;
            uint32_t r = (uint32_t)((float)(c & 0xFF) * g_port_quad_tint);
            uint32_t g = (uint32_t)((float)(c >> 8 & 0xFF) * g_port_quad_tint);
            uint32_t b = (uint32_t)((float)(c >> 16 & 0xFF) * g_port_quad_tint);
            uint32_t a = (uint32_t)((float)(c >> 24) * g_port_quad_alpha);
            q[i].rgba = r | g << 8 | b << 16 | a << 24;
        }
        if (g_port_quad_alpha < 1.0f && blend == BLEND_OPAQUE)
            blend = BLEND_ALPHA;
    }
    if (g_port_quad_scale != 1.0f && n == 4) {   /* the port draws this sprite bigger (MORE BLOOD) */
        float cx = (q[0].x + q[1].x + q[2].x + q[3].x) * 0.25f, cy = (q[0].y + q[1].y + q[2].y + q[3].y) * 0.25f;
        for (int i = 0; i < 4; i++) {
            q[i].x = cx + (q[i].x - cx) * g_port_quad_scale;
            q[i].y = cy + (q[i].y - cy) * g_port_quad_scale;
        }
    }
    uint32_t nv;
    if (n == 3) {
        gv[0] = q[0]; gv[1] = q[1]; gv[2] = q[2];
        nv = 3;
    } else {   /* fan v0 v1 v2 v3 */
        gv[0] = q[0]; gv[1] = q[1]; gv[2] = q[2];
        gv[3] = q[0]; gv[4] = q[2]; gv[5] = q[3];
        nv = 6;
    }
    if (g_globals)
        wr_wr32(g_globals, wr_rd32(g_globals) + (n == 3 ? 1u : 2u));
    /* everything the game can see is done; no GPU (headless): no drawing */
    if (!pipes_init())
        return;
    VkDescriptorSet set = t ? tex_set(t, nearest) : g_white->set;
    emit(set, blend, gv, nv);
}

static void swap_uv(uint32_t a, uint32_t b)
{
    uint32_t u = wr_rd32(a + 0x18), v = wr_rd32(a + 0x1C);
    wr_wr32(a + 0x18, wr_rd32(b + 0x18));
    wr_wr32(a + 0x1C, wr_rd32(b + 0x1C));
    wr_wr32(b + 0x18, u);
    wr_wr32(b + 0x1C, v);
}

static void tile_uvs(uint32_t *pflags, uint32_t verts)
{
    uint32_t flags = *pflags;
    uint32_t v0 = verts, v1 = verts + 0x20, v2 = verts + 0x40, v3 = verts + 0x60;
    if (!(flags & 0x4000u)) {
        const float d[4][2] = { { 0.5f, 0.5f }, { 63.4999f, 0.5f }, { 63.4999f, 63.4999f }, { 0.5f, 63.4999f } };
        for (int i = 0; i < 4; i++) {
            wrf(verts + 0x20u * (uint32_t)i + 0x18, d[i][0]);
            wrf(verts + 0x20u * (uint32_t)i + 0x1C, d[i][1]);
        }
    }
    uint32_t rot = flags & 0x60u;
    if (rot == 0x20u) {          /* v0<-v3, v3<-v2, v2<-v1, v1<-v0_old */
        swap_uv(v0, v3); swap_uv(v3, v2); swap_uv(v2, v1);
    } else if (rot == 0x60u) {   /* v0<-v1, v1<-v2, v2<-v3, v3<-v0_old */
        swap_uv(v0, v1); swap_uv(v1, v2); swap_uv(v2, v3);
    } else if (rot == 0x40u) {
        flags ^= 0x18u;
    }
    if (flags & 0x08u) { swap_uv(v0, v1); swap_uv(v3, v2); }
    if (flags & 0x10u) { swap_uv(v0, v3); swap_uv(v1, v2); }
    *pflags = flags;
}

/* ---- exports: drawing -------------------------------------------------------------------------- */

WR_HANDLER(gbh_DrawTriangle)
{
    (void)base;
    draw_core(WR_ARG(0), WR_ARG(1), WR_ARG(2), WR_ARG(3), 3, 0);
    WR_RET(0, 4);
}

WR_HANDLER(gbh_DrawQuad)
{
    (void)base;
    draw_core(WR_ARG(0), WR_ARG(1), WR_ARG(2), WR_ARG(3), 4, 1);
    WR_RET(0, 4);
}

WR_HANDLER(gbh_DrawTile)
{
    (void)base;
    uint32_t flags = WR_ARG(0), verts = WR_ARG(2);
    if (wr_mem_valid(verts, 0x80))
        tile_uvs(&flags, verts);
    draw_core(flags, WR_ARG(1), verts, WR_ARG(3), 4, 1);
    WR_RET(0, 4);
}

WR_HANDLER(gbh_DrawQuadClipped)
{
    (void)base;
    WR_ONCE("gbh_DrawQuadClipped: drawn unclipped (gta2 never calls it)");
    draw_core(WR_ARG(0), WR_ARG(1), WR_ARG(2), WR_ARG(3), 4, 0);
    WR_RET(0, 5);
}

WR_HANDLER(gbh_DrawFlatRect)
{
    (void)base;
    uint32_t verts = WR_ARG(0), argb = WR_ARG(1);
    if (!wr_mem_valid(verts, 0x80) || !pipes_init())
        WR_RET(0, 2);
    gvert q[4], gv[6];
    uint32_t rgba = (argb & 0x0000FF00u) | ((argb >> 16) & 0xFFu) | ((argb & 0xFFu) << 16) | 0xFF000000u;
    for (int i = 0; i < 4; i++) {
        uint32_t v = verts + 0x20u * (uint32_t)i;
        float rhw = rdf(v + 8);
        q[i] = (gvert){ rdf(v), rdf(v + 4), rhw > 0 ? 1.0f / rhw : 1.0f, 0, 0, rgba };
    }
    gv[0] = q[0]; gv[1] = q[1]; gv[2] = q[2]; gv[3] = q[0]; gv[4] = q[2]; gv[5] = q[3];
    emit(g_white->set, BLEND_OPAQUE, gv, 6);
    WR_RET(0, 2);
}

/* For the port's menus over the game: a see-through rectangle in game
 * screen pixels (rgba with alpha), drawn like gbh_DrawFlatRect but blended. */
float g_port_quad_scale = 1.0f;   /* quads drawn meanwhile are scaled about their centre */
float g_port_quad_tint = 1.0f, g_port_quad_alpha = 1.0f;   /* ... and darkened / see-through */

void gbh_port_rect(float x, float y, float w, float h, uint32_t rgba)
{
    if (!pipes_init())
        return;
    gvert q[4] = { { x, y, 1, 0, 0, rgba }, { x + w, y, 1, 0, 0, rgba }, { x + w, y + h, 1, 0, 0, rgba },
                   { x, y + h, 1, 0, 0, rgba } };
    gvert gv[6] = { q[0], q[1], q[2], q[0], q[2], q[3] };
    emit(g_white->set, BLEND_ALPHA, gv, 6);
}

/* ---- the port's glyphs (translations, src/gta2_text.c) ----
 * An RGBA atlas made once; each glyph is a quad of it, modulated by a colour, alpha blended (or
 * additive like the game's blend 2), in the same draw stream as the game's own text. */
void *gbh_port_atlas_create(const uint32_t *rgba, uint32_t w, uint32_t h)
{
    if (!pipes_init())
        return NULL;
    return wr_vk_texture_create(w, h, rgba, g_gfx.linear);
}

void gbh_port_atlas_quad(void *atlas, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                         uint32_t rgba, int additive)
{
    wr_vk_texture *t = atlas;
    if (!t || !pipes_init())
        return;
    gvert q[4] = { { x, y, 1, u0, v0, rgba }, { x + w, y, 1, u1, v0, rgba }, { x + w, y + h, 1, u1, v1, rgba },
                   { x, y + h, 1, u0, v1, rgba } };
    gvert gv[6] = { q[0], q[1], q[2], q[0], q[2], q[3] };
    emit(t->set, additive ? BLEND_ADD : BLEND_ALPHA, gv, 6);
}

/* The colours of a guest texture (a font glyph with its palette): the fill = the most used opaque
 * colour, the edge = the darkest colour used at least a tenth as often. 0 = not known yet. */
int gbh_port_tex_colours(uint32_t va, uint32_t *fill, uint32_t *edge)
{
    gtex *t = tex_find(va);
    if (!t || !t->pal || !t->pixels || !wr_mem_valid(t->pixels, (uint32_t)(t->h ? t->h - 1 : 0) * 256u + t->w))
        return 0;
    uint32_t n[256] = { 0 };
    for (uint32_t y = 0; y < t->h; y++)
        for (uint32_t x = 0; x < t->w; x++)
            n[wr_rd8(t->pixels + y * 256u + x)]++;
    int best = -1;
    for (int i = 1; i < 256; i++)
        if (n[i] && (best < 0 || n[i] > n[best]))
            best = i;
    if (best < 0)
        return 0;
    int dark = best;
    uint32_t lum_d = 0xFFFFFFFFu;
    for (int i = 1; i < 256; i++) {
        if (n[i] * 10u < n[best])
            continue;
        uint32_t c = t->pal->rgba[i];
        uint32_t lum = (c & 0xFF) * 3u + (c >> 8 & 0xFF) * 6u + (c >> 16 & 0xFF);
        if (lum < lum_d) {
            lum_d = lum;
            dark = i;
        }
    }
    *fill = t->pal->rgba[best] | 0xFF000000u;
    *edge = t->pal->rgba[dark] | 0xFF000000u;
    return 1;
}

/* A picture of the port's own (RGBA, made once per pixel buffer) over the
 * given game screen rectangle - the SHADERS page's test picture. */
void gbh_port_image(const uint32_t *rgba, uint32_t iw, uint32_t ih, float x, float y, float w, float h)
{
    static const uint32_t *src;
    static wr_vk_texture *tex;
    if (!pipes_init() || !rgba)
        return;
    if (src != rgba || !tex) {
        if (tex)
            wr_vk_texture_destroy(tex);
        tex = wr_vk_texture_create(iw, ih, rgba, g_gfx.nearest);
        src = rgba;
        if (!tex)
            return;
    }
    uint32_t c = 0xFFFFFFFFu;
    gvert q[4] = { { x, y, 1, 0, 0, c }, { x + w, y, 1, 1, 0, c }, { x + w, y + h, 1, 1, 1, c }, { x, y + h, 1, 0, 1, c } };
    gvert gv[6] = { q[0], q[1], q[2], q[0], q[2], q[3] };
    emit(tex->set, BLEND_OPAQUE, gv, 6);
}

/* ---- exports: textures / palettes --------------------------------------------------------------- */

WR_HANDLER(gbh_RegisterTexture)
{
    (void)base;
    uint32_t w = WR_ARG(0) & 0xFFFFu, h = WR_ARG(1) & 0xFFFFu, pixels = WR_ARG(2), pal = WR_ARG(3);
    uint32_t va = wr_heap_alloc(0x20, 1);
    if (!va)
        WR_RET(0, 5);
    wr_wr16(va + 0x00, g_serial++);
    wr_wr16(va + 0x0E, (uint16_t)w);
    wr_wr16(va + 0x10, (uint16_t)h);
    wr_wr8(va + 0x12, 1);
    wr_wr32(va + 0x14, pixels);
    gtex *t = (gtex *)calloc(1, sizeof *t);
    t->va = va;
    t->w = (uint16_t)w;
    t->h = (uint16_t)h;
    t->pixels = pixels;
    t->pal = pal < MAX_PALS ? g_pals[pal] : NULL;
    t->size = cache_size(w, h);
    t->inv_size = 1.0f / (float)t->size;
    t->dirty = 1;
    tex_insert(t);
    WR_RET(va, 5);
}

WR_HANDLER(gbh_FreeTexture)
{
    (void)base;
    gtex *t = tex_find(WR_ARG(0));
    if (t) {
        tex_remove(t);
        if (t->vk)
            wr_vk_texture_destroy(t->vk);
        free(t);
        wr_heap_free(WR_ARG(0));
    }
    WR_RET(0, 1);
}

WR_HANDLER(gbh_LockTexture)
{
    (void)base;
    uint32_t va = WR_ARG(0);
    gtex *t = tex_find(va);
    if (t) {
        wr_wr8(va + 0x13, wr_rd8(va + 0x13) | 1u);
        wr_wr32(va + 0x08, t->pixels);
        wr_wr16(va + 0x06, 0x100);
        t->dirty = 1;
    }
    WR_RET(0, 1);
}

WR_HANDLER(gbh_UnlockTexture)
{
    (void)base;
    uint32_t va = WR_ARG(0);
    gtex *t = tex_find(va);
    if (t) {
        wr_wr8(va + 0x13, wr_rd8(va + 0x13) & ~1u);
        wr_wr32(va + 0x08, 0);
        wr_wr16(va + 0x06, 0);
        t->dirty = 1;
    }
    WR_RET(0, 1);
}

WR_HANDLER(gbh_RegisterPalette)
{
    (void)base;
    uint32_t id = WR_ARG(0), p = WR_ARG(1);
    if (id >= MAX_PALS || !wr_mem_valid(p, 255u * 256u + 4u)) {
        wr_log("gbh_RegisterPalette(%u, 0x%08X): out of range", id, p);
        WR_RET(0, 2);
    }
    /* d3ddll writes into the caller's palette: entry 0 := 0, other zero
     * entries := 0x00010000 (so black is not the colour key) */
    wr_wr32(p, 0);
    for (uint32_t i = 1; i < 256; i++)
        if (!wr_rd32(p + i * 256u))
            wr_wr32(p + i * 256u, 0x00010000u);
    /* a new palette object: textures registered with the old one keep it,
     * as the original's textures keep the old converted table */
    gpal *g = (gpal *)malloc(sizeof *g);
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t e = wr_rd32(p + i * 256u);
        g->rgba[i] = ((e >> 16) & 0xFFu) | (e & 0xFF00u) | ((e & 0xFFu) << 16);
    }
    g_pals[id] = g;
    WR_RET(0, 2);
}

WR_HANDLER(gbh_FreePalette)
{
    (void)base;
    uint32_t id = WR_ARG(0);
    /* kept alive: textures may still point at it (the original dangles) */
    if (id < MAX_PALS)
        g_pals[id] = NULL;
    WR_RET(0, 1);
}

WR_HANDLER(gbh_AssignPalette)
{
    (void)base;
    gtex *t = tex_find(WR_ARG(0));
    uint32_t id = WR_ARG(1);
    if (t && id < MAX_PALS) {
        t->pal = g_pals[id];
        t->dirty = 1;
    }
    WR_RET(0, 2);
}

/* ---- exports: image table (frontend backgrounds) ------------------------------------------------ */

typedef struct gimg { int used; uint32_t w, h; wr_vk_texture *vk; } gimg;
static gimg *g_imgs;
static int g_nimgs;

WR_HANDLER(gbh_InitImageTable)
{
    (void)base;
    int n = (int)WR_ARG(0);
    free(g_imgs);
    g_imgs = (gimg *)calloc((size_t)(n > 0 ? n : 1), sizeof *g_imgs);
    g_nimgs = n > 0 ? n : 0;
    WR_RET(g_imgs ? 0u : 0xFFFFFFFFu, 1);
}

WR_HANDLER(gbh_FreeImageTable)
{
    (void)base;
    for (int i = 0; i < g_nimgs; i++)
        if (g_imgs[i].vk)
            wr_vk_texture_destroy(g_imgs[i].vk);
    free(g_imgs);
    g_imgs = NULL;
    g_nimgs = 0;
    WR_RET(0, 0);
}

WR_HANDLER(gbh_LoadImage)
{
    (void)base;
    uint32_t tga = WR_ARG(0);
    if (!tga || !wr_mem_valid(tga, 18))
        WR_RET(0xFFFFFFFEu, 1);
    const uint8_t *h = (const uint8_t *)wr_ptr(tga);
    if (h[1] != 0 || h[2] != 2 || h[16] != 16)
        WR_RET(0xFFFFFFFEu, 1);
    uint32_t w = (uint32_t)(h[12] | h[13] << 8), ht = (uint32_t)(h[14] | h[15] << 8);
    uint32_t px = tga + 18u + h[0];
    if (!wr_mem_valid(px, w * ht * 2u))
        WR_RET(0xFFFFFFFEu, 1);
    int slot = -1;
    for (int i = 0; i < g_nimgs; i++)
        if (!g_imgs[i].used) {
            slot = i;
            break;
        }
    if (slot < 0)
        WR_RET(0xFFFFFFFFu, 1);
    uint32_t *rgba = (uint32_t *)malloc((size_t)w * ht * 4);
    const uint8_t *src = (const uint8_t *)wr_ptr(px);
    for (uint32_t y = 0; y < ht; y++) {
        const uint8_t *row = src + (size_t)(ht - 1 - y) * w * 2;   /* bottom-up */
        for (uint32_t x = 0; x < w; x++) {
            uint32_t c = (uint32_t)(row[2 * x] | row[2 * x + 1] << 8);
            uint32_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
            r = r << 3 | r >> 2;
            g = g << 3 | g >> 2;
            b = b << 3 | b >> 2;
            rgba[(size_t)y * w + x] = 0xFF000000u | b << 16 | g << 8 | r;
        }
    }
    gimg *im = &g_imgs[slot];
    im->used = 1;
    im->w = w;
    im->h = ht;
    if (pipes_init())
        im->vk = wr_vk_texture_create(w, ht, rgba, g_gfx.nearest);
    free(rgba);
    WR_RET((uint32_t)slot, 1);
}

WR_HANDLER(gbh_BlitImage)
{
    (void)base;
    int img = (int)WR_ARG(0);
    int sl = (int)WR_ARG(1), st = (int)WR_ARG(2), sr = (int)WR_ARG(3), sb = (int)WR_ARG(4);
    int dx = (int)WR_ARG(5), dy = (int)WR_ARG(6);
    if (img < 0 || img >= g_nimgs || !g_imgs[img].used)
        WR_RET(0xFFFFFFFFu, 7);
    gimg *im = &g_imgs[img];
    if (sl < 0 || st < 0 || sr < 0 || sb < 0 || sl > (int)im->w || sr > (int)im->w || st > (int)im->h || sb > (int)im->h)
        WR_RET(0xFFFFFFFEu, 7);
    if (dx < 0 || dy < 0 || dx + (sr - sl) > (int)g_gfx.w || dy + (sb - st) > (int)g_gfx.h)
        WR_RET(0xFFFFFFFDu, 7);
    if (!im->vk || !pipes_init())
        WR_RET(0, 7);
    float x0 = (float)dx - 0.5f, y0 = (float)dy - 0.5f;   /* pixel edges: centres are at integers */
    float x1 = x0 + (float)(sr - sl), y1 = y0 + (float)(sb - st);
    float u0 = (float)sl / im->w, v0 = (float)st / im->h, u1 = (float)sr / im->w, v1 = (float)sb / im->h;
    gvert gv[6] = {
        { x0, y0, 1, u0, v0, 0xFFFFFFFFu }, { x1, y0, 1, u1, v0, 0xFFFFFFFFu }, { x1, y1, 1, u1, v1, 0xFFFFFFFFu },
        { x0, y0, 1, u0, v0, 0xFFFFFFFFu }, { x1, y1, 1, u1, v1, 0xFFFFFFFFu }, { x0, y1, 1, u0, v1, 0xFFFFFFFFu },
    };
    emit(im->vk->set, BLEND_OPAQUE, gv, 6);
    WR_RET(0, 7);
}

WR_HANDLER(gbh_BlitBuffer) { (void)base; WR_RET(0, 6); }
WR_HANDLER(gbh_PrintBitmap) { (void)base; WR_ONCE("gbh_PrintBitmap called (not used by gta2)"); WR_RET(0, 1); }

/* ---- exports: frame / state --------------------------------------------------------------------- */

WR_HANDLER(gbh_InitDLL)
{
    (void)base;
    /* d3ddll: FPU control word := (cw & ~0xF00) | 0x400 - 24-bit precision,
     * round toward -inf - for the rest of the process (gta2's own float
     * code runs on it). */
    g_saved_cw = ctx->fp_control_word;
    ctx->fp_control_word = (uint16_t)((ctx->fp_control_word & ~0xF00u) | 0x400u);
    if (!g_globals)
        g_globals = wr_galloc(0xA0);
    for (int i = 0; i < 12; i++) {
        static const uint32_t base_slots[12] = { 10, 62, 62, 88, 1, 0, 110, 126, 126, 40, 10, 0 };
        wr_wr32(g_globals + 0x10 + 4u * (uint32_t)i, 2u * base_slots[i]);
        wr_wr32(g_globals + 0x40 + 4u * (uint32_t)i, 2u * base_slots[i]);
    }
    wr_log("gbh_InitDLL(state 0x%08X): FPU control word 0x%04X -> 0x%04X", WR_ARG(0), g_saved_cw,
           ctx->fp_control_word);
    WR_RET(0, 1);
}

WR_HANDLER(gbh_Init)
{
    (void)base;
    pipes_init();
    WR_RET(0, 1);
}

WR_HANDLER(gbh_CloseDLL)
{
    (void)base;
    gbh_flush();
    ctx->fp_control_word = g_saved_cw;
    WR_RET(0, 0);
}

WR_HANDLER(gbh_CloseScreen)
{
    (void)base;
    gbh_flush();
    wr_wr32(WR_ARG(0) + VS_MODE, 0);
    WR_RET(0, 1);
}

WR_HANDLER(gbh_GetGlobals)
{
    (void)base;
    if (!g_globals)
        g_globals = wr_galloc(0xA0);
    WR_RET(g_globals, 0);
}

WR_HANDLER(gbh_GetUsedCache) { (void)base; WR_RET(0, 1); }

WR_HANDLER(gbh_SetWindow)
{
    (void)base;
    for (int i = 0; i < 4; i++)
        g_win[i] = rdf(g_esp + 4u + 4u * (uint32_t)i);
    WR_RET(0, 4);
}

WR_HANDLER(gbh_SetCamera) { (void)base; WR_RET(0, 4); }
WR_HANDLER(gbh_SetColourDepth) { (void)base; WR_RET(0, 0); }

WR_HANDLER(gbh_BeginScene)
{
    (void)base;
    if (g_globals) {
        wr_wr32(g_globals + 0, 0);
        wr_wr32(g_globals + 4, 0);
        wr_wr32(g_globals + 8, 0);
    }
    gfx_begin_drawing();
    WR_RET(0, 0);
}

WR_HANDLER(gbh_EndScene) { (void)base; WR_RET(0, 0); }
WR_HANDLER(gbh_BeginLevel) { (void)base; WR_RET(0, 0); }
WR_HANDLER(gbh_EndLevel) { (void)base; WR_RET(0, 0); }

WR_HANDLER(gbh_ResetLights) { (void)base; g_nlights = 0; WR_RET(0, 0); }

WR_HANDLER(gbh_SetAmbient)
{
    (void)base;
    g_ambient = rdf(g_esp + 4) * 255.0f;
    /* the world's ambient (0x00472132) only: the HUD sets its own 1.0 after the world (0x004C6DAE, sub_004C6D90) */
    g_dn = g_dn_end_frame != g_gfx.frames ? gta2_daynight_ambient(g_ambient, g_dn_amb) : 0;
    g_dn_frame = g_gfx.frames;
    if (g_trace > 0 && (uint32_t)g_trace == (uint32_t)g_gfx.frames)
        wr_log("trace: SetAmbient %.1f -> day / night %d (%.0f %.0f %.0f)", g_ambient, g_dn, g_dn_amb[0],
               g_dn_amb[1], g_dn_amb[2]);
    WR_RET(0, 1);
}

WR_HANDLER(gbh_AddLight)
{
    (void)base;
    uint32_t p = WR_ARG(0);
    if (g_nlights < 256 && wr_mem_valid(p, 0x14)) {
        light *L = &g_lights[g_nlights++];
        L->packed = wr_rd32(p);
        L->intensity = (float)(L->packed & 0xFF) / 255.0f;
        L->radius = (float)((L->packed >> 8) & 0xFF) / 255.0f * 8.0f;
        L->radius2 = L->radius * L->radius;
        L->inv_radius = L->radius > 0 ? 1.0f / L->radius : 0.0f;
        L->x = rdf(p + 4);
        L->y = rdf(p + 8);
        L->z = rdf(p + 12);
        uint32_t c = wr_rd32(p + 16);
        L->r = (float)((c >> 16) & 255) / 255.0f;
        L->g = (float)((c >> 8) & 255) / 255.0f;
        L->b = (float)(c & 255) / 255.0f;
    } else if (g_nlights >= 256) {
        WR_ONCE("gbh_AddLight: more than 256 lights");
    }
    WR_RET(0, 1);
}

/* unused by gta2 or no-ops in the original */
WR_HANDLER(gbh_Plot) { (void)base; WR_RET(0, 4); }
WR_HANDLER(DrawLine) { (void)base; WR_RET(0, 5); }
WR_HANDLER(SetShadeTableA) { (void)base; WR_RET(0, 5); }
WR_HANDLER(ConvertColourBank) { (void)base; WR_RET(0, 1); }
WR_HANDLER(MakeScreenTable) { (void)base; WR_RET(0, 3); }

WR_HANDLER(gbh_ConvertColour)
{
    (void)base;
    uint32_t r = WR_ARG(0) & 255, g = WR_ARG(1) & 255, b = WR_ARG(2) & 255;
    WR_RET((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3), 3);
}

WR_HANDLER(gbh_Convert16BitGraphic)
{
    (void)base;
    uint32_t buf = WR_ARG(0), bytes = WR_ARG(1);
    if (wr_mem_valid(buf, bytes))
        for (uint32_t i = 0; i + 1 < bytes; i += 2) {
            uint16_t c = wr_rd16(buf + i);   /* X1R5G5B5 -> R5G6B5 */
            wr_wr16(buf + i, (uint16_t)(((c & 0x7C00u) << 1) | ((c & 0x03E0u) << 1) | (c & 0x1Fu)));
        }
    WR_RET(0, 2);
}

#define E(n) { #n, n }
static const wr_export k_gbh_exports[] = {
    E(ConvertColourBank), E(DrawLine), E(MakeScreenTable), E(SetShadeTableA), E(gbh_AddLight),
    E(gbh_AssignPalette), E(gbh_BeginLevel), E(gbh_BeginScene), E(gbh_BlitBuffer), E(gbh_BlitImage),
    E(gbh_CloseDLL), E(gbh_CloseScreen), E(gbh_Convert16BitGraphic), E(gbh_ConvertColour), E(gbh_DrawFlatRect),
    E(gbh_DrawQuad), E(gbh_DrawQuadClipped), E(gbh_DrawTile), { "gbh_DrawTilePart", gbh_DrawTile },
    E(gbh_DrawTriangle), E(gbh_EndLevel), E(gbh_EndScene), E(gbh_FreeImageTable), E(gbh_FreePalette),
    E(gbh_FreeTexture), E(gbh_GetGlobals), E(gbh_GetUsedCache), E(gbh_Init), E(gbh_InitDLL),
    E(gbh_InitImageTable), E(gbh_LoadImage), E(gbh_LockTexture), E(gbh_Plot), E(gbh_PrintBitmap),
    E(gbh_RegisterPalette), E(gbh_RegisterTexture), E(gbh_ResetLights), E(gbh_SetAmbient), E(gbh_SetCamera),
    E(gbh_SetColourDepth), E(gbh_SetWindow), E(gbh_UnlockTexture),
    { NULL, NULL }
};
#undef E

static wr_module k_gbh_module = { "d3ddll.dll", k_gbh_exports, 0 };

void gta2_register_renderer(void) { wr_module_register(&k_gbh_module); }
