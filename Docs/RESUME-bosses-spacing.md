# RESUME — feat/bosses-spacing (Playtest 6, section H)

Worktree `F:\CiresTeamSurvival-agents\cts-bosses-spacing`, ports 17570-17579.

## Done
- **Bosses 5x** (`CireUnitSpacing.*`, hooked in `CireNPCCombat` DesiredScale/ApplyBody): wave/lane bosses, outdoor world
  bosses and boss-classified pack leaders (`IsBossBody` = lane boss or classification Boss) are drawn
  `boss.sizeMultiplier` (5) x their previous size.
- **Nav approach (decision):** the giant body is visual; the collision capsule is capped to the existing **Large nav agent**
  (radius 72, height 300 — `Config/DefaultEngine.ini`), i.e. scaled radius <= 72 and scaled half height <= 150. `CireNav::NavData`
  picks the Large navmesh by capsule radius, so bosses path on the navmesh built for them and fit the streets and under the
  gates. Alternatives rejected: a full 5x capsule (radius ~250, 12 m tall) cannot fit town streets/gates and would need a new
  nav agent + navmesh rebuild; no capsule change (the old ~50 cm radius) would let the giant path through gaps its Large-agent
  lair checks never validated. `ApplyBody` keeps the feet on the capsule bottom (mesh shifted, `CacheInitialMeshOffset`,
  server lifts the actor when the capsule grows).
- **Reach at the body's edge:** `BodyReachBonus` = drawn radius (38 x scale) - capsule radius. Boss melee reach, its ability
  range decision and its own cones/stomps (radius/length) get it; `ACireHero::InRange` adds it for targets, so heroes hit a
  giant from its drawn edge instead of walking into its legs.
