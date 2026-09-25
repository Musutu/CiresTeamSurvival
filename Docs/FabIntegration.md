# Fab integration: how the purchased packs plug in

The purchased packs are listed in `Docs/FAB-PURCHASED.md`, which also has the Add to Project checklist. This page covers how each kind of pack reaches the game.

The rule for every pack: it is licensed and must never be committed, because the repo is public. Every reference to it is therefore optional, and a clean clone builds, runs and passes every test on the Tripo, procedural and free fallbacks.

## Storage

| What | Where | Committed |
|---|---|---|
| Pack content | `CiresTeamSurvival/Content/<PackFolder>` (main checkout), junctioned into each worktree by `Tools/LinkFabContent.py` | never |
| Derived assets (retargeted clips, IK rigs) | `Content/FabDerived/...`, junctioned the same way | never |
| Mapping data (object paths and timing only) | `Content/Data/FabVFX.json`, `FabAnimations.json`, `RaceMeshes.fab.json`, `Art/Fab/FabAnimMap.json`, `Art/Fab/PurchasedPacks.json` | yes |

Two pack types need extra handling:
- **Complete-project packs** (Big Pack Magic Effects, Shadow Magic) can only be created as projects, under `Documents\Unreal Projects\...`. Copy their `Content/<PackFolder>` into the main checkout at the same `/Game` path; they have no plugin dependencies. The stray `/Game/Fire_Magic`-style names inside the Big Pack are stale vendor metadata and resolve to nothing at runtime.
- **Packs that show up while you work.** Rerun `python Tools/LinkFabContent.py`. It finds every untracked top-level Content folder (audio packs included), adds it to `.git/info/exclude`, which covers all worktrees, and junctions it everywhere.

## Niagara VFX: `CireFabVFX`

- **Data.** `Content/Data/FabVFX.json` maps `schools.<school>.<role>` to candidate Niagara systems. `<role>` is one of cast, projectile, impact, area or aura, and the first system that exists wins.
  - `buffs.<effectId | kind.school | kind>` adds state overlays.
  - `Tools/MapFabVFX.py` regenerates the file. It uses a curated table for Lord Enot's `NS_<Element>_Magic_<Type>` naming plus a keyword fallback for other packs. Entries marked `"locked": true` are kept as edited by hand.
- **Spells.** `ACireSpellVisual::UpdateFabVFX` adds one Niagara system on top of the procedural presentation, chosen by mode:
  - caster flare, self shock and channel get **cast**;
  - projectiles get **projectile**, which follows the actor and finishes its particles on end;
  - impacts and target marks get **impact**, with heals using the life cast;
  - live zones get **area**, scaled by radius.
- **Telegraphs.** Enemy lanes, gathers, void zones and area wind-ups never get an overlay, so the warning stays the dim procedural fill (15–25%). Line skillshots keep their line telegraphs.
- **Auras.** `UCireAuraComponent::UpdateFabAuras` attaches a looping state effect per active buff or debuff:
  - at most `MaxFabAuras` (8) at a time, nearest and most important units first;
  - other units follow the *Other units' aura effects* slider: nothing below 0.35, then scaled from 0.75 to 1.
  - The procedural signature layers always stay, so each buff keeps its readable signature.
- **Switches.** `cire.FabVFX 0` or `-CireNoFabVFX` turns the overlay off. `RunAbilityVFXGallery.py --no-fab` captures the "before" pictures.
- **Tests.** The native probe logs `CIRE_FAB_VFX_TESTS_PASS` and the coverage (`90/90 slots resolve` with both Lord Enot packs installed).

## Champion animation: `CireFabAnimation`

- **Retargeting.** `Tools/RetargetFabAnimations.py` reads `Art/Fab/FabAnimMap.json`, which lists source clips, timing and locomotion clips per direction. It retargets each clip onto every champion body in `ChampionAttacks02.json` `bodies`, using the proven Preview02 route:
  1. auto IK rigs (Manny template to UE4 template), with a chain-to-chain auto-aligned retargeter;
  2. a batch retarget;
  3. the Tripo root scale of 100 restored with `RepairTripoAnimationScale.repair_clip`;
  4. root motion off and root lock forced;
  5. validation of every clip.
- **Outputs.** The script writes `A_<Folder>_<clip>` clips and `BS_Fab_Locomotion_<Folder>` (Direction × Speed axes copied from the lancer), and fills `FabAnimations.json` with the windows of the clips that survived.
- **Runtime.**
  - `CireChampionActions::Apply` first tries `FabAnimations.json` `replace`: `style:<preset>`, then `motion:<class>`, then `default` → kind → clips, with alternatives rotating as a combo. Otherwise it plays the ChampionAttacks02 clip.
  - `ApplyFabReactions` adds a full-body **death** (held on its last frame), an upper-body **hit** flinch (at most every 0.9 s), a **dodge roll** that follows the mobility roll's progress, and an **airborne** pose driven by vertical velocity. Each replaces its procedural pose only when this body has the clip.
  - `UCireChampionArt::Apply` uses the Fab locomotion BlendSpace when it exists and matches the body's skeleton.
  - Weapons stay seated: `CireGrip` still curls the fingers around the props in every state.
