# Jungle packs

Challenge packs are now **jungle mobs** (Eric, 2026-09-26). The game gets longer: teams clear packs around the town to
build their kit and their strategy, while they keep defending against the waves from the Breach. That means more PvE,
and more continuous use of the game's mechanics.

This page is the spec and the brief for future agents. It covers:

- the rules
- the monster inventory
- the tier table
- the composition rules
- the Recall ability
- the files and the tests

Code:

- `Source/CiresTeamSurvival/CireJunglePacks.{h,cpp}`: rules, pool, compositions, tier loadouts, formation
- `CireLoot.cpp`: `CireProgression::SpawnBay` and `JungleSchedule`
- `CireMapLayout.cpp`: the pack marker fields
- `CireLayoutEditorHUD.cpp`: the inspector
- `CireLanePathLayout.cpp`: replication and recall points
- `CireItems.cpp`: Recall

Data: `Content/Data/JunglePacks.json`, with a built-in fallback of the same values.

## What Eric asked for

1. **No marker limits.** "I need unlimited of any type."
   - The editor places any number of any setter.
   - The runtime spawns, replicates and draws any number of packs.
2. **Tiers 1–4.** The tier sets how many abilities each monster in the pack uses:

   | Tier | Abilities per monster |
   | --- | --- |
   | T1 | 2 |
   | T2 | 3 |
   | T3 | 5 |
   | T4 | its complete kit |

   Stats and rewards grow with the tier too.
3. **Pack type = monster race**, or Mixed. The pool is built from every monster the wave system can spawn, grouped by race.
4. **Composition:** 3–6 monsters per pack.
   - Tanks: 1–2.
   - Healers: 1–2.
   - DPS: 1–3.
   - The default composition is deterministic. Eric can override it per pack in the panel, and the override is clamped to the rules.
5. **Right panel** for a Challenge Pack:
   - tier
   - pack type
   - composition with +/-
   - a summary line
   - radius, owner and mirror
6. **Recall:** every champion has a Recall to a Recall Point that Eric places. It has a 2-minute cooldown and a channel, and it shows on the action bar.

## Unlimited markers

- **Placement is never capped.** `MapMarkerTypes.json` `maxPerOwner` no longer blocks placement. It now means "how many the
  game reads per team", and **Validate** explains any extras:

  | Setter | The game reads | Validate |
  | --- | --- | --- |
  | Challenge Pack | all | (no cap) |
  | Player Spawn | 10 per team (one per hero slot) | note: "T1 has 12 Player Spawn markers; the game uses the first 10" |
  | Objective | 1 per team | error: "the game runs one castle goal zone per team (the first); remove the extras" |
  | Play Bounds | 1 polygon | error: "the game uses one (the first); merge them or remove the extras" |
  | Rift | 1 per team | note (unchanged) |
  | Monster spawns, paths, respawns, boss spawns, rifts, recall points | up to 64 per realm (was 16) | compile note past 64; the network budget check explains a layout whose paths are too big to send |

- **Packs in the runtime have no cap anywhere:**
  - The route document (`bays` accepts any count).
  - The route rules.
  - The pack schedule (`RouteSchedule` / `ValidateSchedule`).
  - Pack ids: `round x 100000 + realm x 50000 + bay`, so each realm has 50,000 bays.
  - The daises, the minimap and the town-prop clearance.
- **Replication.** Packs no longer ride in the float `LaneLayout`, which was capped near 2,048 values.
  - They go in `ACireGameState::LanePacks`: chunks of at most 1,020 ints.
  - The first chunk opens with a header: magic, route revision, pack count of realm 0, then the pack count of realm 1, or -1 when the realms are identical. Identical realms are sent once.
  - Each pack is 3 ints:
    - `x/10 | y/10` (int16 each)
    - `radius/10 | tier | type | tanks | healers | DPS`
    - `seed`
  - 3,000 packs are about 9,000 ints in 9 chunks, each far inside the engine's array budget.
  - Clients apply the packs only when their revision matches `LaneRouteVersion`, so the two halves of an update never mix.
  - "Layout too large to send" no longer counts packs.
- **Fix found on the way:** `LaneLayout` (goal zone, base, extras) was never registered for replication. It is now, together with `LanePacks`.

## Tiers

