# GTA 2 minimap / radar: map, arrows, objects, fixed places, player, states, hook point

Target: gta2.exe (2004 freeware build, image base 0x400000).
Goal: the facts needed to draw our own GTA3-style radar in the port (map picture + blips).

Tags: **[V]** = verified in the disassembly (address given), **[I]** = inferred (reason given),
**[D]** = checked against the game's data files (`extract/game/App_Executables/data`).
Fixed point Q14 (1.0 = 0x4000 = one block). `this` in ecx (thiscall) unless noted. One logic tick =
33 ms. Source: `analysis/disasm/asm/text.asm`. Nothing here was tested on a running build.
Known addresses and conventions: gameplay_input_pause.md (game `[0x5EB4FC]`, player = game+0x38,
ped = player+0x2C4, ped+0x16C car, Ang16 0..1439), enhancements.md (map zones `[0x662C08]+0x328..`),
widescreen.md (camera fields), framerate.md.

---

## 0. Summary

* **Map** = `[0x662C08]` (map object), `+0` = the DMAP block loaded raw from the .gmp file:
  `base[256*256]` u32 column index, column words, 12-byte block infos. Block lookup `sub_004653C0` /
  `sub_0042A850`. Ground type = `block+0xB & 3` (1 road, 2 pavement [V], 3 field, 0 air), lid tile =
  `block+8 & 0x3FF`. Water = lid tile in SPEC list 3 of the .sty (style table `[0x670684]+0x6C`).
  The in-memory layout is the file layout, so the radar picture can be made from the .gmp/.sty file.
  Level files: `char gmp[256]` at **0x5EC070**, `sty` at 0x5EC170, `scr` at 0x5EC270.
* **Arrows**: arrow manager = HUD `[0x672F40]` + **0x1F18**, **17 arrows x 0x7C**. Each arrow has two
  targets (+0x30, +0x54), the active one at +0x78; target type 1 coord / 2 ped / 3 car / 4 object /
  6 player, position at target+0x14/+0x18/+0x1C (Q14). Colour +0x20 (1 yellow, 2 red, 3 green
  phone, 4 mission, 5 flashing). Visible flag +0x2D. Drawn only by **`sub_004C84C0`** (single call
  0x004CA4D8): skipping it hides the arrows and changes no game state.
* **Objects** (pickups, phones, garage triggers, ...): pool `[0x665784]`, **3825 slots x 0x2C** at
  pool+8. Slot in use when `obj+4` (sprite) != 0; model = `obj+0x18`; position = sprite+0x14/+0x18/+0x1C.
  Phones = models **0xB0..0xB5** [V]; collectables = **200..244** [V range]; token = **266** [V].
* **Fixed places**: churches = mission-script command **0x1B6** (save point, $50000) with rect at
  cmd+0x0C..+0x1C [V]; spray / bomb shops = objects of model **130** (subtype in obj+0x26) [V];
  cranes = `[0x5E747C]`; phones = objects 0xB0..0xB5 (gang from map zones); hospitals / police
  restarts = map zones type 16 / 20 [D].
* **Player**: position / heading as in gameplay_input_pause.md, **z = ped+0x1B4** [V]; in a car use the
  car sprite `[car+0x50]` (+0x14/+0x18/+0x1C, heading u16 at +0). Game's own "player position"
  `sub_004A6610`. Camera = player+0x90 (x +0x98, y +0x9C, z +0xA0, zoom +0xA4).
* **Hook**: the port already wraps `sub_004CA440` (HUD draw) in `src/gta2_manual.c`. Best place for
  the radar under the game's big text, PAUSED panel and quit prompt: a wrapper of **`sub_004C84C0`**
  (the arrow draw, called in the middle of the HUD draw) that draws the radar instead of / after the
  arrows.

---

## 1. Map in memory

### 1.1 Objects [V]

