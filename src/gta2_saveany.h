/* GTA 2 Recompiled - SAVE ANYWHERE (issue #6, gta2_saveany.c). */
#ifndef GTA2_SAVEANY_H
#define GTA2_SAVEANY_H

#include <stdint.h>
#include "recomp_types.h"

void        gta2_saveany_frame(int paused);          /* every frame (HUD): the height-still counter */
const char *gta2_saveany_refusal(void);              /* NULL = SAVE GAME allowed now, else why not */
int         gta2_saveany_save(X86Ctx *ctx);          /* 1 = saved (player\anyslotN.svg) */
int         gta2_saveany_exists(void);               /* the profile's save-anywhere file exists */
int         gta2_saveany_load(X86Ctx *ctx, uint32_t fe);   /* LOAD GAME from the main menu: 1 = the game starts */
void        gta2_saveany_game_running(X86Ctx *ctx);  /* every frame in play: clears the save name after a load */
int         gta2_saveany_redirect(void);             /* 1: the slot name is player\anyslotN.svg (LOAD GAME runs) */

#endif
