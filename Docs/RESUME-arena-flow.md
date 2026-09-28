# RESUME: feat/arena-flow (Playtest 6 section D)

Worktree `F:\CiresTeamSurvival-agents\cts-arena-flow`, ports 17530-17539. Spec: `Docs/EricFeedback/2026-09-27/PLAYTEST-6.md` D.
Design and data: `Docs/Arenas.md` "Arena flow (September 27, Playtest 6)".

## Done
- PvP schedule `CireArenaFlow::PvPAfterWaves / IsPvPAfterWave` now reads `CireWaveDirector::Schedule(World).PvpAfterWaves`
  (waves-modes merged); Sudden Death waves never lead to an arena. Non-PvP cycle ends skip the prep/arena (`SkipArena`).
- Prep = `flow.prepSeconds` 30 + `flow.countdownSeconds` 7; portals beside every human open at prep start (not at T-12 s).
- Enter early (existing trigger); last human through -> bots follow -> countdown. Timer end -> `ServerPullAll` pulls everyone
  still in town (humans + bots) -> countdown. Replicated `ACireGameState::ArenaStage` (1 prep, 2 countdown) + `ArenaCountdownLength`.
- 7 s countdown: ALL THROUGH banner (existing) + new big centre countdown `ACireHUD::DrawArenaCountdown` (CireHUDPortal.cpp).
  Match plate / PvP Prep banner / portal prompt show only the 30 s prep part (`CireArenaFlow::PrepSecondsLeft`).
- +50 g per killing blow in the arena (`OnHeroKilled`, owner chain for pets/summons/constructs) with the gold popup.
- Winners 250 g split (+100 XP kept), stacking Arena Victor +15% PvE damage; losers stacking Arena Vanquished -15%; draw nothing.
  Applied only in `ACireMonster::TakeDamage`. Old power/loot per win off (`flow.legacyPowerLoot`).
- Icons + tooltips via the existing buff row (derived effects in `CireEffects::Gather`), rows in BuffModifiers/BuffVisuals.json.
- Tests: `CireArenaFlow::RunTests` (called from `CireArenaPortal::RunTests`, native gate), interface probe checks stage 1/2 + 37 s prep.

## Not done / next
- (fill after gates) see "Gate logs".
- waves-modes switch done (flow.pvpAfterWaves removed). `OnPhaseChanged(1)` overrides Waves.json prepSeconds with
  flow.prepSeconds + countdown for PvP preps (intended).
- The native run has no champions, so the icon + killing-blow checks in `CireArenaFlow::RunTests` are skipped there
  (logged CIRE_ARENA_FLOW_NOTE); the interface probe covers the portal flow end to end.
- Merge fix: main's CireMatch.cpp had `bReadyGateHold`/`ReadyGateLeft` DOREPLIFETIME swallowed by a `// waves-modes` comment;
  restored in the conflict resolution.

## Assumptions / questions for Eric
- "Loaded" = teleported into the arena (the arena is prebuilt hidden on every peer during prep).
- Countdown is 7 s after everyone is in; at timer end the pull happens at 30 s, so the max prep is 37 s.
- 250 g "split" = 250 / drafted team size (50 each at 5v5). Winners keep the old 100 XP.
- Buff and debuff stack additively: multiplier = 1 + 0.15 x wins - 0.15 x losses (a 1-1 team is back to x1; both icons show).
- The old arena reward (+3% power, +8% loot per win) is OFF because power also boosted PvP; `flow.legacyPowerLoot: true` restores it.
- Early entrants wait at their arena spawn (no damage possible until the arena phase).
- F8 prep-duration overrides are replaced by flow.prepSeconds on each PvP prep (edit Arenas.json flow or the author script).

## Shared-file edits (small, additive)
- `CireGame.h`: ACireGameState `ArenaStage`, `ArenaCountdownLength`, `Ember/DuskArenaBuffs/Debuffs` (+ DOREPLIFETIME in CireMatch.cpp).
- `CireMatch.cpp`: cycle end checks the schedule (SkipArena); ResolveArena uses `CireArenaFlow::AwardResult`.
- `CireHero.cpp`: `OnHeroKilled` hook in hero death; PvE multiplier in `ACireMonster::TakeDamage`.
- `CireEffects.cpp` (Gather: two derived ids), `CireBuffs.cpp` (KnownIds), `CireHUD.cpp` (match plate prep seconds),
  `CireHUDWow.cpp` (PvP Prep banner text/seconds + DrawArenaCountdown call), `CireHUD.h` (declaration),
  `CireInterfaceProbe.cpp` (two added conditions).
- Data: `Content/Data/Arenas.json` (+`flow`, countdown 7), `Tools/AuthorArenas.py` (`FLOW`), `BuffModifiers.json`, `BuffVisuals.json`.

## Gate logs
- 2026-09-28: first build hit a full F: drive (environmental); resumed after space was freed.
