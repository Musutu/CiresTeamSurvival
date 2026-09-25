# Monster expansion: every pack creature, Rare Spawns and the Bonus Loot Wave

monster-expansion (branch `feat/monster-expansion`). Eric's request (2026-09-25): use every creature model in the installed
packs (the cartoon Undead zombie included), reskin what does not fit, and add a **Rare Spawn** with better loot and an
occasional **Bonus Loot Wave**. Only installed assets are used; nothing was downloaded.

## Inventory (Tools/InventoryCreatures.py)

`Tools/InventoryCreatures.py` (commandlet) lists every skeletal mesh in the pack folders with its skeleton, materials and clips,
and marks the ones the committed mapping data already uses. Report: `Saved/CreatureInventory.json`.

| Pack | Creature mesh | Before | Now |
|---|---|---|---|
| Undead Pack | SkeletonEnemy `SK_Skeleton` | hollow_infantry (sword set) | + **Bone Archer** (bow set, helmet/armour/quiver/belt/rag parts, the pack bow on `hand_l`) |
| Undead Pack | Ghoul `SK_Ghoul_Full` | hollow_infantry alternate | unchanged (the loose Ghoul parts are the pieces `SK_Ghoul_Full` already merges) |
| Undead Pack | EnemyGoblin `SM_EnemyGoblin` | unused | **Treasure Goblin** (gold reskin; Bonus Loot Wave) |
| Undead Pack | Zombie `SK_Zombie` | unused (rejected as cartoon) | **Rotting Shambler** (grave-green reskin; Hollow Legion line variant) |
| Undead Pack | Lich `SK_Lich_Full` | unused | **Lich Revenant** (Rare; Fallen Order caster variant) |
| Khornes Monster | `SKM_Monster` + `SM_Monster_Sword` | unused (locomotion-only pack) | **Horned Brute** (Rare; Ironhide bruiser variant) with its blade |
| ROG Creatures | Wolf `SK_Wolf_Fur_Full_W` (white coat) | unused | **Frostfang Alpha** (Rare) |
| ROG Creatures | Deer `SK_Deer_No_Fur` | unused | **Gilded Stag** (gold reskin; Bonus Loot Wave) |
| Quadruped Fantasy | Griffon `SK_Griffon` | unused | **Storm Griffon** (Rare) |
| Quadruped Fantasy | MountainDragon `SK_MOUNTAIN_DRAGON` | unused (9 m, rejected) | **Cinder Drake** (scaled to ~5 m, charred-ember reskin; Rare) |
| Quadruped Fantasy | Centaur sword set (`SK_Sword_Action`, `SK_Sheath`, `SK_Mustaches`) | unused | **Centaur Blademaster** (Feral Kin bruiser variant) |
| ROG / Quadruped | Bear, Boar, Mammoth, grey Wolf, Barghest, Centaur (bow set) | used | unchanged |

Not creatures (reported, not used): the `_No_Fur` / `_Fur_Top` LOD meshes of the ROG bear, boar and mammoth, the Barghest mane
and tail pieces (its full mesh already carries them), the centaur's alternative beards and idle bow/sword props, the
Polyphoria heavy-armour humanoid kit and the demo mannequins shipped in GDH Bundle, Castle Town and the Khornes pack.
No gap needs a new Tripo body.

## Bodies and reskins (Tools/BuildFabExpansionCreatures.py)

The commandlet writes `Content/Data/RaceMeshes.fabx.json` (same shape as `RaceMeshes.fab.json`, read right after it by
`CireMonsterArt`) through the proven native monster path: facing, head height -> `meshScale`, reach and walk/run ground speed
from `Tools/BuildFabCreatures.py` `measure()`, in-place clips with the root locked. Additions:

- **Retargeted clips.** The Khornes monster ships only locomotion; the Undead skeleton's sword set (same UE4 bone names) is
  retargeted onto it for the strikes, flinch and death. The skeleton archer's pack bow shot lives on the bow's own skeleton,
  so the GDH archery loose (UE5 Manny) is retargeted onto the skeleton. Route: `Tools/RetargetTripo.py` auto IK rigs +
  chain-to-chain retargeter, output in `/Game/FabDerived/Expansion/<unit>` (local only).
- **Reskins.** A body row may carry `"reskin"`: the vendor base colour / normal (found from the material's parameters, or named
  in the tool) feed `M_CireMonsterSkin` on the listed slots at runtime, recoloured by `tint` / `tintStrength`, with a rim
  (`rim`, `rimStrength`). `CireRaces::ApplySkin` handles it (the armour mask is off for vendor maps), so ranks, the Rare colour
  and the Bonus colour recolour these bodies like Tripo ones. Masked fur / feathers keep their vendor materials (the stag uses
  the fur-less deer mesh, the griffon keeps its feathers).