| what | address | notes |
|---|---|---|
| map object | `[0x662C08]` | `+0` DMAP pointer (`sub_004653C0` 0x004653C5 `mov esi,[ecx]`); zone data +0x328..+0x334 (enhancements.md 1.4); `+0x34C/+0x354` block count, `+0x350` block capacity, `+0x358/+0x360` column-word count, `+0x35C` column-word capacity (`sub_004646A0` 0x0046474C-0x0046483E) |
| DMAP | `[[0x662C08]]` | allocated `0x40010` bytes (0x004646A4). `+0x00000` u32 base[256*256] (read as one 0x40000 block, 0x004646E4); `+0x40000` u32 column-word count; `+0x40004` u32 block count; `+0x40008` u32* column words; `+0x4000C` block infos (12 bytes each). Both arrays get spare room (**+0x400 column words, +0xC8 blocks**, 0x00464713 / 0x004647B7) for script map changes |
| loader | `sub_00464990` → chunk switch `sub_00464890` | `DMAP` → `sub_004646A0`, `ZONE` → `sub_00464640`, `MOBJ` → `sub_00464590`, `ANIM` → `sub_00464670`, `LGHT`, `RGEN` (0x00464898-0x00464953). CMAP / PSXM / EDIT in the file are ignored [I: no handler] |
| called from | `sub_0045B470` (level load) 0x0045B4C6 | file name = `sub_0045E4E0(0x5EC070)` = 0x5EC070+0 |

### 1.2 Lookup [V]

```c
// sub_0042A830(dmap, a1, a2) = &base[a2*256 + a1]              (0x0042A838-0x0042A83D)
// sub_004653C0(map, x, y, z) (ret 0xC), ints in blocks:          (0x004653C0-0x0046540D)
u32  col  = base[y*256 + x];                  // index into column words
u8  *cw   = (u8*)&colwords[col];
u8   h    = cw[0], off = cw[1];               // column covers z = off .. h-1
if (z >= h || z < off) return NULL;           // air
u32  bi   = colwords[col + 1 + (z - off)];
return &blocks[bi];                           // 12 bytes
```
`sub_0042A850(map, x, y, z)` is the same with byte arguments (0x0042A850-0x0042A8B0). Q14 -> block:
`v >> 14` (`sub_0041F9D0`). x = column (east), y = row (south) [I: GMP base[y][x], screen right = +x].
Levels z = 0..7 [I: GTA 2 maps have 8 levels; h is a byte].

### 1.3 Block info (12 bytes)

| off | field | evidence |
|---|---|---|
| +0x0..+0x6 | u16 left, right, top, bottom side faces | [I: GMP format; not read here] |
| +0x8 | u16 **lid**: bits 0-9 tile number, 10-15 lighting/flat/flip/rotation | [V `and 0x3FF` 0x00466B36; I upper bits] |
| +0xA | u8 arrows (traffic directions) | [I: GMP format] |
| +0xB | u8 slope_type: bits 0-1 **ground type**, bits 2-7 slope | [V `and 3; cmp 1` = road `sub_0042A8C0`, `sub_00433470..sub_00433500`; `cmp 2` = pavement `sub_00433530`] [I: 3 field, 0 air] |

**Surface type of a lid tile** [V]: `[[0x670684]+0x6C + tile*4]` (style object; `sub_00462FD0`, which
maps 3 to 1). Filled from the .sty `SPEC` chunk: lists 1..9 → types 2..10 (`sub_004C00A0`,
`sub_004C0040`). [D] In wil.gmp the third list (type 4) holds **exactly the 4463 top lids at z = 0**
(the sea around the map), so **SPEC list 3 = water** (tiles 608.. in all three styles). The other lists
follow the style format order [I]: grass/dirt, road special, water, electrified, electrified platform,
wood floor, metal floor, metal wall, grass wall.

### 1.4 Colouring a radar pixel (recommended)

For each (x, y): scan z from h-1 down to off, take the first block with `lid & 0x3FF != 0`.
* lid tile in SPEC water list → water
* ground 1 → road, 2 → pavement
* ground 3 (field) and top z >= 2 → building roof, else grass/field [I: street level is z = 1 in wil.gmp,
  28362 of 65536 top lids; roofs z 2..6] [D]
* nothing found → empty (void)

The DMAP chunk in the .gmp file has the same layout (base, column count + words, block count + blocks)
[D: wil.gmp parsed with this layout gives sane results]. Reading the file once at level load is fine;
script map changes (`map change overflow` errors, spare room above) only touch a few blocks.

### 1.5 Which level [V]

