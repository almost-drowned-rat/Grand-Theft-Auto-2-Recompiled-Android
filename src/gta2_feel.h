/* GTA 2 Recompiled - the ANALOGUE feel presets (gta2_feel.c): analog walking and car steering on the left stick. */
#ifndef GTA2_FEEL_H
#define GTA2_FEEL_H

#include <stdint.h>

/* One preset's numbers. Every field can be overridden from saves/settings.ini with the key "feel_<field>"
 * (e.g. feel_turn_gain=0.4; edit the file while the game is closed - it is read once at start-up), so a preset can
 * be tuned without a rebuild. */
typedef struct {
    float dead_in;       /* stick push (0..1) where walking starts: under it the ped stands still */
    float dead_out;      /* push counted as "all the way" */
    float speed_min;     /* walk speed (fraction of the normal walk) at the edge of the dead zone */
    float speed_curve;   /* >1 = gentle pushes stay slow longer, 1 = linear */
    float turn_gain;     /* share of the remaining angle turned per tick (30 ticks a second) */
    float turn_max;      /* most the ped turns in one tick, 4 units = 1 degree (the original game: 48) */
    float turn_min;      /* least it turns per tick while far from the target (stops the slow creep) */
    float slow_turn;     /* 0..1: how much forward speed is lost while facing far away from the stick */
    float steer_dead;    /* car (set by CONTROLS > ANALOGUE STEERING, not settings.ini): stick sideways push that starts steering */
    float steer_curve;   /* car: >1 = finer steering around the middle, still full lock at the edge */
    float steer_hold;    /* car: ticks one steering pulse is held (1..4); more = smoother but coarser */
    float accel;         /* on foot: speed gained per tick toward the target (1.0 = full speed in one tick) */
    float decel;         /* on foot, SPRINTING: speed lost per tick when slowing, stopping or reversing (the slide) */
    float walk_accel;    /* on foot, not sprinting: speed gained per tick (1.0 = at once) */
    float walk_decel;    /* on foot, not sprinting: speed lost per tick (1.0 = stops at once, no slide) */
    float walk_snap;     /* on foot, not sprinting: 1 = the body turns to the stick at once, 0 = eased turn */
} gta2_feel;

int  gta2_feel_active(void);                 /* 1: a preset other than DEFAULT is chosen */
const gta2_feel *gta2_feel_get(void);        /* the numbers in use (preset + settings.ini overrides) */

float gta2_feel_walk_speed(float mag);       /* stick push -> walk speed, 0 = stand, 1 = the normal walk */
int   gta2_feel_turn_step(int d, int sprinting);   /* angle left to turn (signed, -720..719) -> this tick's turn */
float gta2_feel_turn_slow(int d);            /* forward speed factor (0..1) while d is still far from 0 (not used by walking now) */

/* On-foot momentum, once per game tick: the ped keeps its own velocity, which follows the stick with accel / decel,
 * so stopping or reversing slides instead of circling. Takes the stick (sx, sy, push mag); returns the speed
 * 0..1 (0 = standing) and the direction the ped is moving in as a stick-space vector (*vx, *vy). */
float gta2_feel_move(float sx, float sy, float mag, int sprinting, float *vx, float *vy);
void  gta2_feel_move_reset(void);            /* standing still (after a car, ...) */

/* Called right after the game's own car control step (wrapper of sub_004A4C40): replaces the player's
 * left / none / right steering with a pulsed analog value while the stick is pushed sideways. */
void gta2_feel_car_step(void);

/* Called just before the game's car control step: with ANALOGUE the trigger pulls are added to the player's
 * held forward / back bytes (player+0x78 / +0x79), so the game's own gas / brake / reverse logic runs on them. */
void gta2_feel_car_pre(void);

#endif