- **Switches.** `cire.FabAnim 0` or `-CireNoFabAnim`. The native probe logs `CIRE_FAB_ANIM_TESTS_PASS`.

## Creatures: native clips, not AnimBPs (decision)

The ROG Creatures, Quadruped Fantasy Creatures and Undead packs ship with AnimBPs. We still drive them through the existing native monster path (`UCireMonsterAnimInstance` via `Content/Data/RaceMeshes.fab.json`, the same shape as `RaceMeshes.free.json`), for these reasons:
- **Gait sync.** Gait phase is locked to ground speed from data (`walkSpeedCm`/`runSpeedCm`, in-place clips, `lockRoot`), which keeps feet planted at any speed the wave director picks. A vendor AnimBP only knows its own blendspace speeds.
- **Uniform handling.** Casts, hits, deaths, corpses, realm visibility, rim tint and prop sockets work the same for every unit.
- **No per-pack upkeep.** There are no AnimBP compile or upgrade dependencies on packs that declare older engines.
- **Fallback.** `RaceMeshes.fab.json` is the highest-priority overlay, but a unit takes a Fab body only when its mesh and idle clip exist locally. Otherwise it keeps its Tripo or free body.

Champion creature bodies (the Bear, and the Centaur from Quadruped Fantasy Creatures) can move to the `monster_native` binding in `ChampionArtBindings.json` with the pack clips once the packs are installed. The procedural fallbacks stay in place.

## Environment and weapons

- **Town.** Medieval Kingdom meshes go into `Content/Data/TownAssetSlots.fab.json` (priority 20, `status: "imported"`), and the arenas use `Tools/AuthorArenas.py` `FAB_OVERRIDES`. Both resolve only when the package exists.
- **Weapons.** Ultimate Weapons Bundle props replace Tripo props only where they clearly look better. They need `WeaponGrips.json` handle data so the hands close on the grip.

## Status on 2026-09-25 (what landed where)

| Pack (Content folder) | Integrated into | Tool / data |
|---|---|---|
| Big Pack Magic Effects (`Big_Pack_Magic_VFX`), Shadow Magic (`Shadow_Magic`) | cast / projectile / impact / area / aura Niagara per school (fire, frost, storm, shadow, void, holy, life, poison, tide, blood, arcane, spirit) | `Tools/MapFabVFX.py` → `FabVFX.json` |
| State VFX (`State_VFX`) | 22 status effects (stun, root, slow, freeze, poison, burn, silence, curse, charm, blind, heal-over-time, mana, shock, bleed) | `FabVFX.json` `buffs` |
| Earth Spells (`Earth_Spells`), Nature VFX (`Forest_VFX`) | earth and nature schools; nature area heal | `FabVFX.json` |
| Realistic Blood (`RealisticBlood`) | restrained physical (steel) impacts: low-intensity slash/burst | `FabVFX.json` |
| GDH All Animation Bundle (`GDHBundle`), Male Locomotion (`MaleLocomotionSet`), Gun & Sword (`Gun_and_Sword`) | 22 champion bodies: attacks (combo rotation), casts, hit, death, dodge roll, airborne, 8-direction walk/run per weapon set | `Tools/BuildFabAnimMap.py`, `Tools/RetargetFabAnimations.py` → `FabAnimations.json`, `/Game/FabDerived` |
| ROG Creatures, Quadruped Fantasy Creatures, Undead Pack | dire_wolf, bristleback, feral_ursoth, feral_mammoth, grave_hound (Barghest), wild_outrider (Centaur), hollow_infantry (skeleton + ghoul variants) | `Tools/BuildFabCreatures.py` → `RaceMeshes.fab.json` |
| Ultimate Weapons (`Medieval_Weapons`, `_VOL2`) | sword, kite shield, spear, war hammer, dagger, axe props | `Tools/MeasureFabWeapons.py` → `WeaponLoadouts.fab.json`, `WeaponGrips.fab.json` |
| Medieval Kingdom (`CastleTown`) | 16 town prop slots (crates, barrels, keg, lanterns, castle door, cart, fence...) | `Tools/BuildFabTownSlots.py` → `TownAssetSlots.fabkit.json` |

Deliberately not used (reviewed in galleries): the Undead Pack zombie (cartoon-styled, kept the Tripo siegebreaker), the Mountain
Dragon (idle head near the ground on a 9 m winged mesh), the Undead skeleton archer (bow attack lives on the bow's skeleton), the
Medieval Kingdom market stand (bare frame) and table (hides the goods). Crossbow Animation Set is installed but not mapped yet (the
crossbow champion keeps its Tripo clip). The VOL2 bow/arrow/throwing axe have no CPU-readable vertices, so they keep their props.

Switches: `-CireNoFab` hides every overlay (VFX, animation, creatures, weapons, town) for before/after captures; the galleries
take `--no-fab` (ability VFX, monster, new champions, environment).
