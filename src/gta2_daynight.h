/* GTA 2 Recompiled - DAY / NIGHT cycle (see gta2_daynight.c). */
#ifndef GTA2_DAYNIGHT_H
#define GTA2_DAYNIGHT_H

/* The time of day, 0 <= h < 24 (the cycle's clock; also while it is OFF). */
double gta2_daynight_hour(void);

/* The ambient for this frame from the game's own (0..255): 0 = cycle OFF (the original lighting), 1 = the cycle's
 * ambient (out = r, g, b, 0..255; full daylight = 255, every lit face at full brightness). */
int gta2_daynight_ambient(float game_ambient, float out[3]);

/* 1 = car headlights off now: the cycle is ON and it is DAY (08:00-17:00; on for sunset, night and sunrise). */
int gta2_daynight_lights_off(void);
/* 1 = the cycle is ON and it is full NIGHT (21:00-04:30, not the sunset / sunrise). */
int gta2_daynight_full_night(void);

/* render/gbh.c: the world of this frame is drawn (called before the HUD, sub_004CA440) - the HUD's sprites
 * carry the game's light flag too, so the cycle stops here until the next world frame. */
void gbh_daynight_world_end(void);

#endif
