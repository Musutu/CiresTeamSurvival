# RESUME: feat/waves-modes (Playtest 6, section C)

Worktree `F:\CiresTeamSurvival-agents\cts-waves-modes`, branch `feat/waves-modes`, ports 17520-17529.
Spec: `Docs/EricFeedback/2026-09-27/PLAYTEST-6.md` section C (+ `playtest-raw.txt`).

## Interface for feat/arena-flow: the PvP schedule (stable, committed early)

Data: `Content/Data/Waves.json` block `match` (a game-type preset may override `pvpAfterWaves`):

```jsonc
"match": {
  "totalWaves": 25,                   // regular waves; waves after this are Sudden Death
  "pvpAfterWaves": [5, 10, 15, 20],   // global wave numbers (1-based) followed by a PvP arena round
  "suddenDeath": { "health": 2, "damage": 2, "loopLastWaves": 5 }
}
```

C++ (`CireWaves.h`, namespace `CireWaveDirector`; `World` = the match world, server or client-side config):

| Call | Returns |
|---|---|
| `FCireMatchSchedule Schedule(const UWorld*)` | `TotalWaves`, `PvpAfterWaves` (sorted, unique), `SuddenDeathHealth/Damage`, `SuddenDeathLoop` |
| `bool IsPvpAfterWave(const UWorld*, int32 GlobalWave)` | true when a PvP round follows that wave |
| `int32 PvpRoundAfterWave(const UWorld*, int32 GlobalWave)` | 1-based PvP round number (1..4 by default), 0 = none |
| `int32 NextPvpWave(const UWorld*, int32 GlobalWave)` | next PvP wave at or after GlobalWave, 0 = none left |
| `bool IsSuddenDeath(const FCireWaveConfig&, int32 GlobalWave)` | GlobalWave > TotalWaves |

`GlobalWave` is `ACireGameState::Wave` (1-based, incremented when a wave starts). Suggested arena-flow use: when the
cycle's last wave clears (`CireMatch.cpp`, `S->CycleWavesDone>=S->WavesPerCycle`), enter prep/arena only if
`IsPvpAfterWave(World, S->Wave)`; otherwise start the next cycle directly. With the default data (5 waves per
cycle) the existing flow already runs arenas after waves 5/10/15/20, and also after 25, 30, ... (Sudden Death cycles);
gating those extra arenas is arena-flow's call.

Match end: `Waves.json cycles` is 5 (25 waves). While `match.totalWaves > 0` the cycle limit no longer ends the match
(small edit in `CireMatch.cpp`): Sudden Death waves continue until a team runs out of lives.

## Interface for feat/bonus-loot: the wave-type roll hook

`FCireWaveDef CireWaveDirector::RollWaveType(const FCireWaveConfig&, const FCireWaveDef& Planned, int32 GlobalWave, int32 Seed)`
in `CireWaves.cpp` is called for every live (non-smoke) wave just before it is queued. Default: returns `Planned`.
Extend it to swap in a bonus loot stage / special wave: never replace `Type == Boss`, stay deterministic in
`(Seed, GlobalWave)`. `Planned` already has packs expanded (rows carry `Pack`), damage flags and Sudden Death scaling.

## Done (built, latest main merged, all three gates PASS, 2026-09-28)
- Waves.json: 25 waves (5 cycles x 5), packs (1-5: 5x5, 6-25: 7x5-7), match schedule, Sudden Death, monster rules, live scale.
- Runtime: pack expansion (deterministic, per-pack path, pack gap), -20% speed, armored -50% / slow immune / 2x stun,
  damage-off (passive) units, fight-back packs, live scale rescaling living units, Sudden Death (loop waves 21-25, x2),
  RollWaveType hook, schedule API, presets (WavePresets.json: Standard / Hero TD / Hybrid), SelectPreset,
  console cire.WaveScale / cire.WavePreset, -CireWavePreset=<id>.
- UI: champion select GAME TYPE dropdown (host, before wave 1; ServerAction 11); F8 > Waves > MODES & SCALE page.
- Native tests added in CireWaveTests.cpp (packs, schedule, Sudden Death, presets, armored, passive, live scale, SelectPreset).
- Docs/Waves.md "Playtest 6" section.

## Not done / Next
- Eye-checked: GAME TYPE sits left of GAME MODE at 1920x1080 and 1280x720 (draft gallery shot 01 PASS both). The open
  dropdown list is not covered by a gallery state; check it by hand at the playtest.
- arena-flow's schedule adapter calls CireWaveDirector::Schedule at merge (coordinator); merge waves-modes first.

## Assumptions / questions for Eric
- Armored waves: -50% on top of the -20% all-wave speed (0.8 x 0.5 = 0.4 of their old pace). "Path blocking" = walls /
  constructs still stop them; armored marchers still walk through heroes as before.
- Damage-off (Hero TD) waves are not "armored": normal speed, can be slowed, normal gold (armored pays 2x).
- Waves are 5 packs of 5 (waves 1-5) and 7 packs of 5-7 (6-25) for every wave type, armored included; armored leak
  cost lowered 2 -> 1 per unit because a wave now has 25-49 of them.
- Per-unit health is unchanged although waves are 3-7x bigger: tune with the live scale (F8 > Waves) at the playtest.
- Sudden Death replays waves 21-25 in a loop at x2 health and damage (plus the normal cycle growth).
- Hybrid preset: packs 1, 4 and 7 fight back; boss waves (5/10/15/20/25) fight in full.

## Shared-file edits
- `CireGame.h`: `ACireGameState::WavePreset` (replicated FName).
- `CireMatch.cpp`: DOREPLIFETIME for WavePreset; cycle limit ignored while Sudden Death is on.
- `CireController.cpp`: `ServerAction(11, presetIndex)` = host picks the game type.
- `CireNPCCombat.cpp`: slow factor skipped for slow-immune units; damage-off units use the marcher branch.
- `CireCrowdControl.cpp`: Stun x `StunMultiplier`, Slow returns 0 on slow-immune units.

## Gate logs (branch = latest main a5e640e1 + waves-modes)
- Native: PASS (`CIRE_WAVES_TESTS_PASS checks=123`, `CIRE_COMBAT_EXPANSION_PASS`) Saved/ExpansionChecks/20260928T051220200633Z/report.json.
  Run with --timeout 480: at 240 the probe passed (~187 s) but editor shutdown under the Paragon asset-registry
  scan pushed it past 240 s (3 runs; pack-usage-3 saw the same). Paragon has since been unlinked.
- Network: PASS Saved/NetworkSmoke/20260928T051642250300Z/report.json (illegal action rejected, rejection_ack=1: probe intent intact).
- Interface: PASS Saved/InterfaceSmoke/20260928T051828528466Z/report.json (phases survival/prep/arena/recovery/survival).
- Draft gallery (picker eye-check): PASS Saved/DraftGalleryChecks/20260928T054323246193Z/report.json.
