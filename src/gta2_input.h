/* GTA 2 Recompiled - controller schemes and the CUSTOM / keyboard bindings (gta2_input.c). */
#ifndef GTA2_INPUT_H
#define GTA2_INPUT_H

#include <stddef.h>
#include <stdint.h>

/* 12 game actions + SPRINT + RADIO NEXT / PREVIOUS + MENU + CAMERA / ZOOM IN / ZOOM OUT
 * (the keyboard: the first 13 and the camera's three) */
#define GTA2_BIND_N 19

/* the controller lines follow CONTROLLER: MODERN = MODERN's own button layout (issue #3), else CUSTOM's */
int  gta2_bind_count(int keyboard);
/* CUSTOM MAPPING's lines in groups: row r is a heading ("" = empty line; returned, *bind = -1) or a binding
 * (NULL returned, *bind = its index for gta2_bind_label / _value / _set_*) */
int  gta2_bind_rows(int keyboard);
const char *gta2_bind_row(int keyboard, int r, int *bind);
const char *gta2_bind_label(int keyboard, int i);
void gta2_bind_value(int keyboard, int i, char *out, size_t cap);
int  gta2_bind_set_pad(int i, int control);   /* 0: refused (MODERN: not one of its buttons) */
int  gta2_bind_profile(void);                 /* PROFILE 1..3: a whole mapping each */
void gta2_bind_profile_set(int p);            /* then call sub_0044BF30 (the keyboard keys changed) */
void gta2_bind_set_key(int i, uint8_t dik);   /* then call sub_0044BF30 */
void gta2_bind_defaults(int keyboard);        /* the keyboard: then call sub_0044BF30 */

int  gta2_sprint_pressed(void);
int  gta2_pan_held(void);       /* CAMERA PAN: the SPECIAL 2 button is held, the right stick pans the camera */
int  gta2_punch_pressed(void);   /* PUNCH: its button is held (on foot) */
int  gta2_gas_on_rt(void);       /* gas is mapped to R2: the pedal is analogue (gta2_feel.c) */
int  gta2_brake_on_lt(void);     /* brake / reverse is mapped to L2: analogue too */
void gta2_camera_keys(void);   /* CAMERA KEYS: once per frame in play */
void gta2_keys_apply(void);
/* CONTROL OVERVIEW rows: function, "controller   |   keyboard" for the schemes in use */
int  gta2_overview_count(void);
const char *gta2_overview_label(int i);
int  gta2_overview_is_head(int i);
void gta2_overview_value(int i, char *out, size_t cap);
/* CONTROLLER LAYOUT: "LT  SPECIAL 2" for picture item id (gta2_padview.h) */
void gta2_pad_function(int id, int driving, char *out, size_t cap);
int  gta2_kbm_sticks(float *lx, float *ly, float *rx, float *ry);
int  gta2_game_centre(float *x, float *y);   /* the game picture's centre in window points (render/vid.c) */

#endif
