/*
 * GTA 2 Recompiled - MINIMAP map picture from the game's own files (gta2_radar_map.c).
 */
#ifndef GTA2_RADAR_MAP_H
#define GTA2_RADAR_MAP_H

#include <stdint.h>

/* The map picture of a level: its .gmp + .sty (host paths) at px pixels per block (1, 2, 4, 8 or 16).
 * Returns malloc'd RGBA (R in the low byte, opaque), 256*px pixels square, or NULL. */
uint32_t *gta2_radar_build_picture(const char *gmp_host, const char *sty_host, int px);

#endif
