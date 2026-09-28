# RESUME: world-editor (feat/world-editor) — DONE

Worktree `F:\CiresTeamSurvival-agents\cts-world-editor`, branch `feat/world-editor` (main 75a669b9 with game-profiles merged in).
Ports 18000-18049. Full doc: `Docs/WorldEditor.md`.

## Done
- `CireWorldEdit.{h,cpp}`: named world edit sets (`Content/Data/WorldEdits/<name>.json`), active set + tunables
  (`Content/Data/WorldEdit.json`), stable ids (`<realm sublevel>/<actor name>` + realm), JSON round trip, hash, savings,
  undo/redo document (batch = one step), mirror twins, protection (`Guard`), validation (`Issues`), unit index / picking
  (a hit resolves to its outermost pack Level Instance = the whole building, else the root actor + attached), reversible
  removal (hide + no collision + nav octree update, originals restored), runtime load hooks (queued LIs dropped / props
  destroyed before play), live switch (`ApplySet`, LIs removed at load reload on restore), nav cache signature,
  server->client replication (`ACireWorld::WorldEditSet/WorldEditHash`), `cire.WorldEdit <name>|off|list`,
  game-profiles registration (`CireGameProfiles::RegisterWorldEdit`; Default = the WorldEdit.json set).
- WORLD tab (`CireWorldEditorHUD.cpp`, `CireWorldEditorState.h`) in the map layout editor: tabs MAP LAYOUT | WORLD (B),
  hover highlight + tag (name / kind / size / pieces / PROTECTED), click / Shift-click / drag box, Delete / X remove,
  R restore, Ctrl+Z / Backspace undo, Ctrl+Y / Ctrl+Shift+Z redo, H ghosts, Y mirror, V validate (protected pieces,
  markers off the navmesh, paths BLOCKED / UNBLOCKED / shortcut vs the tab's baseline), Ctrl+S save + activate, SAVE AS,
  LOAD, SET OFF, Ctrl+T TEST, F fly to, REMOVED list with per-row RESTORE, RESTORE ALL, savings readout, draft autosave
  (`Saved/WorldEditDraft.json`), icon action bar (CireUIStyle).
- Tests: `CireWorldEditTests.cpp` (55 checks in -CireCombatExpansionProbe), `Tools/RunWorldEditProbe.py` (dedicated
  server + client, real town, `ProbeTest.json`), `Tools/RunWorldEditGallery.py` (scripted WORLD tab captures; PNGs deleted).

## Gate logs (after merging main 75a669b9)
- Build: `Saved/build5.log` Result: Succeeded
- Native: PASS `Saved/ExpansionChecks/20260928T101313461224Z/report.json` (CIRE_WORLD_EDIT_TESTS_PASS checks=55)
- Network: PASS `Saved/NetworkSmoke/20260928T101510773920Z/report.json`
- Interface: PASS `Saved/InterfaceSmoke/20260928T101556694983Z/report.json`
- World edit probe: PASS `Saved/WorldEditProbe/20260928T101732206034Z/report.json` — server and client 8/8 gone at load,
  twins 2/2 kept, live switch-off reloads 5/5 buildings, navmesh changed in 27/180 samples; ProbeTest saves 3,112 actors /
  ~3,107 draw calls.
- Gallery: 5 captures (selected / removed / ghosts / undo) checked, then deleted.

## Not done / Next
- Nothing blocking. Possible follow-ups: a translucent ghost material (ghosts are the real meshes + red outline now);
  applying a game type's set before the town streams (today the game type applies after the load, so the server hides
  that set live; clients load it at stream time).

## Assumptions / questions for Eric
- Unit = the pack's outermost Level Instance (a whole building with interior props), else a top-level actor with what is
  attached. Loose props in the realm sublevels are their own units (5,710 units in both realms).
- Mirror on by default; the twin is the same id in the other realm.
- Protected (cannot remove): a spawn / pack / vendor (NPC, sign, stall) / objective / boss / respawn / recall / rift
  marker inside the footprint (+1.5 m), or a Castle / Gate / Keep piece on a monster path. Warn only: a path through a
  house, a castle piece elsewhere. Tunable in WorldEdit.json.
- The active set lives in its own file (`Content/Data/WorldEdit.json` "active"), not CastleTown.json. SAVE activates.
- Clients load the set file from their own checkout (as with MapLayout.json); a hash mismatch logs CIRE_WORLD_EDIT_MISMATCH.
- Game type "worldEdit": empty/Default = back to the WorldEdit.json active set (not "no edits").
- Draw calls are an estimate (mesh sections of visible primitives at removal time).
- Removing a building can expose what is under it (its foundation came with the LI); ghosts + VALIDATE help check.

## Shared-file edits
- `CireTownMap.cpp`: 4 hook lines next to the trim's (BeginLoad, FilterLevelInstances, ApplyLevel, LogSummary) + include.
- `CireNavCache.cpp`: `CireWorldEdit::Signature()` in the cache key + include.
- `CireGame.h` (ACireWorld): `WorldEditHash`, `WorldEditSet` (ReplicatedUsing=OnRep_WorldEdit), `OnRep_WorldEdit()`.
- `CireWorld.cpp`: replication, BeginPlay (server sets / client selects before streaming), Tick (probe/gallery), OnRep.
- `CireCombatExpansionProbe.cpp`: `CireWorldEdit::RunTests`.
- `CireHUD.h`: `TickWorldEditor()`, `DrawEditorTabs()` declarations.
- `CireLayoutEditorState.h`: `bWorldTab`, `WorldEdit` pointer.
- `CireLayoutEditorHUD.cpp`: tab strip, B key, WORLD tab dispatch, Sync in the layout tab, Esc, restore on close.
