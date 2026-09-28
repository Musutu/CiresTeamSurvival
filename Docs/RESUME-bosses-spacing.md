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
(see "Gate logs" / probe section below; filled at the end of the run)

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

## Gate logs
(pending)
