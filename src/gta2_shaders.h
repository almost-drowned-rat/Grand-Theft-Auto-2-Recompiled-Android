/* GTA 2 Recompiled - the SHADERS page (gta2_shaders.c). */
#ifndef GTA2_SHADERS_H
#define GTA2_SHADERS_H

#include <stddef.h>

enum { SI_NONE, SI_UP, SI_FOLDER, SI_PRESET };
typedef struct { const char *label; int kind; int index; char key[48]; } gta2_shader_item;

void gta2_shaders_apply(void);                 /* settings -> the shader pass */
int  gta2_shader_on(void);
void gta2_shader_set_on(int on);
void gta2_shader_preset_name(char *out, size_t cap);
const char *gta2_shader_hint(void);            /* a line to show, or NULL */
void gta2_shader_browse_open(void);
int  gta2_shader_browse(const gta2_shader_item **items);
int  gta2_shader_pick(const gta2_shader_item *it);   /* 1: the folder changed */
void gta2_shader_dir_title(char *out, size_t cap);
int  gta2_shader_browse_up(void);             /* B: one folder up, 0 at the top */

int  gta2_shader_params(void);                /* the preset's parameters (once it is built) */
const char *gta2_shader_param_label(int i);
void gta2_shader_param_value(int i, char *out, size_t cap);
void gta2_shader_param_step(int i, int d);
void gta2_shader_param_reset(int i);
int  gta2_shader_resolution(void);            /* 0 ORIGINAL, 1 2X, 2 FULL */
void gta2_shader_set_resolution(int r);

#include <stdint.h>
const uint32_t *gta2_shader_test_picture(void);   /* 640x480 RGBA */
void gta2_shader_test_reload(void);   /* data/shader_test.png changed */
/* SHADERS > TAKE TEST PICTURE (pause menu): 2 = asked (the menu is hidden), 1 = saved this frame, -1 = reload */
extern int g_gta2_test_grab;

#endif
