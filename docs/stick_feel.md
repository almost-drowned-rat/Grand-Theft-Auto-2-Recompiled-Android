# ANALOGUE CONTROLS, SPRINT and ENDLESS SPRINTING

Contributed by maverick-hunter (pull request #7). In v0.9.0 the ANALOGUE scheme is split into its parts (maintainer
2026-11-28), each scheme keeps its own buttons: analogue walking is always on (the stick); the pedals are analogue when
gas / brake are mapped to R2 / L2; RECOMP OPTIONS > ENHANCEMENTS has ANALOGUE STEERING (a page: on / off, preset, dead
zone, curve, smoothing), PUNCH (MODERN's X on foot by default, CUSTOM MAPPING moves it), TURRET AIM and CAMERA PAN, all
off by default and single player only; the sprint is ENHANCEMENTS > SPRINT for every scheme. The d-pad weapons / radio
and the right-stick vehicle fire are not taken over (a player can map them). The text below describes the PR as it came.

Not playtested by its author beyond the notes below; numbers are picks, not measurements of any GTA.

## ANALOGUE

(Originally a fourth CONTROLLER scheme after MODERN; now the ENHANCEMENTS switch, see above.) MODERN itself behaves as it
did before. The alternate scheme is MODERN plus a GTA V style layout:

* Cars: R2 is the gas and L2 the brake / reverse, read as analogue pulls; the right stick fires (see below); the left stick steers
  smoothly; d-pad left / right change the radio. L2 is no longer the camera (no button for now).
* On foot: the left stick walks with a heavy, analogue feel (speed follows the push, slow turns, momentum while sprinting);
  d-pad up / down change weapon (the d-pad no longer walks); R2 shoots, Circle punches, Square jumps.
* One fixed feel: the former HEAVY preset on foot, the former SMOOTH preset for car steering (dead zone 0.10, curve 1.6, smoothing 2). CONTROLS > ANALOGUE SETTINGS (listed right under CONTROLLER only while CONTROLLER = ANALOGUE) has, under DRIVING, a PRESET line (SMOOTH 0.10 / 1.6 / 2, SNAPPY 0.08 / 1.2 / 1, HEAVY 0.12 / 2.0 / 2) and the three numbers it sets: DEAD ZONE, CURVE, SMOOTHING (= ticks a steering pulse is held). Changing a number by hand shows CUSTOM, or the preset it happens to equal. Other numbers: `saves/settings.ini` keys `feel_<name>` (names in
  `src/gta2_feel.h`) and `feel_pedal_dead / full / curve / floor` (defaults 0.05 / 0.80 / 2.0 / 0.30).

How: on foot the game's walk command (`player+0xC`) is only compared with zero, so the stick's push scales the speed
through the per-call hook SPRINT uses (`sub_0041FC20`). Cars have a left / none / right steering byte (`phys+0xAD`), so the
stick's value is turned into a pulse train (error diffusion). The triggers are added to the held forward / back bytes
(`player+0x78 / +0x79`) before the car step; after it `phys+0x91` (the gas) is pulsed so its on-share follows the pull.
Car physics `+0x93` is never written.

## Right stick and camera pan

On foot the right stick aims. In a car, up / down fire the vehicle's weapon (a car's attack is not aimed; GTA 2 has no hand-held
drive-by), so L1 / R1 are previous / next weapon in both situations. Holding the SPECIAL 2 button (Select by default) turns the
stick, in both situations, into the four direction presses of the game's own Special 2 look-around (`sub_004A4CF0`, called from
the player step `sub_004A7E80` before it picks driving or walking), so the pan has eight directions; aim and fire are off
meanwhile. Letting go re-centres the view. Special 1 stays on R3.

In the three turret vehicles (the game's own checks `sub_004118F0` / `sub_00411900` / `sub_00411910` on the car) the right stick
points the turret at the stick's direction and fires while it is out (past 0.3). The game only turns the turret one fixed step a
tick (`sub_00423720`: Special 1 held + left / right; the step is the u16 at `0x5E4D34 / 0x5E4D60 / 0x5E501C`; left adds it to the
turret's Ang16 at component+0x10, right subtracts it; the component is found in the car's list by type id 0x72 / 0x94 / 0xF8 with
`sub_004BE980`), so each tick the angle still to go decides left, right or nothing. The turret angle is assumed relative to the car's
heading (`u16[[car+0x50]]`); if it is the world's, `turret_world = 1` in `saves/settings.ini`. The horn edge byte is never touched. The turret's zero is half a turn from the stick's, so 720 is added. CONTROLS > ANALOGUE SETTINGS > TURRET AIM (on by default) turns all of this off, leaving the game's own Special 1 + left / right.

The pan follows the stick directly: after each tick the look-around offsets (`cam+0x48 / +0x4C`, cam = `player+0x90`) are set to
the push times the game's own limit (`[0x5E3CD0]`, `[0x5E3E74]`), so there is no acceleration lag and no drift. The sign of each axis
is learned from the game's own first movement while the stick is pushed (logged as `pan: axis ... sign`). Four arrows at the screen edges show while the pan
button is held; the one in the direction pushed gets brighter.

## PUNCH (experimental)

Circle presses the game's attack with the weapon put away for the player ped's tick (`player+0x788 = 0xFFFF`, the ped's
cached weapon `ped+0x170` cleared; both restored after), so the fists are used and the HUD and ammo never see it. The game
could still fire a weapon on some ticks, so while Circle is held the current weapon's cooldown byte (`weapon+2`, which the
fire functions `sub_004CE6D0` / `sub_004CE970` test first) is also kept above 0 (`gta2_punch_hold`). That last part is a
hypothesis from reading two fire functions; if the weapon still fires, `data/gta2.log` has `punch:` lines to look at.

## SPRINT (stamina bar: ANALOGUE only)

Every scheme keeps the original sprint (GAMEPLAY > SPRINT): 1.5 s at 1.4x, then 10 s before the next. With CONTROLLER =
ANALOGUE, STAMINA SPRINT (CONTROLS > ANALOGUE SETTINGS, under ON FOOT, ON by default; OFF gives the original sprint back) replaces it with a
stamina bar. SPRINT PRESET (same page) picks its numbers: **1** (default, the maintainer's for v0.9.0 - GTA 2 had no
sprint at all, it stays a short burst): a full bar lasts 2.5 s of running at 1.4x, refilling starts 2.5 s after you stop,
empty to full in 8 s standing still (16 s while walking); **2** (this PR's own): 3.5 s, after 0.5 s, 3 s (6 s). The bar
drains only while the ped walks. Running dry locks the sprint
until the button is released and the bar is full. SPRINT BAR (same page, ON by default) draws the bar at the top centre, in
10 segments, while sprinting and refilling, fading out 1.5 s after it is full.

## ENDLESS SPRINTING (CHEATS)

Below the other ENDLESS cheats, off by default. With the stamina bar it never drains; with the original sprint it is a plain
hold with no burst limit or rest. It works with every scheme.