Level manager **0x5EC070** (static): `+0x000` gmp file name, `+0x100` sty, `+0x200` scr, `+0x300` extra
scr (bonus/save, may be empty) (`sub_0045E4E0..sub_0045E510`, used by `sub_0045B470`).
`+0x400` u8 main level index (set from the profile, 0x004557FA), `+0x401` u8 bonus index (0x0045582E),
`+0x402` u8 1 = bonus level (0x00455806 / 0x0045583A) [V writes, I names].
From data\test1.seq [D]: wil.gmp = City 1 (**Downtown**), ste.gmp = City 2 (**Residential**),
bil.gmp = City 3 (**Industrial**) [I: district names]; bonus maps lorne2e/m/h, mike1e/m/h, mike2e/m/h.

---

## 2. Navigation arrows

### 2.1 Where [V]

HUD `[0x672F40]` + **0x1F18** = arrow manager (ctor `sub_004C7080`: array ctor 17 x 0x7C,
`+0x83C` u8 = 1 gang arrows enabled, `+0x840` arrow ptr, `+0x844` u8).

| per arrow (0x7C) | meaning |
|---|---|
| +0x00 / +0x04 | screen x / y of the drawn arrow (passed to the sprite draw) [V 0x004C833A-0x004C8342] |
| +0x08 | u16 screen angle [V 0x004C8031, 0x004C8332] |
| +0x20 | **colour type** (`sub_004C5E60`): 1 yellow, 2 red, 3 green (gang phone, names from `sub_004C5EE0`), 4 default script arrow (`sub_004CA610`), 5 flashing (script opcodes 0x1B8/0x1B9, 0x00478E6D / 0x00478F7A; SET colour opcode `sub_00478FB0`) |
| +0x24 | u16 sprite frame (gang arrow frame = gang+0x138; 0 for script arrows) [V 0x004CAA63] |
| +0x26 | u8 retarget countdown (20) [V 0x004C7FA2] |
| +0x28 | gang info ptr; **0 = mission (script) arrow** [V `sub_004C7030`] |
| +0x2C | s8 minimum gang respect (yellow 40, red 80, green -19, `sub_004C5F10`) |
| +0x2D | u8 **visible** (result of `sub_004CA770`, stored 0x004CA875) |
| +0x2E | u8 slot used (`sub_004C5E70`) |
| +0x30, +0x54 | two **targets** (0x24 each) |
| +0x78 | ptr to the active target (+0x30 or +0x54); the closer one is picked every 20 ticks (`sub_004C7E60`) |

Target (0x24): `+0x00` Ped*, `+0x04` Car*, `+0x08` Object*, `+0x0C` Player*, `+0x10` **type**,
`+0x14/+0x18/+0x1C` **x/y/z (Q14)**, `+0x20` u8 [I: on screen]. Types (jump table 0x4C7E24,
`sub_004C7CF0`, refreshed every tick): **0 none**, **1 coordinate** (`sub_004767C0`), **2 ped**
(alive `ped+0x21C & 1`; position ped+0x1AC/+0x1B0/+0x1B4), **3 car** (dead when car+0x88 == 5;
position from car sprite +0x50), **4 object** (dead when model 0xAE; position from obj+4 sprite),
5 coordinate kept as is [I: used for the phone being answered, `sub_004CA770`], **6 player** (its ped).
A dead target is cleared (type 0) [V 0x004C7DD9].

### 2.2 Ticks and visibility [V]

* World tick → HUD tick `sub_004CAB50` → `sub_004CA890(mgr)`: for each arrow with a target,
  `sub_004CA860` refreshes positions, then `+0x2D = sub_004CA770()`. So arrows do **not** update while
  paused (still drawn).
* `sub_004CA770`: script arrow (+0x28 == 0) → visible. Gang arrow → hidden unless `mgr+0x83C`;
  `show_all_arrows` (debug byte **0x5EADB2**) → visible; hidden while a mission runs
  (`[[0x6644BC]+0x344]` non-null and `[..] != 0`, `sub_004C7350`); hidden if the gang's respect for the
  local player (`s8 gang+0x11C+player+0x2E`, `sub_0045DAC0`) < +0x2C; if `HUD+0x2758` is set, only
  that arrow.
* Gang phone arrows are created at level start: script init `sub_00481400` → create object
  `sub_00476EA0` → `sub_004CA910` (phone model → colour, gang = `sub_00465350(map, x, y)`, error
  "Placing gang phone when no gang", up to 2 phones per gang+colour in one arrow).

### 2.3 Drawing and suppression [V]

