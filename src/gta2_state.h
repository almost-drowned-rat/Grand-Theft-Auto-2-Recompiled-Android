/* GTA 2 Recompiled - what the local player is doing (docs/re/gameplay_input_pause.md §2). */
#ifndef GTA2_STATE_H
#define GTA2_STATE_H

#include <stdint.h>

uint32_t gta2_game(void);           /* the game object, 0 outside a game */
uint32_t gta2_player(void);         /* the local player, 0 outside a game */
uint32_t gta2_player_ped(void);     /* the player's ped, or 0 */
int      gta2_player_driving(void); /* 1: driving a car (not a passenger, not a train) */
float    gta2_sprint_stamina(void); /* 0..1 sprint stamina (1 = full / INFINITE), for a HUD bar */
void     gta2_punch_hold(void);            /* punch held: the current weapon's cooldown kept up so it can't fire */
int      gta2_punch_swap(uint32_t ped);    /* before the ped's tick: punch held -> weapon put away; 1 = swapped */
void     gta2_punch_restore(uint32_t ped); /* after the ped's tick: the weapon back */
void     gta2_sprint_bar_draw(void);  /* SPRINT BAR: the stamina bar (its place: ENHANCEMENTS > SPRINT BAR), after the HUD */
void     gta2_sprint_bar_preview(float alpha);   /* a full sample bar at the set place (the menu, after a POSITION change) */
int      gta2_sprint_exhausted(void); /* 1 while locked out after running dry */

int      gta2_turret_active(void);    /* ANALOGUE: the player drives a turret vehicle with TURRET AIM on (as of the last tick) */
void     gta2_pan_pre(void);          /* ANALOGUE pan: before the game tick, note the look-around offsets */
void     gta2_pan_post(void);         /* after it: the tick's change made bigger (within the game's limits) */
void     gta2_pan_arrows_draw(void);  /* ANALOGUE pan: four arrows at the screen edges while the pan button is held */

#endif