- **Nameplates** over the giant head (`PlateLift`, one argument in `CireHUDWow.cpp`).
- **Raid-boss bar** (`CireHUDBossBar.cpp`, layout panel `RaidBoss` at 546,88 480x118 under the match plate): big ornate bar
  for the top boss (portrait with boss spikes, Display-face name, subtitle Siege Host / World Boss / Pack Leader, % and a phase
  medallion, tall health bar with damage trail in the theme bar frame, theme ornament crown, phase ticks with diamonds at the
  boss's authored Enrage/ShieldWall thresholds or 75/50/25, cast bar or "Attacking YOU" + PHASE n/m), up to two slimmer stacked
  bars for more bosses, divider between. All pieces are CireUIStyle theme-layer calls (Frame/Card, Bar+BarFrame, Ornament,
  Divider, PortraitRing via DrawPortrait, Medallion, theme colours) so all 4 HUD themes restyle it. Click a bar to target;
  hover gives the unit tooltip; follows the "Boss / pack leader frames" option; shows in F10 layout editing.
- **Unit spacing:** monster capsule radius 38 -> 46 (still within the 48 Hero nav agent), melee reach +20, and melee units in
  reach sidestep crowding lane-mates (separation padding 30, strength 0.7) instead of freezing in a clump.
- **Data/live tuning:** `Content/Data/UnitSpacing.json`; console `cire.Spacing reload | legacy | <key> <value>` (applies
  live: scale/capsule are re-evaluated every tick).
- **Tests:** `CireUnitSpacingTests.cpp` (native, runs in `CireNPCCombat::RunSmoke` → RunExpansionChecks native).
  `RunOptionsGallery` default-panel overlap list now includes `RaidBoss`.
- **Probe:** `-CireBossSpacingProbe` (`CireBossSpacingProbe.cpp`, `Tools/RunBossSpacingProbe.py [--shots] [--citadel]`):
  crowd overlap before/after, giant world bosses on nav + Large-agent path to spawn, 45 s giant wave-boss march through town,
  captures per theme into `Saved/BossSpacing/<stamp>/`.

## Results
- Native: `CIRE_UNIT_SPACING_TESTS_PASS checks=21 boss_scale=6.75 capsule=72/150 reach_bonus=184` (wave boss).
- Probe, Citadel (`Saved/BossSpacing/20260928T054408Z/`): 16 melee units on one hero —
  **before (legacy) 11 overlapping pairs, mean overlap 15 cm, nearest 86 cm; after 4 pairs, 5 cm, 107 cm (64% fewer overlaps)**.
  5x Siege Host (scale 6.75, capsule 72/150, Large agent) marched 68 m in 45 s, longest stall 0.4 s.
- Probe, town with Eric's MapLayout (`Saved/BossSpacing/20260928T065815Z/`, PASS): crowd **before 7 overlapping pairs
  (16 cm), nearest 106 cm → after 2 pairs (2 cm), nearest 119 cm (71% fewer)**; all 12 outdoor bosses (both realms) drawn
  7.5-8x archetype scale (13-14 m tall), capsule 72/150, on the navmesh, Large-agent path to the player spawn; 5x Siege
  Host marched 103 m through the town in 45 s, longest stall 0.7 s.
- Captures (4 themes each): `Saved/BossSpacing/20260928T065815Z/siege_boss_<Theme>.png` (giant in a town street, raid bar
  with the Siege Host big + a slim second bar) and `world_boss_<Theme>.png` (Ursoth the Elder Bear at its lair, three
  stacked raid bars). Themes: GildedCitadel, Ironbound, ArcaneVeil, VerdantBloom.
- Tuning: capsule 46 + reach +20 + separation (pad 30, strength .7) measured together; the individual shares were not
  isolated (use `cire.Spacing <key> <value>` live to try variants).

## Not done / Next
- F8 page for the spacing values (JSON + console only for now).
- Hero capsules unchanged (40): only monsters clump; a hero radius change also touches CireHero/CireMobility tests.
- Boss walk speed unchanged: a 5x body at the same speed strides slowly (lumbering). Tune per boss if Eric wants.
- Click-selecting the upper body of a giant: the capsule is only 3 m tall; the raid bar and Tab targeting cover it.

## Assumptions / questions for Eric
- "5x bigger" = 5x the boss's previous drawn size (archetype/rank/wave size already applied), height and width.
- Collision stays Large-agent sized (see decision) — the giant visually overlaps walls/houses while pathing streets.
- Raid bar position: top centre under the match plate (movable in F10); up to 3 bosses (1 big + 2 slim).
- Phase ticks = the boss's authored enrage / shield-wall thresholds, else 75/50/25.

## Shared-file edits (small, additive)
- `CireHUD.h` (DrawRaidBossBars, RaidBossPhases, LastRaidBars), `CireHUD.cpp` (call after DrawBossFrames, panel help, F10 visibility)
- `CireUISettings.cpp` (default panel RaidBoss), `CireOptionsGallery.cpp` (overlap list)
- `CireHUDWow.cpp` (nameplate lift for monsters), `CireHero.cpp` (InRange + body bonus)
- `CireNPCCombat.cpp` (scale/capsule, melee reach, separation, giant area radius, smoke hook)
- `CireOutdoorBossProbe.cpp` (probe hook)

## Gate logs (after merging main a8f83d78)
- Build: Result: Succeeded (`Saved/build7.log`)
- Native: PASS `Saved/ExpansionChecks/20260928T065100216529Z/report.json` (earlier runs under load hit the 240 s exit
  timeout after printing CIRE_COMBAT_EXPANSION_PASS — environmental)
- Network: PASS `Saved/NetworkSmoke/20260928T065426024177Z/report.json`
- Interface: PASS `Saved/InterfaceSmoke/20260928T065540161611Z/report.json`
- Boss spacing probe (town + shots): PASS `Saved/BossSpacing/20260928T065815Z/report.json`
- WowUI gallery (not a required gate): RaidBoss passes the default-panel overlap and centre checks; the only failure is
  "action bar stays centred" (Skills panel, untouched by this branch) — `Saved/WowUIGalleryChecks/20260928T070357170189Z/`
