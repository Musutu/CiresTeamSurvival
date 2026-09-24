# World and combat polish playtest

This is a development prototype checkpoint, launched September24 at05:47UTC. Balance simulations have been stopped at the user's request. This session is for seeing the game, movement, equipment and targeting in action.

## Start

Open `PlayTripoPreview.cmd`. Select a champion from the roster. For a quick full-skill preview, pick Iron Warden, Ash Ranger, Veil Scholar, Lancer or Rift Summoner, press **F8**, and choose **Load complete champion kit**. F8 closes developer tools. This replaces that match's draft with a level24 test kit.

## Controls

These are the WoW-style defaults. Every key except Escape, the mouse buttons and the wheel can be rebound (see `Keybindings.md`); on-screen key hints follow your bindings.

| Action | Control |
|---|---|
| Move forward / backpedal | W / S |
| Turn character (strafe while right mouse held) | A / D |
| Strafe left / right (no turning) | Q / E |
| Autorun | Num Lock |
| Orbit camera without turning | Hold left mouse and drag |
| Steer: camera and character turn together | Hold right mouse and drag |
| Run forward | Hold both mouse buttons |
| Zoom camera | Mouse wheel (over the world) |
| Jump | Space |
| Dodge roll | Ctrl |
| Walk/run | Caps Lock |
| Select unit | Left click (without dragging) its body or unit frame |
| Select yourself | F1 |
| Cycle enemies (in front of camera, nearest first) / reverse / allies | Tab / Shift+Tab / F |
| Toggle auto attack | T |
| Action bar 1: abilities / ultimate | 1–6 / R (bar 2: Shift+1–6, bar 3: Alt+1–6) |
| Recall to town (prep/recovery) | G |
| Shop / help / chat | B / H / Enter |
| Confirm aimed ground skill | Left click valid ground |
| Cancel aimed skill | Escape or right mouse |
| Developer quick start | F8, or the button beneath the minimap |
| Options / layout editor | F9 / F10 |

Ground abilities show AIM in the skill bar. Green placement is valid; red indicates a placement restriction. Targeted skills distinguish SELF, ALLY and ENEMY. Ally heals can fall back to self as explained in their tooltip. F9 → Controls has optional quick ground casting. Selected units display their basic attack range; spell ranges can differ.

## Five-minute inspection

1. Walk along the winding lane and inspect the new barrels, crates, braziers, arches, stairs, trees, obelisks and lamps. Routes and the defended entrance remain clear.
2. Select yourself, a teammate and a monster. Check their highlight, circle, range and unit-frame relationship.
3. On Ranger, compare the bow/crossbow previews in F8. Attack from distance and check projectile/hit feedback. Warden has a sword and shield; other class loadouts include staffs, daggers, axes and lances.
4. Aim a ground skill with its key, move the cursor, confirm or cancel. Check its warning boundary and active effect.
5. Walk, strafe, jump and dodge. Dodge costs25 energy with a3.5s cooldown; only a brief part of its0.55s motion avoids damage. Tune these values under F8 → Movement.

## Known visual limits

- Character and weapon grips, Bear rigging and Centaur procedural motion remain prototype work; the Bear now uses the actual Bear body.
- Spell effects have new layered light, trails and runes, but are not a final AAA effects package.
- Complete bespoke test kits currently cover the five core champions. The wider roster uses role-filtered skill drafting.
- Three newly generated Tripo landmarks (gatehouse, watchtower, shrine) are ready online; their direct export/import handoff is pending. They are not included until saved Unreal receipts are verified.

Progress and technical evidence are recorded in `Checkpoint-20260924-WorldCombat.md`.