`sub_004C84C0(mgr)` (thiscall, plain `ret`, only caller **0x004CA4D8** in the HUD draw): for 17 arrows
`sub_004C82C0` (skip if both targets empty or +0x2D == 0): HUD sprite type 6, frame +0x24 at
(+0, +4), angle +8, scale cam+0xA8; then, for colour 1/2/3, a second sprite frame 8 / 10 / 9;
colour 5 blinks (mode 7/2). In network games (`[0x673E2C]`) an arrow is skipped when the ped of
the player in its first target (+0x30+0xC) has ped+0x21C bit 25 set.
**To hide the arrows**: wrapper of `sub_004C84C0` that returns without calling the generated code
(`ctx->r_esp += 4`). It only draws (sprite calls, no writes), so the logic, targets and
mission script are untouched.

**Blips from arrows** (mirrors exactly what the game would show):
```c
mgr = R32(0x672F40) + 0x1F18;
for (i = 0; i < 17; i++) {
    a = mgr + i*0x7C;
    if (!R8(a+0x2E) || !R8(a+0x2D)) continue;
    t = R32(a+0x78);  if (!t || R32(t+0x10) == 0) continue;
    x = R32(t+0x14); y = R32(t+0x18); z = R32(t+0x1C);   // Q14
    colour = R32(a+0x20);   // 1 yellow, 2 red, 3 green, 4 mission, 5 flashing
}
```

---

## 3. Object pool (pickups, phones, triggers)

### 3.1 Pool [V]

| what | address | notes |
|---|---|---|
| object pool | `[0x665784]` | alloc 0x29178 (0x00484B71); ctor `sub_00483F10`: `+0` free head, `+4` active list head, `+8` **3825 (0xEF1) objects x 0x2C** (0x00483F1E-0x00483F3D) |
| object manager | `[0x665788]` | create `sub_004852E0(model, x, y, z, rot)` / `sub_00485480` (models 200..244) → `sub_00484E00`; `+0x20` 50 token flags, `+0x54` token index (0x00484E09-0x00484E1E) |
| object info | `[0x665B40]` | `sub_00488170(model)` = `[+0x87F4 + model*4]` (+0x34 class, +0x40, +0x5C, +0x61 "ticked") |

Object (0x2C) [V `sub_00482490`, `sub_00483990`, `sub_00484910`]:

| off | meaning |
|---|---|
| +0x00 | next (free list / active list) |
| +0x04 | **sprite**; position sprite+0x14/+0x18/+0x1C (Q14), angle u16 sprite+0 (`sub_004340D0/E0`, `sub_00420600/690`) |
| +0x08 | object info ptr |
| +0x14 | unique id (counter 0x593210) |
| +0x18 | **model** |
| +0x20 | 1 = on the active (ticked) list, 2 = static |
| +0x26 | u8 per-object parameter (0x63 default; garage subtype / crane index, section 4) |

**In use** = `obj+4 != 0`: freeing sets model, sprite and id to 0 (0x00484919, 0x00484999, 0x0048499C).
Static objects (+0x20 == 2) are not on any list, so **scan all 3825 slots** (0x2C stride from pool+8).

### 3.2 Models

