/*
 * GTA 2 Recompiled - MINIMAP (GAMEPLAY > MINIMAP; maintainer 2026-11-27).
 *
 * Our own radar: the map picture is made at level load from the game's own
 * files (the level's .gmp block lids + .sty tiles), nothing of other mods is
 * used. Drawn after the HUD (sub_004CA440). Blips: docs/re/minimap.md.
 */
#ifndef GTA2_RADAR_H
#define GTA2_RADAR_H

#include "wr.h"

void gta2_radar_init(void);            /* at start: watches which .gmp / .sty the game opens */
void gta2_radar_draw(X86Ctx *ctx);     /* before the HUD (sub_004CA440), every frame in a game */
int  gta2_radar_draw_full(float x, float y, float s);   /* PAUSE MAP: the district in a square; 0 = none */
/* PAUSE MAP legend: the n-th line shown (only switched-on kinds), its label; the icon drawn centred at
 * (x, y), d wide. Returns NULL past the last line. */
const char *gta2_radar_legend(int n, float x, float y, float d);

/* NAVIGATION ARROWS: before the game draws its arrows (mgr = HUD+0x1F18). Returns 0xFFFFFFFF = draw none,
 * else a mask of arrows made invisible for this one draw; restore puts their visible byte back. */
uint32_t gta2_radar_arrows_hide(uint32_t mgr);
void     gta2_radar_arrows_restore(uint32_t mgr, uint32_t hidden);

#endif
