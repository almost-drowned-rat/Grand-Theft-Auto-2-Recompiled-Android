# Car lights (sprite deltas) and damage

2026-11-27, for the DAY / NIGHT cycle (headlights off in the day; maintainer: "the lights go out when you ram
something head-on - use that behaviour"). [V] read in the disassembly, [I] inferred.

## Delta data and the car texture cache

- Style object `[0x670684]`; chunk dispatch near 0x4C0705 / 0x4C072A [V]. DELS -> `sub_004BFAB0` (raw store at
  style+0x48); DELX -> `sub_004BFA60`: `sub_004BF840` index at style+0x50 ({u16 sprite, u8 count, pad,
  count x {u32 ptr into DELS, u16 size, pad}}), `sub_004BF980` per-sprite pointer table at style+0x54, `sub_004BF900`
  offsets -> pointers [V].
- `sub_004BF230(sprite, delta)` returns one delta entry; only caller `sub_0044AEA0(sprite*, pixels, mask*, remap*)`
  [V], which walks the mask: bits 0..21 apply delta `bit` (`sub_004C0290`, row stride 256), bits 22..31 apply delta
  `bit-17` mirrored left / right (`sub_004C02D0`) - the right-hand light is the mirrored left one [V].
- Cache `sub_0044B0C0` (ecx = mask*), callers only 0x4BE293 / 0x4BE4A6 in `sub_004BE060` [V]. Object `[0x5E85C0]`:
  48 entries x 0x14 (slots 0..31 for sprites up to 64 px high, 32..47 taller): +4 sprite, +8 mask, +0xC LRU stamp,
  +0x10 texture slot, +0x12 pass. Lookup `sub_0044AD90`: exact (sprite, pass, mask) reused; an entry whose mask is a
  subset is patched in place with the missing deltas; else the LRU entry is rebuilt (base copy `sub_004C0380` /
  `sub_0044AD50`, all deltas, upload `[0x671978]` `sub_004C2C80` [I]) [V].

## The car's delta mask car+8

Checked against the pixel boxes in wil.sty (cars face down in the sprite) [V for sprites 0 and 3]:

| bit | delta |
|---|---|
| 0 / 1 | rear right / rear left dent |
| 2 / 3 | front left / front right dent |
| 4 | windscreen |
| 5 / 22 | left / right brake light |
| **6 / 23** | **left / right headlight** |
| 7..10 | left door frames [I] |
| 11..14, 28..31 | pop-up headlight frames, cars with car info +0xD flag 0x02 [I name] |
| 15 | roof light (car info +0xC flag 0x04) |

- Lights on `sub_00425590`: bit 6 unless bit 2 (front left damaged), bit 23 unless bit 3; pop-up cars 11 / 28; roof
  light 15 [V]. Off `sub_00425650`: clears 6, 23, 11..14, 28..31, 15 [V]. Pop-up animation `sub_00423C30` steps
  11->12->13->14->6 (28->..->23) every 3rd tick [V]. Brake lights `sub_004213D0` (5 unless bit 1, 22 unless bit 0) /
  `sub_00421430` [V].
- Not time of day: car state car+0x9C in `sub_00426030` - state 4 -> `sub_00425E30` (lights on, state 3), states 2 / 5
  -> `sub_00425E60` (off) [V]. Observed in play (maintainer 2026-11-27): a parked car has no headlights; they come on
  only when the car is given gas (not when someone gets in) - so state 4 is most likely the first throttle [I]. car+0xA4 light flags: 0x2 headlights
  wanted, 0x1 brake, 0x8 / 0x1C flashing [V]. car+0x84 = model; car info = `[[style+0x5C] + 4*model]` [V].

## Damage per corner

`sub_00422F00(corner)` (jump table 0x4230BC) [V]: 0 front left sets bit 2, clears 6 + 11..14; 1 front right sets 3,
clears 23 + 28..31; 2 rear left sets 1, clears 5 (after `sub_00422980`); 3 rear right sets 0, clears 22 (after
`sub_00422A40`); 4 windscreen sets 4. Callers: collision `sub_00423180` (hit zones 0..3 -> corners 3 / 2 / 0 / 1)
and 0x42A3C6..0x42A420 (every corner not yet damaged, tested by `sub_004230D0`) [V]. The light-on functions skip
damaged corners, so a frontal crash puts that headlight out for good; repair `sub_00425F20` clears bits 0..4 and turns
the wanted lights on again [V].

## Drawing

`sub_004BE060` (one sprite): car when `[spr+0x30] == 2`, car = `[spr+8]` [V `sub_0040FEB0`]. It saves car+8
(0x4BE224) and restores it (`sub_004A51A0` at 0x4BE2E4 / 0x4BE518) [V]. With Screen\lighting on (`[0x595011]`) it
draws two passes: the body without the light mask (`sub_004BA340`), then only the light deltas (`sub_004BA330`, flag
0x80); an empty light mask makes `sub_0044B0C0` return 0xFFFF and the pass is skipped [V]. Light mask `[0x5E85BC]`:
0xC78060 normal cars, 0xC00060 roof-decal cars, 0xC70060 model 0x54 [V].

Car lights are only these deltas - no `gbh_AddLight` (only caller 0x4614CA, per-cell list `[0x6622B4]` filled by
`sub_00469010` / `sub_00482D30` from map lights, objects and movers, none in the car code) [I].

## Bits 15..18, sirens, roof lamps (follow-up)

- Siren flasher `sub_00423B80` (light tick `sub_00426140` while car+0xA4 & 0x04): phase A bit 16 (`sub_00422AA0`) +
  18 (`sub_00422920`), phase B bit 15 (`sub_00422BD0`) + 17 (`sub_004229E0`); siren off `sub_00422D80` clears 15..18
  [V]. Siren flag only on siren cars: `sub_00414F20` = car info +0xC & 0x02, or model 0x54 [V].
- 17 / 18 = rear emergency lamps (right / left), cars with +0xC & 0x80, only while corner 0 / 1 is undamaged [V];
  Medicar d17 / d18 at the rear [V data].
- Bit 15 by car type [V]: siren car (+0xC & 2) = siren phase B; +0xC & 4 (bus, limos, tow truck, Karma bus) = roof
  lamp, on with the headlights (`sub_00425590`); +0xC & 0x20 (Taxi 56, Taxi Xpress 51) = taxi sign, on while
  car+0x9C == 3 and car+4 == 0 (`sub_00423B50`); +0xC & 0x40 (GT-A1 19, TV Van 67) = painted decal set for good at
  init (0x426C32) - so decal cars get light mask 0xC00060.
- Pop-up frames 11..14 are the headlight only on cars with +0xD & 2 (Stinger 46); elsewhere 11..14 are rear doors.
- U-Jerk Truck = model 69 (car info +0xC / +0xD = 0), sprite 62 [I index]: d6 headlight 6 px, d15 a lamp strip
  nothing ever sets; its visible front lamps are painted into the base sprite (white under d6, orange under d15 [I,
  own palette decoding]) - no mask bit removes them.

## DAY / NIGHT (port)

`src/gta2_manual.c` wraps `sub_004BE060`: in the day (`gta2_daynight_lights_off`, cycle ON, 08:00-17:00) car+8 loses
bits 6, 23 (and the pop-up frames) around the original call and gets its real value back after it - draw time only,
the simulation never sees it. With lighting on, the body pass key is unchanged and the light pass just loses the
headlights; with lighting off each visible car needs one cache rebuild at the switch (once, not per frame).
