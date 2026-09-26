# In-game developer tools

Open **F9 → Developer** in a development standalone session. Network clients,
listen/dedicated servers and shipping builds cannot enable these overrides.
Changes remain drafts until **Enable overrides → Apply live**. Saving a profile
does not enable it in future sessions; use Load, review, then Apply.

| Page | Real gameplay controls |
| --- | --- |
| Match | Prep, arena, recovery and between-wave durations; waves per cycle; world time scale; pause new wave spawning; freeze the phase clock; explicit team-life reset and normal phase transition |
| Spawn / stats | Future wave count, NPC health/damage, spawn origin/formation, newly started cooldowns; manual next-wave request |
| Effects | Future projectile speed/collision radius, warning/lifetime multipliers, construct/summon health, category-specific world/player/monster/protection/wall collision policies |
| Balance lab | Actual wave or arena combat with role bots, optional player participation, counts, wave tier, NPC role and bounded runtime |
| Replays | Start/stop Unreal recording, refresh saved recordings, page through local metadata and play a finished recording |
| Paths (nav-paths) | Route and navmesh status, per-segment reachability, world/minimap navmesh toggles, and the in-world path editor: drag waypoints, the breach, challenge bays and the goal zone; lane width; apply live, save/load `BattlefieldRoutes.json`, defaults. See `Docs/Navigation.md` ; **MAP LAYOUT EDITOR** opens the setter-based layout editor (`Docs/MapLayout.md`, also `RouteEditor.cmd`) |

The first Apply captures original match timing, wave count and world speed.
**Restore original tuning** restores that snapshot and neutralizes future-spawn
overrides. It does not rewind combat or return spent resources. Timing edits keep
elapsed time; shortening a phase below elapsed time schedules its normal
transition on the next tick. Phase advance uses the normal GameMode entry,
cleanup and reward decisions. Forcing a PvE cycle clear destroys its remaining
wave creeps without awarding kill rewards. Match mutation controls require the
balance lab to be stopped first so its restoration snapshot remains coherent.

Spawn points stay in their own lane. Extreme origin/spacing/count combinations
clamp at the lane boundary; use a smaller formation to avoid crowding. Health and
damage changes affect newly configured monsters, not existing health pools.
Effects are adjusted once at their validated authoritative spawn entry points.
The saved profile is `Saved/Config/CireDeveloper.ini` (version 1); invalid or
nonfinite values fail transactionally instead of partially changing the match.

Balance lab scenarios suspend normal match actors and use real hero/NPC combat,
not spreadsheet approximations. Stopping restores the original roster and player
pawn. Measurements include damage, healing, DPS/HPS, surviving units, tank target
share, threat lead and victim changes. Completed/stopped reports are written to
`Saved/BalanceLab`. Live tuning affects a fixture only where its normal combat
spawn path consumes that tuning; authored ability parameters remain in the
separate skill tuning catalog.

Replays use Unreal's local replay system under `Saved/Demos`. Playback is rejected
in a live multiplayer session. A spectator HUD offers pause, ±10-second seek,
speed and exit. Keyboard controls are Space (pause), arrows (seek), +/- (speed),
WASD/QE (camera), RMB (look), F9 (options), Escape (return). Replay files record
network-replicated gameplay; local-only UI preferences and nonreplicated cosmetic
events are not guaranteed to reproduce exactly.

Validation includes native phase-duration semantics and
`CireDeveloperTools::RunValidationSmoke` for malformed limits, neutral unauthorized
queries and elapsed-time preservation. Runtime recordings and balance metrics
must be checked in a built Unreal session; a successful source build alone does
not validate their playback or measured balance.