| Tier | Abilities per monster | Health | Damage | Gold per kill | First appears |
| --- | --- | --- | --- | --- | --- |
| T1 | 2 | x1.00 | x1.00 | x1.0 | round 1, wave 1 |
| T2 | 3 | x1.65 | x1.10 | x1.5 | round 1, wave 3 |
| T3 | 5 | x2.30 | x1.25 | x2.25 | round 2, wave 1 |
| T4 | complete kit (6 or more) | x2.95 | x1.40 | x3.0 | round 3, wave 1 |

How to read the table:

- **Health** is the engine's challenge curve, `1 + 0.65 x (tier - 1)` (`Cires::ChallengeHealthMultiplier`, times `+12 %` per round).
  - `JunglePacks.json` `health` multiplies on top of it. It is 1.0 by default.
  - The pack's **leader** (its first tank) gets x1.5 more (`leader.healthMultiplier`).
- **Damage** is `JunglePacks.json` `damage`, on top of the challenge damage.
- **Gold** is `JunglePacks.json` `gold`. It multiplies every pack kill's bounty:
  - a pack unit is worth 10x the wave value
  - the leader is worth 100x the wave value
  - loot tables already grow +25 % per tier.
- **First appears** is `unlockRound` / `unlockWave`, where a round is one match cycle.
  - Promotions (`LootTables.json` `packSchedule`) can raise a pack's tier as rounds pass, capped at 4.
- The **ability tier numeral** (I–III on the ability names) is the pack tier. T4 shows III with the full kit.
- **Abilities** are the non-basic abilities. The basic attack is always there.
  - A monster uses its own kit first: the core skills, then its own pool in the match's seeded order.
  - Borrowed abilities come after its own kit.
  - T4 uses everything, and it is always more than T3 because every kit is topped up to `kitFloor` (6).

## Pack types and roles

**Pack types** are the ten races in `Races.json` `raceOrder`, plus **Mixed**:

- drowned_deep
- blightwood
- hollow
- ironhide
- drakkari
- stoneborn
- feral_kin
- fallen_order
- voidborn
- aetheri
- **Mixed**

**Hollow** is also a type. Eric's list names nine races, but the Hollow Legion is the wave system's first race, so its bodies are included.

- A pack marker without `"pack"`, or with an unknown one, reads as **Mixed**. Every pack placed before this change is a T1 (or older tier) Mixed pack.
- **Mixed** draws every race's units plus the three wild rare creatures.

**Roles** (`CireJunglePacks::RoleOf`):

| Pack role | Rule |
| --- | --- |
| Tank | the archetype's role is `tank` |
| Healer | it owns a `healAlly` ability |
| DPS | everything else: bruiser, ranged, caster without a heal, swarm, support without a heal |

- Bosses are never pack members.
- The non-combat bonus creatures (`exclude`) are never pack members.
- The three rare creatures belong to a **family** race. They borrow skills from it, but they only appear in Mixed packs.

## Composition rules

A pack has **3–6 monsters**: **1–2 tanks, 1–2 healers, 1–3 DPS**. There is always at least one of each role.

**Default (automatic).** The default composition is deterministic from:

- the pack's **seed** (from its realm-local position, so mirrored twins match)
- its **type**
- its **tier**

Its size grows with the tier:

| Tier | Monsters |
| --- | --- |
| T1 | 3–4 |
| T2 | 4–5 |
| T3 | 5–6 |
| T4 | 6 |

The extra slots go to DPS twice as often as to tanks or healers. A role at its maximum is skipped.

**Override.** The inspector's +/- buttons store `"comp": [tanks, healers, dps]` on the marker.

- A step that would break the rules is refused, and the panel says why.
- Overrides from files are clamped: each count to its range, then the total down to 6 (DPS first, then healers, then tanks).
- **AUTO** clears the override.
- Validate flags any invalid composition in a file.

**Members.** For each role slot, the unit is drawn from the type's pool of that role (a seeded shuffle, cycling).

- The spawn order is tanks, then healers, then DPS.
- The **first tank is the Pack Leader**. It gets warlord colours, the boss frame, the pack-leader bounty and loot, and x1.5 health.

**Formation** (inside the pack radius, turned by the seed):

- tanks in a front row
- DPS in the middle
- healers behind

Each spot is projected onto the navmesh, falling back to the pack centre's navmesh point. `CIRE_PACK_SPAWN ... offnav=N` counts the fallbacks.

