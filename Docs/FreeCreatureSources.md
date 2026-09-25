# Free, pre-rigged and animated creatures for the monster races

Researched 2026-09-24 (world-dressing). Licenses were read on the live pages; GLB headers were read for bone and clip
lists. The repository is public, so only CC0 / public-domain / CC-BY content may be committed. Fab (Standard License)
and Epic content is licensed for use, not redistribution: it stays under the gitignored `Content/Fab/` (or the pack's
own folder) and is referenced by data with fallbacks.

Our runtime constraint (`Docs/MonsterArt.md`): monsters are animated natively by sampling `UAnimSequence`s (idle, walk,
run, attack, hit, death) with no Animation Blueprint. Any skeleton works as long as the clips exist; see "How a new body
is wired" below.

## What is integrated now (committed, CC0)

Six race units that had no art now draw an animated CC0 body instead of a reskinned humanoid fallback. They are the
lowest-priority overlay: as soon as the Tripo race agent (or anyone) lists the unit in `RaceMeshes.tripo.json`, that
art wins.

| Unit (race) | Body | Source | Clips used (idle / walk / run / attack / hit / death) |
|---|---|---|---|
| `dire_wolf` (feral_kin line) | Wolf, head at 150 cm | Quaternius Wolf, [poly.pizza/m/P1gU3Qkr9r](https://poly.pizza/m/P1gU3Qkr9r) | Idle / Walk / Gallop / Attack / Idle_HitReact_Left / Death |
| `grave_hound` (hollow special) | Wolf, head at 105 cm, hollow palette | same | same |
| `feral_shaman` (feral_kin caster, "Antlered Shaman") | Stag | Quaternius Stag, [poly.pizza/m/tQdzbZ1Cmw](https://poly.pizza/m/tQdzbZ1Cmw) | Idle / Walk / Gallop / Attack_Kick (+Attack_Headbutt alt) / HitReact / Death |
| `tusked_behemoth` (feral_kin tank) | Bull, head at 235 cm | Quaternius Bull, [poly.pizza/m/a8PIIYwF7r](https://poly.pizza/m/a8PIIYwF7r) | as Stag |
| `bristleback` (feral_kin special, boar) | Pig re-coloured as a black boar, knee-high | Quaternius Pig, [poly.pizza/m/u35l6uP5vj](https://poly.pizza/m/u35l6uP5vj) | Idle / Walk / Run / Headbutt / Jump_Start / Death |
| `crystal_ballista` (stoneborn ranged, "spider-legged construct") | Spider | Quaternius Spider, [poly.pizza/m/yRYJiAJyiM](https://poly.pizza/m/yRYJiAJyiM) | Spider_Idle / Spider_Walk (x2.2 for run) / Spider_Attack / Spider_Attack / Spider_Death |

Pipeline: `Tools/FetchFreeCreatures.py` (verifies each poly.pizza page says `Licence: CC0 1.0`, animated, creator
Quaternius, and the page's ResourceID matches the file before downloading) -> `Tools/ImportFreeCreatures.py` (import to
`/Game/Free/Creatures`, race-skin material instances, clip measurements, `Content/Data/RaceMeshes.free.json`).
Provenance: `Art/Creatures/Free/PROVENANCE.md`. Native checks: `RunNPCChecks --only native` PASS
(`CIRE_MONSTER_ART_PASS checks=2062 bodies=16`).

Honest limits: every committable model that can be downloaded without a login is **low-poly / stylized**. They sit in
the realistic town once darkened and tinted by the race and rank skin, and read well at gameplay distance, but they are
placeholders next to the Tripo bodies. The flat-shaded Quaternius animals have no UVs, so they take one colour per
material slot (no fur texture). There is no foot IK, and quadruped strafing plays the forward cycle.

## Research table

"Committable" means the files may go into this public repo.

### A. Committable (CC0 / CC-BY)

| Asset | License | Committable? | Rig / animations | Race / unit it could fill | Steps |
|---|---|---|---|---|---|
| **Quaternius Ultimate Animated Animal Pack** (Wolf, Husky, Fox, Stag, Deer, Horse, Bull, Cow, Donkey, Alpaca, Shiba) | CC0 1.0 (pack page + each poly.pizza page) | **yes** | "AnimalArmature" quadruped, 38-51 bones. Attack(_Headbutt/_Kick), Death, Eating, Gallop, Gallop_Jump, Idle, Idle_2, Idle_HeadLow, Idle_HitReact_L/R, Jump_ToIdle, Walk. Flat colours, no UVs | feral_kin dire wolf, antlered shaman, tusked behemoth; hollow grave hound; town ambience (horse, cow, deer) | **Integrated** (Wolf, Stag, Bull). Others: add to `CREATURES`/`RIGS`/`UNITS` in the two tools |
| Quaternius Animated Monster Pack (Skeleton, Bat, small Dragon, Slime) | CC0 1.0 | yes | Skeleton 12 bones (Attack, Death, Idle, Running, Spawn); Bat 23 (Attack, Attack2, Death, Flying, Hit); Dragon 27 (same) | hollow skeleton trash mob (has a rise-from-the-ground Spawn clip), drakkari `ember_whelp`, voidborn bat swarm | Flyers need a hover offset and no "feet planted" check; not integrated |
| Quaternius Ultimate Monsters (50: Orc, Demon, Ghost, Mushnub, Mushroom King, Squidle, Yeti...) | CC0 1.0 | yes | Humanoid 43 bones: Idle, Walk, Run, Punch, HitReact, Death, Weapon...; flying set | blightwood `sporeling` (Mushnub), voidborn stand-ins | **Chibi style**: placeholder only; not integrated |
| Quaternius Farm Animals (Pig, Cow, Horse, Llama, Sheep...) | CC0 1.0 | yes | Pig 13 bones: Death, Headbutt, Idle, Idle_Eating, Jump, Run, Walk | feral_kin `bristleback` | **Integrated** (Pig, darkened as a boar) |
| Quaternius Spider, Rat, Zombie (CC0 version), Tentacle, Snake | CC0 1.0 | yes | Spider 39 bones (Attack, Death, Idle, Jump, Walk); Rat 31 (Attack, Death, Idle, Jump, Run, Walk); Tentacle 21 (Attack, Attack2, Idle, Poke) | stoneborn `crystal_ballista` (Spider), hollow plague rats, voidborn/drowned tentacle | **Integrated** (Spider). Note: two other Quaternius zombies on poly.pizza are CC-BY 3.0 |
| Quaternius *Bestiary - Dungeon Monsters Kit* (Aug 2026) | **Quaternius Asset License**: no redistribution | **no** | humanoid | - | local only; not used |
| Khronos glTF sample **Fox** | model CC0, rig + animation CC-BY 4.0 (tomkranis), conversion CC-BY 4.0 | yes, with attribution | 24 bones, Survey/Walk/Run only (no attack/death) | ambient fox | not integrated |
| Poly Haven | CC0 | yes | **no animated creatures**; Street Rat is a static prop (used as static town vermin) | - | - |
| Kenney (Cube Pets, Blocky Characters, Graveyard Kit) | CC0 | yes | some animated | toy/blocky style: unsuitable | - |
| OpenGameArt CC0: 3d wolf (NewDLC, textured PBR), Evil Giant Rat (CDmir), Raven (Teh_Bucket), Chicken + Rooster (CDmir, painted), Rigged Horse, Octaminator | CC0 | yes | rigged; clip lists mostly unverified (.blend) | more realistic wolf/grave hound, plague rats, ambient crows/chickens, drowned tentacle walker | Blend -> FBX export by hand, then the same import tool |
| OpenGameArt CC-BY: Evil Crow Creature (5 clips), Earth Elemental golem (10 clips), Animated Wolf/Rat (CoinCoin) | CC-BY 3.0 / 4.0 | yes, with attribution | animated | voidborn/hollow flyer, stoneborn golem | credit the author in PROVENANCE |
| OpenGameArt CC-BY-SA / GPL (winter wolf, golems, boar enemies, tentacle monster) | share-alike | **avoid** (conflicts with the Fab EULA 6(a) if Fab content is in the project) | - | - | - |
| Sketchfab CC-BY (Hell Wolf, Wolf Spider, Animated Realistic Boar, Hellhound, Black Rat, Golem, Kraken, Crow) | CC-BY 4.0 | yes, with attribution, **but download needs a login** (API returns 401) | 7-12 clips each | realistic feral_kin / hollow / voidborn bodies | Eric downloads in a browser; beware rips from commercial games labelled CC |
| Mixamo | free for commercial use, **no redistribution of raw files** | **no** (local only) | huge humanoid clip library | humanoid races' motion | keep raw FBX local; ship only cooked |

### B. Local only: Fab / Epic (free, Unreal format, needs "Add to Project")

All checked on Fab: price 0 on every license tier, **Fab Standard License** (no public redistribution: keep out of the
repo). Paragon heroes are Unreal-format only, UE 4.19-5.8, each on its own UE4-style skeleton with AnimSequences
(idle, jog, abilities, hit reacts, deaths) and now AnimBPs; the trademark "Paragon" may not be used.

| Listing | Race / unit it could fill |
|---|---|
| Paragon: **Grux** | ironhide warchief (orc brute) |
| Paragon: **Rampage** | ironhide `ironhide_juggernaut` (troll) or stoneborn colossus |
| Paragon: **Khaimera** | feral_kin werebear / beast warrior |
| Paragon: **Sevarog** | hollow warlord or voidborn herald boss |
| Paragon: **Riktor** | ironhide hooker/bruiser (some sci-fi parts) |
| Paragon: **The Fey** | blightwood `withered_matron` |
| Paragon: **Minions** (dawn/dusk melee, ranged, super) | fallen_order / voidborn line units |
| Paragon: **Morigesh** | voidborn or hollow caster |
| Paragon: **Iggy & Scorch** | ironhide sapper |
| Paragon: Greystone, Terra, Serath | fallen_order knight-commander, champion, fallen angel |
| Paragon: Narbash | ironhide `ironhide_drummer` |
| Paragon: Revenant, Countess, Kallari | hollow gunslinger, hollow vampire caster, voidborn assassin |
| **Infinity Blade: Adversaries / Warriors** | **no longer listed on Fab** (did not migrate); usable only if already in Eric's Launcher library |
| **ANIMAL VARIETY PACK** (PROTOFACTOR): Crow (17 clips), Fox, Stag, Doe, Pig, Wolf | best realistic match: feral_kin wolf/shaman/boar, ambient crows |
| **Quadruped Fantasy Creatures** (PROTOFACTOR): Barghest (19 clips), Centaur, Griffon, Mountain Dragon (41 clips) | hollow grave hound / dire wolf (Barghest), drakkari drake boss (Dragon) - already in Eric's library |
| **Dragon for Boss Monster: PBR** (Dungeon Mason): 4 dragons, up to 18 clips | drakkari `drakkari_ashwing` / broodmother |
| **Skeleton Necromancer** (Feyloom), UE5 skeleton | hollow caster |
| **Whisper** (Leks), 16 clips | hollow reaper / voidborn wraith |
| **Monster** (Khornes), **CC BY 4.0** on Fab, Epic skeleton | committable with attribution after a Fab login download: fallen_order / voidborn brute |
| **Spiders - characters with animations** (Mixall), **CC BY 4.0**, 10 spiders | committable with attribution: voidborn / hollow spiders |
| "Monster for Survival Game PBR Polyart" | **no longer free** (now a $59.99 bundle): skip |

### Steps for a local-only pack (Eric)

1. Launcher **Library -> Fab Library** (or **Window -> Fab** in the editor): **Add to Project -> CiresTeamSurvival**.
   Packs land in their own top folder (e.g. `/Game/ParagonGrux`), which must stay out of git: move them under
   `Content/Fab/` or add the folder to `.gitignore` before committing anything.
2. Note the skeletal mesh and the AnimSequences for idle, walk, run (jog), attack, hit and death.
3. Add the unit to `Content/Data/RaceMeshes.free.json` by hand (or to a local `RaceMeshes.tripo.json` entry):
   `mesh`, `meshScale`, `yaw` (so the head faces +X), `heightCm`, `animations`, plus `walkSpeedCm`/`runSpeedCm` when the
   clips are in place. Humanoid Paragon rigs already have `head`/`hand_r`/`foot_l` style names on most skeletons;
   otherwise map them with `sockets`. A missing package is skipped, so the entry is harmless on other machines.
4. Run `python Tools/RunNPCChecks.py --only native` (every body is posed and measured) and
   `python Tools/RunMonsterGallery.py --only races_` to look at it.

## How a new body is wired (`RaceMeshes.free.json`)

Same shape as `RaceMeshes.tripo.json` (`variant`, `mesh`, `meshScale`, `yaw`, `heightCm`, `animations`) plus:

* `rig`: `"quadruped"` accepts sockets instead of humanoid bones in the checks.
* `sockets`: socket name -> bone. Added to the mesh in memory when the body is applied, so aura sockets, footsteps and
  hit points find `head`, `pelvis`, `spine_03`, `hand_l/r`, `foot_l/r`, `ball_l/r` on an animal.
* `walkSpeedCm`, `runSpeedCm`: natural ground speed of in-place clips (cm/s at actor scale 1), so the gait rate follows
  the real movement speed; measured from the paw stride by the import tool.
* `reachCm`: pose bound for long bodies and tails in the checks.
* `lockRoot`: hold the root joint at its bind transform (Quaternius AnimalArmature clips key the armature proxy root).
* `dropPropBones`: archetype props that must not attach (animals hold no dagger).

Import gotcha fixed in the tool: these Blender glTF exports put scale 100 / -90 degrees X on the *skinned mesh node*.
glTF ignores that transform for skinned meshes, but Interchange baked it into the vertices, so the skin rendered 100x
too large while every bone measured correctly. The importer loads a copy with identity skinned-mesh node transforms.
