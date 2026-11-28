# Save anywhere (GTA 2 issue #6)

2026-11-28. [V] read in the disassembly, [I] inferred. Port: `src/gta2_saveany.c`, the pause line SAVE GAME and
the main-menu line LOAD GAME in `src/gta2_menu.c`, the slot-name wrapper `sub_004528A0` in `src/gta2_manual.c`.

## The church

- Script opcode 0x1B6 = `sub_0047FE80` (thiscall script thread) [V]. First entry (thread+0xC == 0): frame counter
  `[[0x5E8108]] > 0`; a job running (`[[0x6644BC]+0x344]` non-null and `*p != 0`) -> message `svmiss`; money
  (`sub_00421980`) >= 50000 (fixed, every district) -> sound 0x3D, `sub_00421990(player, -50000)`,
  `sub_0047EF40(ecx=[0x6644BC], 0x5EC070+0x300)`, message `svdone`; else `svscore` [V]. Message:
  `sub_004C6750(ecx=[0x672F40]+0xE4, 1, key)` - the HUD's message line [V]. Then it waits until the ped leaves the
  rect. Opcode 0x115 `sub_0047FE50` = the same save without checks (unused by the main district scripts) [V].

## The writer and the file (.svg, redirected to saves/ by the port's file layer)

- `sub_0047EF40(this = script [0x6644BC], const char *path)`, thiscall, ret 4; NULL / "" path -> `player\plyslot%d.svg`
  with the profile slot `[0x5EC070+0x403]` [V]. `0x5EC070+0x300` = the area's save name (set when a save was loaded,
  `sub_0045E470` = strncpy 0xFF; "" in a fresh area).
- Header 0x748 bytes from 0x664590: gmp / sty / scr names (25 bytes each), main / bonus area, bonus flag; the player
  block at +0x54 (`sub_004A6B20`): x / y / z Q14 = ped+0x1AC / 0x1B0 / 0x1B4, heading, money, counters, ped+0x216
  (health), weapons' ammo, gang respects, ped+0x20A (cop points) ...; the thread table (command id, cmd+8) of every
  0x113 / 0x114 thread; then 3 chunks `u32 size` + data from `sub_004642A0([0x662C08])` [V layout, I meanings].
- Load: area start `sub_0045B470` -> `sub_0047F0B0(script, name)` when +0x300 is set; it restores the block, the
  threads and the chunks and sets `script+0xC1E2C = 1`; the player-create handler `sub_00477140` then spawns at the
  saved x / y / z (x / y snapped to the block + an offset) [V]. **The position is in the file: the player comes back
  where he saved.** Cars and peds are not saved.

## Fields for "may save now" [V unless noted]

- Wanted: `s16 ped+0x20A` = cop points; HUD heads (`sub_00434C00`) from 600 / 1600 / 3000 / 5000 / 8000 / 12000.
- In a car: ped+0x16C = the car, ped+0x168 (on-foot sprite) = 0; ped+0x1AC.. are stale then (copied from the sprite
  each ped tick only while it exists).
- States ped+0x278 / ped+0x27C (setters `sub_00433270` / `sub_004332B0`): (0, 0) plain on foot [I]; (9, 0xF) dead;
  (0xA, 0xA) in a car; (8, 0x11..0x1B) knocked down / ragdoll; state 2 = 6 / 7 getting in / out.
- ped+0x254 = the surface type under the ped (`sub_00465650`), 4 = water [I].
- Falling / drowning: not found - the port requires the height ped+0x1B4 to stand still for 15 frames.

## Frontend (LOAD GAME)

- Page 0 built by `sub_00453E20`: u16 count at fe+0x122 (3), item i at fe+0x126 + i*0x82 (+0 type 1, +1 visible,
  +2 x 300, +4 y 250 / 270 / 290, +6 u16 text[50], +0x6A font / +0x6C remap 0xFFFF, +0x80 action: 1 PLAY, 0x101 /
  0x102 / 9), cursor markers at fe+0xCAC + 6*i (x 280, y = item y + 8, visible), cursor page+0xBC6 [V]. Action 0x10C
  does nothing (0x459A35) [V].
- The game's own load (RESUME SAVED STATUS, handler 0x45997A): `sub_00452940(slot)` (file exists; name from
  `sub_004528A0(u8 slot, char *out)` stdcall ret 8), `sub_00455850(fe)` (header vs the area table, sets the level
  manager, `sub_0045E470(name)`), then `fe+0xEDF4 = 0x10; fe+0x110 = 2` = start the game [V].

## The port

- SAVE GAME (pause menu, under RESUME): free (maintainer); refused on a job, in a bonus area, in a car, wanted (any
  cop points), on water, in any state but (0, 0) or while the height moved in the last 15 frames; else
  `sub_0047EF40` with `player\anyslotN.svg`, the HUD's `svdone`, back into the game. The line's value shows why not.
- LOAD GAME (main menu, under PLAY, only when the profile's anyslot file exists): the game's load path with
  `sub_004528A0` answering `player\anyslotN.svg`, then "start the game". Once the player exists, +0x300 is set back
  to "" so a church save writes `plyslotN` again; a death still goes back to the church save (maintainer).