**Missing role.** If a race ever has no unit of a role, that slot draws the role from the Mixed pool. This is a cross-race **stand-in**, and it is logged as `STAND-IN` in the audit. No race needs one today.

## Monster inventory

The block below is generated from the data by `Tools/JunglePackInventory.py --write`. It applies the same rules as the game.
Rerun it after changing Races.json, Bestiary.json or JunglePacks.json.

<!-- inventory:begin (Tools/JunglePackInventory.py --write) -->

**68 pack monsters** (non-boss combat units the wave system can spawn), 20 bosses kept out of packs, 2 non-combat creatures excluded (gilded_stag, treasure_goblin).

| Pack type | Tank | Healer | DPS | Total | Units (role, own kit -> complete kit) |
| --- | ---: | ---: | ---: | ---: | --- |
| The Drowned Deep (`drowned_deep`) | 1 | 2 | 3 | 6 | abyssal_stalker (DPS, 4->6), barbspitter (DPS, 4->6), coralshell_guardian (tank, 4->6), deepspawn_thrall (DPS, 4->6), mind_leech (healer, 5->6), tidecaller (healer, 5->6) |
| The Blightwood (`blightwood`) | 1 | 2 | 3 | 6 | barkhide_warden (tank, 4->6), rotbloom_shaman (healer, 5->6), sapling_brute (DPS, 4->6), sporeling (healer, 4->6), thornspitter (DPS, 4->6), vinelasher (DPS, 4->6) |
| The Hollow Legion (`hollow`) | 1 | 1 | 6 | 8 | barbed_hunter (DPS, 3->6), blight_caster (healer, 4->6), bone_archer * (DPS, 1->6), grave_hound (DPS, 3->6), hollow_infantry (DPS, 3->6), hollow_shieldbearer (tank, 4->6), ironbound_bruiser (DPS, 4->6), rotting_shambler * (DPS, 2->6) |
| The Ironhide Warband (`ironhide`) | 1 | 2 | 4 | 7 | blood_hexer (healer, 4->6), horned_brute * (DPS, 3->6), ironhide_bulwark (tank, 4->6), ironhide_drummer (healer, 3->6), ironhide_grunt (DPS, 4->6), redmoon_axethrower (DPS, 4->6), redmoon_ravager (DPS, 4->6) |
| The Drakkari Brood (`drakkari`) | 1 | 1 | 4 | 6 | drakkari_flamecaller (healer, 4->6), drakkari_scalebreaker (DPS, 4->6), drakkari_scaleguard (tank, 4->6), drakkari_whelpguard (DPS, 3->6), drakkari_wingshot (DPS, 4->6), ember_whelp (DPS, 3->6) |
| The Stoneborn (`stoneborn`) | 1 | 2 | 3 | 6 | bastion_golem (tank, 4->6), crystal_ballista (DPS, 4->6), deepforge_runesmith (healer, 4->6), ether_mote (healer, 4->6), granite_crusher (DPS, 4->6), rune_sentinel (DPS, 3->6) |
| The Feral Kin (`feral_kin`) | 1 | 1 | 5 | 7 | bristleback (DPS, 3->6), centaur_blademaster * (DPS, 2->6), dire_wolf (DPS, 3->6), feral_shaman (healer, 4->6), tusked_behemoth (tank, 4->6), werebear_mauler (DPS, 4->6), wild_outrider (DPS, 4->6) |
| The Fallen Order (`fallen_order`) | 1 | 2 | 4 | 7 | blighted_chaplain (healer, 4->6), dread_knight (DPS, 4->6), fallen_inquisitor_crossbow (DPS, 4->6), fallen_squire (DPS, 3->6), flagellant (DPS, 3->6), lich_revenant * (healer, 3->6), oathbreaker_templar (tank, 4->6) |
| The Voidborn (`voidborn`) | 1 | 1 | 4 | 6 | null_warden (tank, 4->6), rift_gazer (DPS, 4->6), rift_stalker (DPS, 3->6), rift_weaver (healer, 4->6), void_ravager (DPS, 4->6), voidling (DPS, 3->6) |
| The Aetheri Remnant (`aetheri`) | 1 | 1 | 4 | 6 | aetheri_bulwark (tank, 4->6), aetheri_engineer (healer, 4->6), aetheri_lancer (DPS, 3->6), aetheri_phaseblade (DPS, 3->6), aetheri_warframe (DPS, 3->6), skitter_drone (DPS, 2->6) |
| Wild creatures (Mixed only) | 0 | 0 | 3 | 3 | cinder_drake * (DPS, family drakkari, 2->6), frostfang_alpha * (DPS, family feral_kin, 3->6), storm_griffon * (DPS, family feral_kin, 2->6) |
| **Mixed (all of the above)** | **10** | **15** | **43** | **68** | |

