/*
 * GTA 2 Recompiled - MINIMAP: the map picture (see gta2_radar.h). No runtime calls here, so
 * tools can build it too.
 *
 * Map picture: for every one of the 256 x 256 columns of the level's .gmp
 * (DMAP chunk) the lids are drawn top down, each block's lid tile from the
 * .sty (TILE + PALX + PPAL) shrunk to PX x PX pixels with its flip and
 * rotation; a see-through pixel of an upper lid (bridges, rails, wires) shows
 * the lid below. File formats: DMA's GTA2 map format 5.00 / style format 7.00.
 */
#include "gta2_radar_map.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAP_N 256

/* ---- files ----------------------------------------------------------------------------------- */

static unsigned char *read_all(const char *host, size_t *n)
{
    FILE *f = fopen(host, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = len > 0 ? malloc((size_t)len) : NULL;
    if (d && fread(d, 1, (size_t)len, f) != (size_t)len) {
        free(d);
        d = NULL;
    }
    fclose(f);
    *n = d ? (size_t)len : 0;
    return d;
}

static uint32_t rd32le(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16le(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* a chunk of a GBMP / GBST file: its data and size, or NULL */
static const unsigned char *chunk(const unsigned char *d, size_t n, const char *magic, const char *type, size_t *len)
{
    if (n < 6 || memcmp(d, magic, 4))
        return NULL;
    size_t o = 6;
    while (o + 8 <= n) {
        size_t sz = rd32le(d + o + 4);
        if (sz > n - o - 8)
            return NULL;
        if (!memcmp(d + o, type, 4)) {
            *len = sz;
            return d + o + 8;
        }
        o += 8 + sz;
    }
    return NULL;
}

/* ---- the picture ----------------------------------------------------------------------------- */

typedef struct {
    const unsigned char *tiles, *ppal;
    size_t ntiles, nppal;
    const unsigned char *palx;   /* 16384 u16 */
    uint32_t *small;             /* per tile px*px RGBA (0 = see-through), made on first use */
    unsigned char *made;
    int px;
} style;

static uint32_t pal_colour(const style *s, uint32_t pal, uint32_t c)
{
    size_t off = (size_t)(pal / 64) * 65536 + ((size_t)c * 64 + pal % 64) * 4;
    if (off + 3 > s->nppal)
        return 0xFF000000u;
    const unsigned char *b = s->ppal + off;   /* B G R x */
    return 0xFF000000u | (uint32_t)b[0] << 16 | (uint32_t)b[1] << 8 | b[2];
}

static const uint32_t *tile_small(style *s, uint32_t t)
{
    int px = s->px, n = px * px;
    uint32_t *out = s->small + (size_t)t * (size_t)n;
    if (s->made[t])
        return out;
    s->made[t] = 1;
    uint32_t pal = rd16le(s->palx + 2 * t);   /* tiles use virtual palettes from 0 (PALB tile base) */
    int step = 64 / px;
    const unsigned char *page = s->tiles + (size_t)(t / 16) * 65536;
    int tx = (int)(t % 16 % 4) * 64, ty = (int)(t % 16 / 4) * 64;
    for (int by = 0; by < px; by++)
        for (int bx = 0; bx < px; bx++) {
            uint32_t r = 0, g = 0, b = 0, k = 0;
            for (int y = by * step; y < by * step + step; y++)
                for (int x = bx * step; x < bx * step + step; x++) {
                    uint32_t c = page[(size_t)(ty + y) * 256 + (size_t)(tx + x)];
                    if (!c)
                        continue;   /* index 0: see-through */
                    uint32_t rgb = pal_colour(s, pal, c);
                    r += rgb & 0xFF;
                    g += rgb >> 8 & 0xFF;
                    b += rgb >> 16 & 0xFF;
                    k++;
                }
            /* mostly see-through: let the lid below show */
            out[by * px + bx] = k * 2 < (uint32_t)(step * step) ? 0
                                : 0xFF000000u | (b / k) << 16 | (g / k) << 8 | r / k;
        }
    return out;
}

uint32_t *gta2_radar_build_picture(const char *gmp_host, const char *sty_host, int px)
{
    if (px < 1 || px > 16 || 64 % px)
        return NULL;
    size_t gn = 0, sn = 0;
    unsigned char *gmp = read_all(gmp_host, &gn), *sty = read_all(sty_host, &sn);
    uint32_t *img = NULL;
    style s = { 0 };
    size_t dlen = 0, tlen = 0, plen = 0, xlen = 0;
    const unsigned char *dm = gmp ? chunk(gmp, gn, "GBMP", "DMAP", &dlen) : NULL;
    s.tiles = sty ? chunk(sty, sn, "GBST", "TILE", &tlen) : NULL;
    s.ppal = sty ? chunk(sty, sn, "GBST", "PPAL", &plen) : NULL;
    s.palx = sty ? chunk(sty, sn, "GBST", "PALX", &xlen) : NULL;
    if (!dm || !s.tiles || !s.ppal || !s.palx || xlen < 32768 || dlen < 65536 * 4 + 4) {
        goto out;
    }
    s.ntiles = tlen / 4096;
    s.nppal = plen;
    s.px = px;
    s.small = calloc(s.ntiles * (size_t)(px * px), sizeof(uint32_t));
    s.made = calloc(s.ntiles, 1);

    const unsigned char *base = dm;
    size_t o = 65536 * 4;
    uint32_t ncw = rd32le(dm + o);
    o += 4;
    if (ncw > (dlen - o) / 4)
        goto out;
    const unsigned char *cols = dm + o;
    o += (size_t)ncw * 4;
    if (o + 4 > dlen)
        goto out;
    uint32_t nb = rd32le(dm + o);
    o += 4;
    if (nb > (dlen - o) / 12)
        goto out;
    const unsigned char *blocks = dm + o;

    int w = MAP_N * px;
    img = calloc((size_t)w * (size_t)w, sizeof(uint32_t));
    if (!img || !s.small || !s.made)
        goto out;
    for (int y = 0; y < MAP_N; y++)
        for (int x = 0; x < MAP_N; x++) {
            uint32_t c = rd32le(base + 4 * (size_t)(y * MAP_N + x));
            if (c >= ncw)
                continue;
            uint32_t head = rd32le(cols + 4 * (size_t)c);
            int height = (int)(head & 0xFF), off = (int)(head >> 8 & 0xFF);
            int left = px * px;   /* pixels still empty in this block */
            for (int z = height - 1; z >= off && left > 0; z--) {
                uint32_t ci = c + 1 + (uint32_t)(z - off);
                if (ci >= ncw)
                    break;
                uint32_t bi = rd32le(cols + 4 * (size_t)ci);
                if (bi >= nb)
                    continue;
                uint16_t lid = rd16le(blocks + (size_t)bi * 12 + 8);
                uint32_t t = lid & 0x3FFu;
                if (!t || t >= s.ntiles)
                    continue;
                int rot = lid >> 14 & 3, flip = lid >> 13 & 1;
                const uint32_t *sm = tile_small(&s, t);
                for (int py = 0; py < px; py++)
                    for (int qx = 0; qx < px; qx++) {
                        uint32_t *d = &img[(size_t)(y * px + py) * (size_t)w + (size_t)(x * px + qx)];
                        if (*d)
                            continue;
                        int u = flip ? px - 1 - qx : qx, v = py;
                        for (int r = 0; r < rot; r++) {
                            int nu = v;
                            v = px - 1 - u;
                            u = nu;
                        }
                        uint32_t col = sm[v * px + u];
                        if (col) {
                            *d = col;
                            left--;
                        }
                    }
            }
        }
    for (size_t i = 0; i < (size_t)w * (size_t)w; i++)
        if (!img[i])
            img[i] = 0xFF000000u;   /* nothing there: black */
out:
    free(s.small);
    free(s.made);
    free(gmp);
    free(sty);
    return img;
}
