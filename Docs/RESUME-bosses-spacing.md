# RESUME — feat/bosses-spacing (Playtest 6, section H)

Worktree `F:\CiresTeamSurvival-agents\cts-bosses-spacing`, ports 17570-17579 (re-gate 2026-09-28: 17925-17949).

## Done
- **Boss size per category + per marker (Eric 2026-09-28, "5x is too big for some bosses; wave bosses were already big"):**
  `Content/Data/UnitSpacing.json` `boss.sizeMultiplier` is split into `boss.outdoorBoss` (default **5**), `boss.waveBoss`
  (lane/wave bosses, default **1.0** = the size they had before this branch) and `boss.packLeaderBoss` (boss-classified pack
  leaders, default **2**). The old `sizeMultiplier` key is still read as the outdoor-boss size when `outdoorBoss` is absent.
  Live: `cire.Spacing outdoorBoss|waveBoss|packLeaderBoss <v>` (`bossSize` = alias of outdoorBoss) or `cire.Spacing reload`;
  range 0.2-10. Category = `CireUnitSpacing::BossBodyOf` (outdoor pack-id block first, then lane boss, else pack leader).
- **Per-marker boss model SIZE** on Boss Spawn markers in the route/layout editor, under the per-marker "HP x" stepper:
  "SIZE x" stepper (steps 0.1, also Shift+[ / Shift+] with a Boss marker selected), JSON key `"size"` (default 1, clamp
  0.2-3, written only when not 1), mirrored twins follow, compiled into both realms' `FCireRouteSpot::SizeScale`, twin-sync
  validation covers it. The outdoor boss spawn copies it into the replicated `UCireNPCState::BodySize` so every peer draws the
  same size. **Final outdoor scale = previous size x outdoorBoss x marker size**; wave boss = previous x waveBoss; boss pack
  leader = previous x packLeaderBoss. Capsule cap (Large agent) and the body reach bonus are computed from the final scale,
  so they work for any size (a native test retunes a wave boss to 8x and 0.3x live).
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
- **Tests (updated for the split):** wave boss at default = unchanged legacy scale, boss pack leader = 2x legacy, outdoor boss
  with marker size 0.6 = 3x legacy, outdoor boss marker 1 = 5x (giant-body checks run on it), old-key fallback, per-key
  parsing, live resize keeps the capsule cap/reach bonus; `CireLayoutWiringTests` round-trips/mirrors/clamps/compiles the
  marker `"size"`; `CireMonsterArtTests` pack-leader height ratio uses packLeaderBoss; the probe checks each outdoor boss =
  outdoorBoss x its marker size and the marching wave boss = waveBoss.
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
- F8 page for the spacing values (JSON + console + the route editor's per-marker SIZE x for now; an F8 row was not cheap:
  the developer page edits a draft struct that does not own UnitSpacing).
- The Results numbers below were measured with every boss at 5x (before the 2026-09-28 split); the re-gate probe run is logged
  under "Gate logs".
- Hero capsules unchanged (40): only monsters clump; a hero radius change also touches CireHero/CireMobility tests.
- Boss walk speed unchanged: a 5x body at the same speed strides slowly (lumbering). Tune per boss if Eric wants.
- Click-selecting the upper body of a giant: the capsule is only 3 m tall; the raid bar and Tab targeting cover it.

## Assumptions / questions for Eric
- 2026-09-28 split: waveBoss default 1.0 (their pre-branch size), packLeaderBoss 2, outdoorBoss 5 x marker size. The marker
  SIZE only affects outdoor (Boss Spawn) bosses; wave/pack-leader bosses have no marker. Marker size clamp 0.2-3; the category
  multipliers accept 0.2-10 (the old key allowed 1-10). Eric's MapLayout files were not edited: markers without "size" = 1.
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
- 2026-09-28 boss size split: `CireMapLayout.h/.cpp` (marker SizeScale, SetBossSize, "size" save/load, twin sync + validation,
  compile), `CireLanePath.h` (FCireRouteSpot::SizeScale), `CireLayoutEditorHUD.cpp` (SIZE x stepper, Shift+[ ]),
  `CireNPCState.h/.cpp` (replicated BodySize), `CireOutdoorBosses.cpp` (copies the marker size), `CireLayoutWiringTests.cpp`,
  `CireMonsterArtTests.cpp` (pack leader ratio)

## Gate logs (2026-09-28 re-gate: main 8b323888 merged cleanly + the boss size split; ports 17925-17949)
- Build: Result: Succeeded
- Native: PASS `Saved/ExpansionChecks/20260928T075931897429Z/report.json` (`CIRE_UNIT_SPACING_TESTS_PASS checks=26`,
  `CIRE_LAYOUT_WIRING_PASS checks=62`, `CIRE_MONSTER_ART_PASS`, `CIRE_COMBAT_EXPANSION_PASS`)
- Network: PASS `Saved/NetworkSmoke/20260928T080318955667Z/report.json` (first run failed the client strafe step once —
  hero movement, untouched here; rerun passed: environmental)
- Interface: PASS `Saved/InterfaceSmoke/20260928T080438168255Z/report.json`
- Boss spacing probe (town, no shots): PASS `Saved/BossSpacing/20260928T080647Z/report.json` — crowd 8 -> 3 overlapping pairs
  (62% fewer), nearest 107 -> 119 cm; 12 outdoor bosses marker size 1 x outdoorBoss 5 = scale 7.5-8, capsule 72/150, on nav,
  path to spawn; wave boss (waveBoss 1) scale 1.35, capsule 62/119, marched 67.8 m in 45 s, longest stall 0.4 s.

## Gate logs (after merging main a8f83d78)
- Build: Result: Succeeded (`Saved/build7.log`)
- Native: PASS `Saved/ExpansionChecks/20260928T065100216529Z/report.json` (earlier runs under load hit the 240 s exit
  timeout after printing CIRE_COMBAT_EXPANSION_PASS — environmental)
- Network: PASS `Saved/NetworkSmoke/20260928T065426024177Z/report.json`
- Interface: PASS `Saved/InterfaceSmoke/20260928T065540161611Z/report.json`
- Boss spacing probe (town + shots): PASS `Saved/BossSpacing/20260928T065815Z/report.json`
- WowUI gallery (not a required gate): RaidBoss passes the default-panel overlap and centre checks; the only failure is
  "action bar stays centred" (Skills panel, untouched by this branch) — `Saved/WowUIGalleryChecks/20260928T070357170189Z/`