`*` = a Bestiary.json creature (race variant or rare). Bosses (never pack members): `aetheri_colossus`, `aetheri_hierarch`, `drakkari_ashwing`, `drakkari_broodmother`, `drowned_prophet`, `elder_oakheart`, `fallen_crusader`, `fallen_high_inquisitor`, `feral_mammoth`, `feral_ursoth`, `gravemaw_pack_leader`, `hollow_siegebreaker`, `ironhide_juggernaut`, `ironhide_warchief`, `maw_of_the_deep`, `stoneborn_colossus`, `stoneborn_forgelord`, `voidborn_devourer`, `voidborn_herald`, `withered_matron`.

### Role gaps and stand-ins

- None: every race fields at least one tank, one healer and one DPS, so no cross-race stand-in is needed today. If a race ever loses a role, that slot draws the role from the Mixed pool and the game logs it (`STAND-IN` in the audit).

### Kit fill audit (kit floor 6)

Every pack unit needs 5 abilities for T3 and a complete kit bigger than that for T4. Units whose own kit has fewer than 6 non-basic abilities borrow the rest from their race (same pack role first). 68 units are filled:

- `abyssal_stalker` [drowned_deep DPS]: own kit 4, borrowed 2 from its race drowned_deep: drowned_tidal_slam (deepspawn_thrall), drowned_barnacle_charge (deepspawn_thrall)
- `aetheri_bulwark` [aetheri tank]: own kit 4, borrowed 2 from its race aetheri: aether_colossus_beam (aetheri_colossus), aether_colossus_stomp (aetheri_colossus)
- `aetheri_engineer` [aetheri healer]: own kit 4, borrowed 2 from its race aetheri: aether_psionic_storm (aetheri_hierarch), aether_hierarch_ascension (aetheri_hierarch)
- `aetheri_lancer` [aetheri DPS]: own kit 3, borrowed 3 from its race aetheri: aether_psionic_storm (aetheri_hierarch), aether_hierarch_ascension (aetheri_hierarch), aether_phase_strike (aetheri_phaseblade)
- `aetheri_phaseblade` [aetheri DPS]: own kit 3, borrowed 3 from its race aetheri: aether_warp_slam (aetheri_warframe), aether_warframe_charge (aetheri_warframe), aether_overdrive (aetheri_warframe)
- `aetheri_warframe` [aetheri DPS]: own kit 3, borrowed 3 from its race aetheri: aether_phase_strike (aetheri_phaseblade), aether_psi_sweep (aetheri_phaseblade), aether_psionic_storm (aetheri_hierarch)
- `barbed_hunter` [hollow DPS]: own kit 3, borrowed 3 from its race hollow: hollow_pounce (grave_hound), hollow_rending_bite (grave_hound), hollow_pack_howl (grave_hound)
- `barbspitter` [drowned_deep DPS]: own kit 4, borrowed 2 from its race drowned_deep: drowned_stalker_lunge (abyssal_stalker), drowned_rend (abyssal_stalker)
- `barkhide_warden` [blightwood tank]: own kit 4, borrowed 2 from its race blightwood: blight_root_eruption (elder_oakheart), blight_oakheart_stomp (elder_oakheart)
- `bastion_golem` [stoneborn tank]: own kit 4, borrowed 2 from its race stoneborn: stoneborn_quake (stoneborn_colossus), stoneborn_boulder_toss (stoneborn_colossus)
- `blight_caster` [hollow healer]: own kit 4, borrowed 2 from its race hollow: npc_hunter_disengage (barbed_hunter), npc_hunter_volley (barbed_hunter)
- `blighted_chaplain` [fallen_order healer]: own kit 4, borrowed 2 from its race fallen_order: fallen_chains_of_judgment (fallen_high_inquisitor), fallen_mass_silence (fallen_high_inquisitor)
- `blood_hexer` [ironhide healer]: own kit 4, borrowed 2 from its race ironhide: ironhide_war_drums (ironhide_drummer), ironhide_battle_hymn (ironhide_drummer)
- `bone_archer` [hollow DPS]: own kit 1, borrowed 5 from its race hollow: npc_hunter_disengage (barbed_hunter), npc_hunter_volley (barbed_hunter), hollow_pinning_net (barbed_hunter), hollow_pounce (grave_hound), hollow_rending_bite (grave_hound)
- `bristleback` [feral_kin DPS]: own kit 3, borrowed 3 from its race feral_kin: feral_pounce (dire_wolf), feral_hamstring (dire_wolf), feral_pack_howl (dire_wolf)
- `centaur_blademaster` [feral_kin DPS]: own kit 2, borrowed 4 from its race feral_kin: feral_pounce (dire_wolf), feral_hamstring (dire_wolf), feral_pack_howl (dire_wolf), feral_elder_roar (feral_ursoth)
- `cinder_drake` [drakkari DPS]: own kit 2, borrowed 4 from its family race drakkari: drakkari_wing_buffet (drakkari_scalebreaker), drakkari_dive_charge (drakkari_scalebreaker), drakkari_molten_slam (drakkari_scalebreaker), drakkari_draconic_fury (drakkari_scalebreaker)
- `coralshell_guardian` [drowned_deep tank]: own kit 4, borrowed 2 from its race drowned_deep: drowned_tentacle_sweep (maw_of_the_deep), drowned_devour (maw_of_the_deep)
- `crystal_ballista` [stoneborn DPS]: own kit 4, borrowed 2 from its race stoneborn: stoneborn_boulder_charge (granite_crusher), stoneborn_ground_pound (granite_crusher)
- `deepforge_runesmith` [stoneborn healer]: own kit 4, borrowed 2 from its race stoneborn: stoneborn_ether_repair (ether_mote), stoneborn_shield_matrix (ether_mote)
- `deepspawn_thrall` [drowned_deep DPS]: own kit 4, borrowed 2 from its race drowned_deep: drowned_stalker_lunge (abyssal_stalker), drowned_rend (abyssal_stalker)
- `dire_wolf` [feral_kin DPS]: own kit 3, borrowed 3 from its race feral_kin: feral_elder_roar (feral_ursoth), feral_rending_maul (feral_ursoth), feral_ursoth_charge (feral_ursoth)
- `drakkari_flamecaller` [drakkari healer]: own kit 4, borrowed 2 from its race drakkari: drakkari_flame_nova (drakkari_broodmother), drakkari_wing_gust (drakkari_broodmother)
- `drakkari_scalebreaker` [drakkari DPS]: own kit 4, borrowed 2 from its race drakkari: drakkari_spear_lunge (drakkari_whelpguard), drakkari_tail_sweep (drakkari_whelpguard)
- `drakkari_scaleguard` [drakkari tank]: own kit 4, borrowed 2 from its race drakkari: drakkari_inferno_breath (drakkari_ashwing), drakkari_ashwing_tail (drakkari_ashwing)
- `drakkari_whelpguard` [drakkari DPS]: own kit 3, borrowed 3 from its race drakkari: drakkari_wing_buffet (drakkari_scalebreaker), drakkari_dive_charge (drakkari_scalebreaker), drakkari_molten_slam (drakkari_scalebreaker)
- `drakkari_wingshot` [drakkari DPS]: own kit 4, borrowed 2 from its race drakkari: drakkari_flame_nova (drakkari_broodmother), drakkari_wing_gust (drakkari_broodmother)
- `dread_knight` [fallen_order DPS]: own kit 4, borrowed 2 from its race fallen_order: fallen_oath_strike (fallen_squire), fallen_shield_rush (fallen_squire)
- `ember_whelp` [drakkari DPS]: own kit 3, borrowed 3 from its race drakkari: drakkari_flame_nova (drakkari_broodmother), drakkari_wing_gust (drakkari_broodmother), drakkari_magma_rain (drakkari_broodmother)
- `ether_mote` [stoneborn healer]: own kit 4, borrowed 2 from its race stoneborn: stoneborn_rune_mine (deepforge_runesmith), stoneborn_forge_mending (deepforge_runesmith)
- `fallen_inquisitor_crossbow` [fallen_order DPS]: own kit 4, borrowed 2 from its race fallen_order: fallen_dark_cleave (dread_knight), fallen_deathcharge (dread_knight)
- `fallen_squire` [fallen_order DPS]: own kit 3, borrowed 3 from its race fallen_order: fallen_dark_cleave (dread_knight), fallen_deathcharge (dread_knight), fallen_unholy_frenzy (dread_knight)
- `feral_shaman` [feral_kin healer]: own kit 4, borrowed 2 from its race feral_kin: feral_boar_gore (bristleback), feral_bristle_burst (bristleback)
- `flagellant` [fallen_order DPS]: own kit 3, borrowed 3 from its race fallen_order: fallen_dark_cleave (dread_knight), fallen_deathcharge (dread_knight), fallen_unholy_frenzy (dread_knight)
- `frostfang_alpha` [feral_kin DPS]: own kit 3, borrowed 3 from its family race feral_kin: feral_pounce (dire_wolf), feral_hamstring (dire_wolf), feral_pack_howl (dire_wolf)
- `granite_crusher` [stoneborn DPS]: own kit 4, borrowed 2 from its race stoneborn: stoneborn_rune_strike (rune_sentinel), stoneborn_rune_rush (rune_sentinel)
- `grave_hound` [hollow DPS]: own kit 3, borrowed 3 from its race hollow: npc_hunter_volley (barbed_hunter), hollow_pinning_net (barbed_hunter), boss_leader_rally (gravemaw_pack_leader)
- `hollow_infantry` [hollow DPS]: own kit 3, borrowed 3 from its race hollow: boss_leader_rally (gravemaw_pack_leader), boss_leader_cleave (gravemaw_pack_leader), boss_leader_charge (gravemaw_pack_leader)
- `hollow_shieldbearer` [hollow tank]: own kit 4, borrowed 2 from its race hollow: boss_siege_stomp (hollow_siegebreaker), boss_siege_cleave (hollow_siegebreaker)
- `horned_brute` [ironhide DPS]: own kit 3, borrowed 3 from its race ironhide: ironhide_cleave (ironhide_grunt), ironhide_war_charge (ironhide_grunt), ironhide_bloodlust (ironhide_grunt)
- `ironbound_bruiser` [hollow DPS]: own kit 4, borrowed 2 from its race hollow: boss_leader_rally (gravemaw_pack_leader), boss_leader_cleave (gravemaw_pack_leader)
- `ironhide_bulwark` [ironhide tank]: own kit 4, borrowed 2 from its race ironhide: ironhide_earthsplitter (ironhide_juggernaut), ironhide_trunk_sweep (ironhide_juggernaut)
- `ironhide_drummer` [ironhide healer]: own kit 3, borrowed 3 from its race ironhide: ironhide_blood_hex (blood_hexer), ironhide_spirit_mend (blood_hexer), ironhide_bog_curse (blood_hexer)
- `ironhide_grunt` [ironhide DPS]: own kit 4, borrowed 2 from its race ironhide: ironhide_warchief_roar (ironhide_warchief), ironhide_skull_cleave (ironhide_warchief)
- `lich_revenant` [fallen_order healer]: own kit 3, borrowed 3 from its race fallen_order: fallen_profane_light (blighted_chaplain), fallen_dark_absolution (blighted_chaplain), fallen_censer_smoke (blighted_chaplain)
- `mind_leech` [drowned_deep healer]: own kit 5, borrowed 1 from its race drowned_deep: drowned_drowning_pool (tidecaller)
- `null_warden` [voidborn tank]: own kit 4, borrowed 2 from its race voidborn: void_devour (voidborn_devourer), void_singularity (voidborn_devourer)
- `oathbreaker_templar` [fallen_order tank]: own kit 4, borrowed 2 from its race fallen_order: fallen_crusader_cleave (fallen_crusader), fallen_crusade (fallen_crusader)
- `redmoon_axethrower` [ironhide DPS]: own kit 4, borrowed 2 from its race ironhide: ironhide_cleave (ironhide_grunt), ironhide_war_charge (ironhide_grunt)
- `redmoon_ravager` [ironhide DPS]: own kit 4, borrowed 2 from its race ironhide: ironhide_cleave (ironhide_grunt), ironhide_war_charge (ironhide_grunt)
- `rift_gazer` [voidborn DPS]: own kit 4, borrowed 2 from its race voidborn: void_phase_strike (rift_stalker), void_rend (rift_stalker)
- `rift_stalker` [voidborn DPS]: own kit 3, borrowed 3 from its race voidborn: void_gravity_slam (void_ravager), void_rift_charge (void_ravager), void_null_cleave (void_ravager)
- `rift_weaver` [voidborn healer]: own kit 4, borrowed 2 from its race voidborn: void_silence_of_stars (voidborn_herald), void_collapsing_star (voidborn_herald)
- `rotbloom_shaman` [blightwood healer]: own kit 5, borrowed 1 from its race blightwood: blight_thornstorm (withered_matron)
- `rotting_shambler` [hollow DPS]: own kit 2, borrowed 4 from its race hollow: boss_leader_rally (gravemaw_pack_leader), boss_leader_cleave (gravemaw_pack_leader), boss_leader_charge (gravemaw_pack_leader), boss_leader_frenzy (gravemaw_pack_leader)
- `rune_sentinel` [stoneborn DPS]: own kit 3, borrowed 3 from its race stoneborn: stoneborn_boulder_charge (granite_crusher), stoneborn_ground_pound (granite_crusher), stoneborn_granite_smash (granite_crusher)
- `sapling_brute` [blightwood DPS]: own kit 4, borrowed 2 from its race blightwood: blight_vine_lash (vinelasher), blight_strangling_vines (vinelasher)
- `skitter_drone` [aetheri DPS]: own kit 2, borrowed 4 from its race aetheri: aether_psionic_storm (aetheri_hierarch), aether_hierarch_ascension (aetheri_hierarch), aether_photon_beam (aetheri_lancer), aether_orbital_strike (aetheri_lancer)
- `sporeling` [blightwood healer]: own kit 4, borrowed 2 from its race blightwood: blight_rot_pool (rotbloom_shaman), blight_entangle (rotbloom_shaman)
- `storm_griffon` [feral_kin DPS]: own kit 2, borrowed 4 from its family race feral_kin: feral_pounce (dire_wolf), feral_hamstring (dire_wolf), feral_pack_howl (dire_wolf), feral_elder_roar (feral_ursoth)
- `thornspitter` [blightwood DPS]: own kit 4, borrowed 2 from its race blightwood: blight_stump_slam (sapling_brute), blight_uproot_charge (sapling_brute)
- `tidecaller` [drowned_deep healer]: own kit 5, borrowed 1 from its race drowned_deep: drowned_tidal_prophecy (drowned_prophet)
- `tusked_behemoth` [feral_kin tank]: own kit 4, borrowed 2 from its race feral_kin: feral_mammoth_stomp (feral_mammoth), feral_mammoth_trample (feral_mammoth)
- `vinelasher` [blightwood DPS]: own kit 4, borrowed 2 from its race blightwood: blight_stump_slam (sapling_brute), blight_uproot_charge (sapling_brute)
- `void_ravager` [voidborn DPS]: own kit 4, borrowed 2 from its race voidborn: void_phase_strike (rift_stalker), void_rend (rift_stalker)
- `voidling` [voidborn DPS]: own kit 3, borrowed 3 from its race voidborn: void_disintegrate (rift_gazer), void_orb (rift_gazer), void_paralyze_gaze (rift_gazer)
- `werebear_mauler` [feral_kin DPS]: own kit 4, borrowed 2 from its race feral_kin: feral_pounce (dire_wolf), feral_hamstring (dire_wolf)
- `wild_outrider` [feral_kin DPS]: own kit 4, borrowed 2 from its race feral_kin: feral_boar_gore (bristleback), feral_bristle_burst (bristleback)