| model | meaning | tag |
|---|---|---|
| 0xB0, 0xB1 | phone, red gang colour | [V `sub_00475AA0`, `sub_004C5EA0`] |
| 0xB2, 0xB3 | phone, yellow | [V] |
| 0xB4, 0xB5 | phone, green | [V] (two per colour [I: idle / ringing]) |
| 200..244 (0xC8..0xF4) | collectables (weapons, power-ups), created by `sub_00485480` | [V range 0x00476EDC-0x00476EE8] |
| weapons 200-223, bonuses 228-240 | sub-ranges from the mod's claim | [I unverified] |
| 266 (0x10A) | **token**: not created again once collected (50 flags) | [V 0x00484E09; I name] |
| 130 (0x82) | garage trigger (spray / bomb shop), section 4 | [V] |
| 137 (0x89) | crane trigger | [V 0x00429437] |
| 143 (0x8F) | trigger of `[0x5E7D70]` manager [I: car crusher] | [V call 0x00429461] |
| 129, 139, 141, 161, 167, 8, 9 | other car-collision objects (table 0x4294F8); 161 / 167 see below | [V, meaning open] |
| 149 / 150 / 10 | antenna van / Wang car pickup / mines (mod's claim) | [I unverified] |

**Follow-up after the run-time pool log (Downtown, level start)** [D = wil.scr / ste.SCR / bil.scr]:

Script file layout (the same as in memory at `[0x6644BC]+0x46C`): `u16 offset[6000]` at file+0,
command records at file+12000+offset. Each record has `+0` u16 index, `+2` u16 opcode, `+4` u16 next,
and `+8` u32, which is filled at run time with what was created (Object*, arrow ...) [V 0x00476F7C,
0x00478E4E].
Object declaration (opcode 16; opcode 15 is the same with z = 255 = "on the ground"):
`+0xC/+0x10/+0x14` x/y/z (Q14), `+0x18` u16 **model**, `+0x1A` u16 rotation [V `sub_00476EA0`
0x00476ED8 / 0x00476EEA]. Kill frenzy (opcode 0x1A6, `sub_00480430`): `+0xC` u16 = index of the
declaration the frenzy is bound to.

| model | answer | evidence |
|---|---|---|
| **286 (0x11E)** | **KILL FRENZY badge** | [D] every opcode-0x1A6 command whose target is an object points at a model-286 declaration: wil 16/16, bil 15/15, ste 15/15 (14 of them via opcode 15). The first one is at (130.5, 109.5) = the log's 286@130,110, and wil declares exactly 16 (log x16) |
| 266 (0x10A) | **token** (confirmed) | [D] wil declares 49 (log x49), bil 50; this matches the 50 collected flags at obj manager+0x20 [V 0x00484E09] |
| 176 / 178 / 180 → 177 / 179 / 181 | gang phones | [D] the script declares 176/178/180 (3/6/6). The log has 177/179/181 with the same counts, so a phone switches to the odd model at run time (all in 0xB0..0xB5) |
| 294 (0x126) | script object, 16 in each of wil/ste/bil | [D] not bound to any KF command; meaning open (the same count in all three cities suggests a fixed per-city set) |
| 278 (0x116) | **not** declared by the script | [D] made by game code (22 in wil); meaning open; not a KF |
| 200..209, 228..239 | collectables (weapons / power-ups) | [V range]; [D] wil's script declares only 230..239, so 200..209 come from elsewhere (e.g. generators) |
| 2, 25 | script objects (wil 9 / 73, bil 0 / 42) | [D] decor or triggers, meaning open; ignore on the radar |
| 122, 170, 255, 258 | made at level start by `sub_004C3C70` (0x004C3D66, 0x004C3DE7, 0x004C43DF, 0x004C46C3 ...), in the traffic-light code family (`sub_004C30A0`, "Too many traffic lights") | [V creation sites] [I: traffic lights / junction parts; 170 and 255 both x154 = one pair per light]; ignore |
| 161 | made by `sub_004C4F30` from script commands (`sub_00477A00/AC0`, `sub_0047EDB0`); car collision → `[0x67280c]` `sub_004C4FE0` with obj+0x27 | [V] meaning open; ignore |
| 167, 169 | made in pairs by `sub_0044CC30` / `sub_0044CDD0` (0x0044CD67 / 0x0044CD95); car collision with 167 → `[0x5E9378]` `sub_0044D180` | [V] [I: doors (two halves), script user 0x00477D72]; ignore |

### 3.3 Kill frenzies

Starting a frenzy is a script command (opcode 0x1A6, `sub_00480430`): it sets
**`byte [[0x6644BC]+0xC1E2D]` = 1** (frenzy running; `sub_00475A30` / `sub_00475A40`) and shows `kfstart`.
What triggers it (a pickup object, entering a given car) is decided by the mission script, not by an
object flag [V: the KF command reads only script data]. A vehicle-bound frenzy can therefore not be
read from the object pool alone.

**Car-bound frenzies** [D]: the other 0x1A6 commands point at **opcode 426 (0x1AA) = parked car
declarations** (`+0xC/+0x10/+0x14` x/y/z, `+0x18` rotation, `+0x1C` u16 car model): wil 4 (car models
0x36, 0x36, 0x11, 0x38), ste 5, bil 5. At run time: for each command with opcode 0x1A6, take
`t = cmd(u16 cmd+0xC)`. If `t+2` is 16 or 15 it is a badge (Object* at `t+8` [I]); if `t+2 == 426` it
is a frenzy car (Car* at `t+8` [I], the same run-time pointer pattern as for objects).
Open: which flag marks a frenzy as done (the badge object is freed; the car stays).

---

## 4. Fixed locations

| place | runtime source | how to read |
|---|---|---|
| **Church / "Jesus Saves"** | mission script command **opcode 0x1B6** (`sub_0047FE80`, dispatch `sub_004805B0`, table 0x480C50 index 0x18D) [V] | trigger rect centre `cmd+0x0C` x, `+0x10` y, half size `+0x18` / `+0x1C` (Q14) [V 0x0047FF6D-0x00480017]; costs 50000 (0xC350), refused while a mission runs |
| script commands | `[0x6644BC]` | `cmd(i) = sub_00476200(i)`: `u16 off = [s + 0x46C + 2*i]`, 0 = none, else `s + 0x334C + off`; i = 1..5999 (0x1770, loop 0x0048162D); opcode = u16 cmd+2 [V] |
| **Spray shop / bomb shop** | object model **130** (car collision `sub_004293D0` → `sub_00427D10`) [V] | `obj+0x26` 0xFA..0xFC or 0xFE → bomb shop (`sub_004244A0`, `bdone`); other → respray (`sub_00427CA0`, `sdone`/`cdone`, colour = obj+0x26 if valid else 0xFD) [V 0x00427D29-0x00427D56] |
| **Crane** | `[0x5E747C]`: count `+0xD98`, 0x15C each (max 10 [I: 0xD98 / 0x15C = 10]); created by script `sub_00477660` → `sub_00449810` → `sub_00449130` [V] | base object at crane+0x2C (model 0x87) [V 0x00449186]; trigger object model 137, crane index in obj+0x26 |
| crusher [I] | `[0x5E7D70]` (created next to the crane manager 0x0045CF6D, script user 0x004752AB) | trigger object model 143; layout not traced |
| **Gang phones** | objects 0xB0..0xB5 (3.2); script list of phone handles `[0x6644BC]+0xC1E32` (31 u16) [V 0x00476FB8] | the gang arrows already point at them (section 2) |
| hospitals / police restarts | map zones type 16 `restartN` / type 20 `arrestN` [D wil.gmp ZONE] | zone list at map+0x32C (enhancements.md 1.4); gang areas = zone type 14 |

Map zones hold **no** churches or garages [D: ZONE names of wil/ste/bil.gmp]. So read churches from the
script (opcode 0x1B6) and shops from the object pool, both after the script init `sub_00481400`
(level start).

---

## 5. Player

| item | source | tag |
|---|---|---|
| x / y / **z** | ped+0x1AC / +0x1B0 / **+0x1B4** (`sub_00403A00/10`, `sub_00416B50`) | [V] |
| in a car | car `= ped+0x16C`; position sprite `[car+0x50]` +0x14/+0x18/+0x1C (`sub_0041E430..450`) | [V] |
| game's own position | `sub_004A6610(player, &x, &y, &z)`: the ped, or the car being entered/left (ped+0x27C 6/7), or the camera in control modes 2/3 | [V] [I: ped+0x1AC follows the car while driving, not checked; use the car sprite to be safe] |
| heading | `sub_004361B0(ped, &u16)`: on foot `[ped+0x168]+0x40`, in a car `u16 [car+0x50]+0`, else `[0x5E5C60]`; Ang16 0..1439 | [V] (direction convention: gameplay_input_pause.md 3.2, verify once) |
| camera | logic camera `sub_00434900(player)` = player+0x90 (player+0x208 in modes 2/3); render camera `[0x5E3CC4]`; x +0x98, y +0x9C, z +0xA0, zoom +0xA4, HUD scale +0xA8 (widescreen.md) | [V] |

---

## 6. States

From gameplay_input_pause.md section 2 [V]:
* not in a game / frontend: `[0x5EB4FC] == 0` or `[0x595018] != 0`
* playing: `[game] == 1`; **paused (F6)**: `[game] == 2` (`sub_00416BC0`); single step: 0
* quit prompt: `byte [player+0x78A]`
* no control (cutscene / script): `player+0x68 != 0` or `byte [player+0x2F]`
* mission running: `[[0x6644BC]+0x344]` non-null and `[ptr] != 0` [V `sub_004C7350`; I name]
* GTA 2 has **no stats/map screen** in game: the pause is only the PAUSED panel (`sub_004C9FA0`). A big
  pause map belongs to the port's own pause menu (`gta2_pause_draw`, src/gta2_menu.c).

---

## 7. Hook point in the frame

`sub_0045A5A0` (render) → gbh_BeginScene → world → **`sub_004CA440` HUD** (0x0045A61F) → gbh_EndScene
[V gameplay_input_pause.md 7]. Inside `sub_004CA440` the order is [V 0x004CA450-0x004CA510]:

```
+0x2850, +0x2844, +0x2AF0, +0x2760, +0x27B4, +0x2840, +0x4C, HUD, +0x27B8, +0xE4 (messages), +0x7E8, +0x818,
+0x1F18 sub_004C84C0  (ARROWS)          <- radar here
+0x2A20 sub_004C96F0, +0x2854 sub_004C8A40 (big centre text, e.g. kfstart), +0x2A1C PAUSED panel,
+0x2A1F sub_004C8910, +0x2A1E quit prompt
```
* `skip_user` (0x5EAD64) skips the whole HUD.
* Radar **under** the big text, the PAUSED panel and the quit prompt, **over** the world and the
  score/wanted/pager HUD: wrap `sub_004C84C0` (draw the radar, optionally call the original arrows).
* Radar over everything: the existing wrapper of `sub_004CA440` in src/gta2_manual.c (it already calls
  `gta2_radar_draw` before the pause menu).
* Both run every presented frame, also while paused; `[0x5E3CC4]` is valid there.

---

## 8. Open

1. Model names inside 200..244 and the meaning of 8, 9, 129, 139, 141, 161, 167; the mod's numbers
   (149, 150, 10, 200-223, 228-240) are not confirmed. A one-time log of (model, x, y) over the pool
   at level start in each district would settle it.
2. Whether collected/respawning pickups stay in the pool with a hidden sprite (then `obj+4 != 0` is not
   enough; check the sprite's visible flag).
3. Car-bound kill frenzies (3.3) and the crusher manager layout (4).
4. Target type 5 and arrow target+0x20 (on-screen flag?) are named from use only.
5. Whether ped+0x1AC/+0x1B0 track the car while driving (5).

---

## 9. Cars / police

### 9.1 Car pool [V]

| what | address | notes |
|---|---|---|
| car pool | `[0x5E4CA0]` | alloc 0xE0C4 (0x00428FDE); ctor `sub_00426DB0`: `+0` free head, **`+4` active list head**, `+8` **306 (0x132) cars x 0xBC**, link = **car+0x4C** (0x00426DBE-0x00426DE4); `+0xE050` dword, `+0xE0C0` u16 |
| car physics pool | `[0x66AB7C]` | 306 x 0xB0 (`sub_00420F80`), the object at car+0x58 |
| pop | `sub_00425480` (push at the list head) / `sub_004254A0` (append at the tail) | both put the car on the **active list** (pool+4, next = car+0x4C) [V 0x0042548B-0x0042548E, 0x004254C9 / 0x004254D5] |
| create | `sub_00426AC0(x, y, z, rot, model, ...)` (wrappers `sub_00426E10`, `sub_00426E40`) | `car+0x84 = model`, **`car+0x88 = 1`** (0x00426B37 / 0x00426B3D), `car+0x50` = new sprite at (x, y, z) |

**Walk the active list** instead of the 306 slots:
`for (c = R32(pool+4); c; c = R32(c+0x4C))`. Freed cars go back to the free list, so everything on
the active list is a live car slot.
* model `car+0x84`, position sprite `[car+0x50]`+0x14/+0x18/+0x1C (Q14), heading u16 `[car+0x50]+0`
* state `car+0x88`: see 9.1b. 5 means "being removed", **not** "destroyed"; a destroyed car is
  `car+0x74 >= 32000`. Models 72..81 are named "Wreck 0..9" [D gxt], but an exploded car does not
  switch to them (9.1b).
* driver `car+0x54` (ped, 0 = empty) [V gameplay_input_pause.md 2] [I meaning].
* Hidden or not drawn: not traced. The game flags (sprite) are open; the active list holds parked and
  moving cars alike.

### 9.1b Destroyed cars (follow-up: a blown-up cop car stayed on the radar)

**car+0x88 is not a damage state.** It is the car's life/traffic state [V writes]:

| value | where | meaning |
|---|---|---|
| 1 | create 0x00426B3D; 2/3/4 → 1 at 0x00421605 (function just before `sub_00421610`) | normal (in use) |
| 2, 3, 4 | `sub_00421470` (→ 4 unless 2/3/5), `sub_00421490` (→ 3 unless 5), `sub_004241C0` (4 → 2); `sub_004214B0` tests for "2, 3 or 4" | recyclable traffic stages [I] |
| 5 | `sub_004241C0`: 2 → 5 when `sub_00424010` allows it, 3 → 5 (0x004241F1); a car in state 5 is taken off the collision list `[0x5E668C]` (0x0040FBE7, 0x00429E87) | being removed / not solid [I] |
| 6 | `sub_004241C0`: 5 → 6 and returns 1, then the car is freed (`sub_0040F490`, 0x0040FC39) | freed this tick |
| 7 | `sub_004214E0` (only from the crane code `sub_00448450`); tested by `sub_004214D0` | held by a crane [I] |

So an exploded car normally stays at 1 and passes the old filter.

**Damage** = s16 **car+0x74**, 0..**32000 (0x7D00)**. Hits add to it and it is clamped at 32000. The
damage effects use the byte car+0x8C (3 from 16000, 4 from 25000); at 31500 or more `sub_00426F60` runs.
At **exactly 32000** the car is blown up by **`sub_00426FA0`** (0x004271F0-0x00427275) [V].
`sub_00426FA0` reads car+0x84 but never writes it, so **a burnt-out car keeps its model 12/52/84**
[V: no write to +0x84 in `sub_00426FA0`; the only car write is at creation, 0x00426B37].

**The game's own "car wrecked" test** (mission script, 0x0047B834-0x0047B843) [V]:
```c
wrecked = (s16)R16(car+0x74) >= 0x7D00 || R32(car+0x88) == 5;
```
**Radar filter:** `model in {12, 52, 84} && !wrecked`, walking the active list (9.1). The police code
uses the same 32000 test to skip wrecked cars (0x004B139F); it is also used at 0x00450D40.

### 9.2 Models (names = e.gxt key `car<model>` [D]; numbers confirmed by code where tagged)

| model | name (e.gxt) | role | evidence |
|---|---|---|---|
| **12 (0x0C)** | Cop Car | police | [V `sub_0041F6E0` "is police car" = {12, 52, 84}; `sub_00421790`; wanted spawn 0x004B3B00] |
| **52 (0x34)** | SWAT Van | police | [V `sub_0041F6E0`, `sub_004217A0`] |
| **84 (0x54)** | Special Agent Car | FBI | [V `sub_0041F6E0`, `sub_00411920`; wanted spawn 0x004B3AF6] |
| **22 (0x16)** | Armed Land Roamer | army | [V `sub_00411910`; wanted spawn 0x004B3AEC] |
| **54 (0x36)** | Tank | army | [V `sub_00411900`; army random spawn 0x004B3B88; also a KF car 3.3] |
| **3** | Pacifier | army [I: APC] | [V army random spawn 0x004B3B81] |
| **30 (0x1E)** | Land Roamer | army (default of the random pick) | [V 0x004B3B8F; `sub_004217B0`] |
| 17 | Fire Truck, 34 Medicar | emergency (not law) | [D gxt] |

Wanted-level dispatch `sub_004B34E0` (turns a traffic car into a law car, 0x004B3AAD-0x004B3B94) [V]:
it uses the value `[[0x66B7A0]+0x65C]`. When the chosen car is a Cop Car and the police-car count
`+0x658` is below `+0x659`, value 3 → Cop Car, 4 → Special Agent Car, 6 → Armed Land Roamer.
Otherwise, at value 6, it rolls 0..6 through table 0x4B49F0 → 0..3 Pacifier, 4..6 Tank, out of range
Land Roamer. [I] +0x65C is the wanted level (enhancements.md uses +0x654 for the ped cap; the two are
not reconciled).

For the radar: **law car = model in {12, 52, 84, 22, 54, 3}** (30 Land Roamer only when you want to
include the army jeep, since the plain Land Roamer is also a civilian car [I]); skip wrecked cars
(9.1b: `car+0x74 >= 0x7D00 || car+0x88 == 5`).

### 9.3 Chasing vs parked (optional, cheap)

* `car+0x54 != 0` = someone is driving [V field, I meaning]; a parked police car has no driver.
* A police driver is a class-4 ped (ped+0x238 == 4, enhancements.md 1.2) [I]. A cop car on the active
  list with a class-4 driver is a police unit. Whether it is chasing the player (police objective) is
  not traced.
