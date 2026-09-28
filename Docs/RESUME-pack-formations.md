# RESUME: feat/pack-formations (Playtest 6, section B)

Worktree `F:\CiresTeamSurvival-agents\cts-pack-formations`, ports 17510-17519. Spec: `Docs/EricFeedback/2026-09-27/PLAYTEST-6.md` §B
and `pack-formations.png`. Full design: `Docs/JunglePacks.md` (Tiers, Composition rules, Formations).

## Done
- **T3/T4 spawn bug: root cause and fix.** `JunglePacks.json` gated authored tiers behind `unlockRound` / `unlockWave`:
  T2 at cycle 1 wave 3, T3 at cycle 2 and T4 at cycle 3. `CireLayoutRuntime::RestartOnLayout` (Alt+F5 after route editing)
  resets to cycle 1, so a playtest never saw T3/T4 packs; `CireProgression::JungleSchedule` -> `RouteSchedule` -> `BayTier`
  returned 0 for them. The layout editor, file and compile were fine; the packs just never became due. The fix opens every
  tier at cycle 1 wave 1 (data + built-in). The unlock knobs stay in the JSON if Eric ever wants gating back.
- **Probe:** `-CireJungleProbe` (`Tools/RunJunglePackProbe.py`) now starts by loading Eric's committed `Content/Data/MapLayout.json`,
  compiling it over the route file, applying it live, doing the Alt+F5 reset (cycle 1) and calling the REAL
  `ACireGameMode::SpawnPacks`. It fails unless every authored pack spawns in both realms at its tier, T1..T4 all present,
  tanks ahead of healers, and members facing the pack facing. It logs `CIRE_JUNGLE_PROBE_ROOTCAUSE`: what the old unlocks would
  have spawned. It then runs the old 40-pack stage with sizes up to 8 and caster lines.
- **Max pack size 8**, tanks 1-3, healers 1-3, DPS 1-6, always >= 1 of each.
- **DPS kinds:** melee / physical ranged / **ranged caster** (`RoleOf`: caster/support without heal -> caster, ranged -> ranged, else melee).
  Composition `[t,h,anyDps,melee,ranged,caster]`; the legacy `[t,h,d]` (Eric's layout) still reads, as "any DPS".
- **Six preset formations** (3..8) from the drawing, data-driven (`JunglePacks.json` `formations`, built-in fallback). Default compositions use
  the preset role counts for the size (T1 3-4, T2 4-6, T3 5-7, T4 6-8).
- **Facing:** the pack faces the nearest point of its realm's monster paths (on the path: the hero base). Front = tanks. Members spawn turned to it.
- **Stats:** base damage 200 for all roles; tank 1500 HP; healer 50 %, melee 75 %, ranged 65 %, caster 65 %. Per-tier health x (1/1.65/2.3/2.95)
  and damage x (1/1.1/1.25/1.4); global health / damage. Leader x1.5 HP kept.
- **Live editing:** F8 > **Packs** page (new, `CirePackStatsPage.cpp`) and the map layout editor's Challenge Pack inspector (**PACK STATS**).
  Changes rescale every living pack monster (health fraction kept); SAVE writes `JunglePacks.json`.
- Inspector composition rows: TANKS, HEALERS, DPS (ANY), MELEE DPS, RANGED DPS, CASTER DPS.
- Network: packs are 4 ints each now (DPS kinds), magic 0x4A51.
- Native tests (`CireJunglePacks::RunTests`): clamp/valid 3..8, comp JSON round trip, stats shares, unlocks open, formations vs drawing,
  tanks-ahead offsets, caster pools. Progression and route-tool tests updated for the new unlocks / 4-int wire.

## Not done / Next
- Gates: see "Gate logs" below (fill in on each run).
- Inspector height: the pack section grew (~6 rows). If it clips on small screens, fold PACK STATS behind a toggle.

## Assumptions / questions for Eric
1. **Unlocks:** every authored pack spawns from wave 1 whatever its tier (your placement is the progression). Gating by cycle is still possible in `JunglePacks.json` (`unlockRound` / `unlockWave`).
2. **Per-tier defaults** keep the old tier growth: HP x1 / 1.65 / 2.3 / 2.95, damage x1 / 1.1 / 1.25 / 1.4. T1 numbers are exactly yours (tank 1500, 200 damage).
3. **No per-round health growth** on packs any more (the old curve added +12 % per cycle); promotions still raise tiers in later cycles.
4. **Damage 200** replaces the old challenge damage (24 x elite multiplier), so packs hit far harder than before. Scale with F8 > Packs > Global damage x.
5. **Facing** = toward the nearest monster path. No per-pack manual facing yet. Say if you want a FACING stepper on packs.
6. **Formation spacing** = half the pack radius per unit (1.5-4 m), so a 4.5 m pack spaces monsters ~2.3 m apart.
7. The drawing's pack of 4 is asymmetric (back row shifted left); kept as drawn.
8. A race without casters fills caster slots with its other DPS (logged `KIND ... has no caster DPS`).

## Shared-file edits (keep small at merge)
- `CireLoot.cpp` `SpawnBay`: formation offsets + facing yaw, `ApplyTier(..., bLeader)`, the leader HP line moved into ApplyTier, log `facing=`.
- `CireLanePath.h`, `CireLanePath.cpp`: `HasCompOverride` via `IsZero`; route `comp` read/write via `CompFromJson` / `CompJson`.
- `CireLanePathLayout.cpp`: pack wire format 4 ints (magic 0x4A51).
- `CireDeveloperHUD.cpp`: 11th F8 page "Packs" (id 10), page buttons 56 px wide. `CireHUD.h`: `DrawPackStatsPage` declaration.
- `CireProgressionTests.cpp`: challenge gating expectations (every bay open at cycle 1).
- `CireRouteToolsTests.cpp`: 4-int wire count, composition with DPS kinds, role counting.
- `Content/Data/JunglePacks.json`: schema 2 (stats, formations, unlocks 1/1, tier health).

## Gate logs
(filled in below as they run)