<!-- inventory:end -->

## The inspector (right panel)

When a Challenge Pack is selected, the panel shows:

- **Owner** (T1 / T2 / SHARED) and **MIRROR**, as for every marker.
- **RADIUS** (`[` `]` keys).
- **TIER**: T1 T2 T3 T4 buttons (`-` `=` keys step it). The tooltip gives the ability counts. The tier you set also becomes the tier of the next pack you place.
- **PACK TYPE**: `<` `>`, or **K** to cycle. It shows the race name or Mixed.
- **COMPOSITION**:
  - TANKS, HEALERS and DPS, each with - / + and its range
  - **AUTO** to go back to the generated composition
  - a total line that turns red if a file breaks the rules
- **COPY TO ALL PACKS** / **COPY WITHIN 30 m**: give the team's other packs (and their twins) this pack's tier, type and composition in one undo step. Use it to retier a layout full of T1 packs.
- **Summary**, for example `T3 Drowned Deep: 2 tank · 1 healer · 3 DPS · 5 abilities each`. It adds `(auto)` when the composition is generated.

Other details:

- **Fixed (Eric's playtest, "it just kept making tier 1's").** Placing a pack selects it, so the tier keys changed only
  that pack, and every next pack was placed at the unchanged "next tier" (1). Now a tier or type change on a selected
  pack also becomes the next pack's, and with Challenge Pack armed and nothing selected the panel shows NEXT TIER
  (T1–T4) and NEXT TYPE directly. The route-tools test places packs at each tier, runs place / = / place, and checks
  every pack and its twin through save/load and compile.

- With nothing selected and Challenge Pack armed, `-` `=` and K set the **next** pack's tier and type. The reticle shows them.
- Mirrored twins copy the type and composition, and Validate checks that they stay in sync.
- List labels read `T1 Pack 12  T3 Drowned Deep`.

## Recall

Every champion has **Recall**. It is the existing Teleport to Base hearthstone channel, retargeted.

**Where it goes.** Recall takes the champion to the **nearest Recall Point of his team**. The Recall Point is a new setter:

- Shift+2 by default (action bar 2, slot 2)
- per team and mirrored like the others
- it has a facing

With no Recall Point, the champion lands at the base (the first player spawn).

Why a marker type and not a vendor sub-handle:

- Recall points are independent of the stalls. Eric can put one by the market, one by the castle, or several.
- The hero goes to the **nearest** one, which a per-vendor handle could not express cleanly.
- It reuses the marker list, mirroring, Validate and FLY TO.

**Channel.** The channel (6 s) is interrupted by damage or by moving more than 40 cm. It is free and instant during prep and recovery, as before.

**Cooldown.** The 120 s cooldown starts when the channel completes.

**Data.** The channel time, cooldown and move tolerance are data-driven: `Items.json` `"teleport": { channelSeconds, cooldownSeconds, moveTolerance }`.

**Controls.**

- The key is **RecallToTown**, G by default.
- The action bar shows a **Recall** button with the cooldown sweep. It is not one of the skill slots.

**Shops.** Shops are always open (B opens every merchant anywhere). Recall is about walking back to the stalls, not about unlocking the shop.

## Tests

- **`CireJunglePacks::RunTests`**, run inside `CireRouteEditor::RunTests` (`CIRE_ROUTE_TOOLS_PASS`, part of `Tools/RunExpansionChecks.py`):
  - the tier ability counts, 2/3/5/full
  - JunglePacks.json parsing
  - 12,000 default compositions all valid and deterministic
  - every override clamps to a valid composition
  - every race has every role
  - every unit reaches the kit floor, and its T1–T4 loadouts are 2/3/5/kit
  - borrowed skills never enter the wave pool
  - race packs stay in their race
  - the formation stays inside the radius
- **Route-tools checks:**
  - 120 packs parse and round-trip
  - 3,000 packs replicate in chunks and unpack exactly
  - identical realms are sent once
  - 120 mirrored packs per team place, number past 16 and renumber when pack 50 is removed
  - type and composition sync to twins
  - legacy packs load as Mixed
  - 119 packs per realm compile, pass the runtime rules and the network budget, and apply live
  - spawned packs follow the composition, with one leader and the tier's ability count
  - recall points compile
  - extra objectives and Play Bounds are explained by Validate
- **`Tests/ItemRulesTests.cpp`:** `RouteSchedule` with 150 bays.
- **Recall checks** (in `CireProgression::RunSmoke`, `CIRE_PROGRESSION_PASS`, part of the expansion gate): the channel, damage and movement interrupts, the nearest Recall Point of the team, the fallback to the base when the team has none, and the 120 s cooldown from Items.json.
- **Town probe** (`Tools/RunJunglePackProbe.py`, `-CireJungleProbe`): 40 packs of mixed tiers and types in both realms on the town, every composition valid, no errors (`CIRE_JUNGLE_PROBE_PASS`).
