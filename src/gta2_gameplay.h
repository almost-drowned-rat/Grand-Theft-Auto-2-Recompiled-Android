/* GTA 2 Recompiled - GAMEPLAY options: KEEP BODIES, MORE BLOOD (gta2_gameplay.c). */
#ifndef GTA2_GAMEPLAY_H
#define GTA2_GAMEPLAY_H

#include <stdint.h>

int  gta2_body_before_tick(uint32_t ped);   /* 1: kept this tick (undo its count) */
void gta2_blood_before_tick(uint32_t particle);
int  gta2_more_blood(void);
void gta2_sprite_look(uint32_t sprite, float *scale, float *tint, float *alpha);

/* PEDESTRIANS */
#include "recomp_types.h"
void gta2_peds_game_start(X86Ctx *ctx, uint32_t manager);   /* after sub_004416B0 (its return value) */
int  gta2_ped_factor(void);
int  gta2_peds_spawn_ok(void);
void gta2_peds_live(X86Ctx *ctx);          /* between loop steps */
extern int g_gta2_peds_pending;
int  gta2_traffic_factor(void);
void gta2_traffic_tick(void);            /* once per game tick */

/* CAMERA DISTANCE (issue #2): the game's target zoom constants scaled, docs/re/camera_distance.md */
void gta2_camera_distance_apply(void);   /* every frame (cheap): writes only on a change */
/* FIXED CAMERA (issue #6): no speed zoom, no lead, one fixed height (gta2_gameplay.c) */
int  gta2_fixed_camera_on(void);
int  gta2_is_player_camera(uint32_t cam);
void gta2_fixed_camera_apply(void);      /* every frame */

#endif