- **Attachments.** `"attachments": [{mesh, socket}]` hangs a skeletal prop on its own skeleton from a bone (the archer's bow).
- **Props with grips.** The brute's blade is an archetype prop; `WeaponGrips.fabx.json` (same tool, `MeasureFabWeapons.measure`)
  gives it a handle, so the fist closes on it. `CireGrip` reads the `.fabx` file after `.fab`.
- **Spectral.** `"spectral": true` (the lich): it hovers on its cloak; its death rises, and the corpse sinks away.
- The drake prowls on its walk cycle at every speed (its gallop dives the head to the ground); the griffon walks and gallops on
  the ground (its flight set stays unused: monsters walk the lane).

Run: `UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/BuildFabExpansionCreatures.py -unattended -nullrhi
[-CireExpansionOnly=unit+unit] [-CireExpansionKeep]`. Marker `CIRE_FAB_EXPANSION_PASS`; report `Saved/FabExpansion.json`.

## Units (Content/Data/Bestiary.json, Tools/AuthorBestiary.py)

`Tools/AuthorBestiary.py` writes `Bestiary.json` (full NPC archetypes, merged after `Races.json` by
`CireMonsterExpansion::MergeInto`) and `AudioCues.expansion.json`. Every unit names a `fallback` body so a clean clone (or
`-CireNoFab`) draws an existing body.

| Unit | Kind | Role | Fallback | Skills (pool) | Sounds (pack) |
|---|---|---|---|---|---|
| Treasure Goblin | bonus | bruiser (never attacks) | hollow_infantry | none | Whoosh / Cute Item Collect / Cute Star Burst |
| Gilded Stag | bonus | bruiser (never attacks) | grave_hound | none | Whoosh / Punch / Positive Magic + Body Fall |
| Rotting Shambler | hollow line variant, every 3rd | bruiser | hollow_infantry | Grave Grasp (root cone), Festering Rot (pool) | Blood & Gore / Blood Drop / Body Fall |
| Bone Archer | hollow ranged variant, every 2nd | ranged | barbed_hunter | Bone Volley | Arrow Shot / Bone |
| Centaur Blademaster | feral_kin bruiser variant, every 3rd | bruiser | wild_outrider | Trample, Blade Sweep | Metal Whoosh / Punch / Body Fall |
| Horned Brute | rare + ironhide bruiser variant, every 3rd | bruiser | ironbound_bruiser | Horned Cleave, Leap Slam, Frenzy | Metal Weapon Clash / Generic Hit / Body Fall |
| Lich Revenant | rare + fallen_order caster variant, every 4th | caster | blight_caster | Grave Nova (root), Soul Rend, Unholy Mending | Dark Magic / Negative Magic |
| Storm Griffon | rare | bruiser | dire_wolf | Diving Pounce, Talon Frenzy | Whoosh + Stab / Punch / Body Fall |
| Cinder Drake | rare | bruiser | ember_whelp | Cinder Breath, Ember Spit | Fire Magic / Generic Hit / Fire + Body Fall |
| Frostfang Alpha | rare | bruiser | dire_wolf | Pounce, Frozen Hamstring, Winter Howl | Stab / Frost Magic / Frost + Body Fall |

Race variants apply only in live waves (`ACireGameMode::SpawnWave` -> `StartWave(Mode, true)`), counted per wave and race
slot, so developer/test starts stay deterministic. No monster skills before `skillProgression.firstSkillWave` still holds.

## Rare Spawn (Waves.json "rareSpawn")

```jsonc
"rareSpawn": { "enabled": true, "chance": 0.3, "fromWave": 2, "maxPerCycle": 2,
               "health": 1.8, "damage": 1.2, "size": 1.15, "bounty": 5,
               "pool": ["lich_revenant", "storm_griffon", "cinder_drake", "frostfang_alpha", "horned_brute"] }
```

- **When.** From global wave `fromWave`, each Normal / pack / Custom wave rolls `chance` (seeded per match and wave, both
  lanes get the same rare) until `maxPerCycle` rares have come this cycle. Armored, escort, boss and bonus waves never roll.
  A wave row can also be a fixed rare (`"rare": true`, the `$` flag in F8).
- **Look.** At least Elite rank (+1 skill once skills unlock), then `health` / `damage` / `size` on top (about 4x the health of a mob of its wave, so a rare adds roughly half a wave of work). It wears the Rare
  colour (cyan `#33f2ff`, `Bestiary.json specialColors`) on its skin, rim, name plate and target frame, an arcane aura
  (`FabVFX.json` school aura, tinted), and the plate reads "Rare Storm Griffon". A `RARE SPAWN` banner and the
  `sting.rare` cue (Fantasy UI dark alert / bell) announce it to its lane.
- **Reward.** `bounty` mob values to every teammate (a normal mob pays 1) and a **personal chest** for every eligible player
  (`LootTables.json` `sources.rareSpawn`: `rare_cache` from round 1, `rare_hoard` from round 3). Contributors are recorded
  like bosses, so eligibility follows the personal-loot rules.

## Bonus Loot Wave (Waves.json "bonusWave")

```jsonc
"bonusWave": { "enabled": true, "chance": 0.4, "fromWave": 2, "maxPerCycle": 1,
               "extraBreatherSeconds": 6, "escapeSeconds": 26, "fleeRadius": 950, "bounty": 4,
               "wave": { "label": "Goblin Hoard", "type": "bonus_loot", "units": [
                 { "archetype": "treasure_goblin", "count": 3, "health": 0.8 }, { "archetype": "gilded_stag", "count": 1, "health": 1.1 } ] } }
```

- **When.** After a cleared wave (never the cycle's last, so prep is never delayed), from `fromWave`, `chance`, at most
  `maxPerCycle` per cycle. The creatures spawn at the rift (Eric's ruling) during the breather.
- **Behaviour.** They never attack and never cost lives. They trot down the lane; a champion of their lane within `fleeRadius`
  makes them bolt away (back up the road) at +15% speed. After `escapeSeconds`, or on reaching the gate, they escape with a
  puff and the `bonus.escape` jingle. They never block the next wave (`mustClear` false). Slows and roots slow their escape; while a bonus wave runs the Skill Shop does not auto-open (its key still opens it).
- **Pacing.** The breather grows by `extraBreatherSeconds` (6 s) only when a bonus wave runs: at most one per cycle, 40%
  chance, so about +2.4 s per 8-minute cycle on average. Ready-up still ends the breather early; uncaught creatures just flee.
- **Telegraph.** A `BONUS LOOT WAVE` banner with the `sting.bonus_wave` cue and the announcement line; every creature wears the
  gold Bonus colour and a holy aura.
- **Reward.** `bounty` mob values per creature to every teammate, plus a personal purse (`sources.bonusWave`: `bonus_purse`,
  `bonus_hoard`). A caught creature plays `bonus.caught` and a gold burst.

### Economy (mob value v; a normal mob pays 1 v to every teammate)

| Source | Gold per player | Loot |
|---|---|---|
| Normal wave (7-9 mobs) | 7-9 v | none |
| Rare Spawn | 5 v bounty + 4-6 v chest (6-9 v from round 3) | 60% item (+20% epic pool), 45% tome |
| Bonus Loot Wave (4 creatures, all caught) | 16 v bounty + 8-12 v purses | 30% consumable + 8% basic item per creature |
| Challenge pack (for scale) | 10 v per unit + 100 v leader + cache | pack tables |

A caught bonus wave is worth roughly three normal waves and a rare about one and a half, both far below a challenge pack, so
they feel rewarding without moving the shop curve.

## F8 -> Waves

- **TYPE** cycles to *Bonus Loot*; **TEMPLATE** fills the goblin hoard. A Bonus Loot wave placed in the cycle plays like the
  occasional one (fleeing, no lives, `mustClear` off).
- Row flag **$** marks a fixed rare row.
- The bottom row: **RARES** on/off, **RARE %**, **RARE FROM**, **RARE $ (MOB)**; **BONUS** on/off, **BONUS %**, **ESCAPE**,
  **BONUS $ (MOB)**; **EDIT BONUS** switches the composer to the bonus wave's rows (unit cycles the bestiary creatures);
  **SPAWN BONUS** starts it now. SAVE JSON / APPLY LIVE cover the new blocks.

## Fallback and switches

- Packs missing, `-CireNoFab` or `-CireNoFabCreatures`: the units draw their fallback bodies (race palette + rank/Rare/Bonus
  colours on the Tripo skin), the creature cues fall back to shipped sounds, auras are skipped when no Fab VFX is installed.
- `Bestiary.json` missing or invalid: logged (`CIRE_BESTIARY_DATA_ERROR`), the rare pool and bonus rows drop unknown units,
  and the waves keep running.

## Tests and renders

- Native (`CireMonsterExpansion::RunSmoke`, in the NPC smoke of `-CireCombatExpansionProbe`): creatures, fallbacks, cues,
  Fab bodies (all ten with the packs), variants, the deterministic rare roll (about 30% of eligible waves, caps, wave types),
  a live forced rare (plate, elite, colour, toughness, bounty, personal chest, banner) and a forced bonus wave (count, no
  block, bounty, flee, escape without lives, gate never leaks, personal purse, banner). Marker `CIRE_MONSTER_EXPANSION_PASS`.
  `CIRE_MONSTER_ART_PASS` covers the new bodies' grounding, heights, compactness and stride.
- Gallery: `python Tools/RunMonsterGallery.py --only bestiary,creature_,rare_look,bonus_look` -> `Saved/MonsterGallery/<stamp>/`:
  the lineup next to a hollow infantry, each creature at idle / walk / attack contact / death, the Rare look and the Bonus look.
