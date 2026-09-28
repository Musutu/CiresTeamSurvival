# RESUME: world-editor (feat/world-editor)

Worktree `F:\CiresTeamSurvival-agents\cts-world-editor`, branch `feat/world-editor` (from main 013e221e). Ports 18000-18049.
Task: an in-game WORLD EDITOR so Eric can choose which town buildings / props matches remove, with undo.

## Done
- `CireWorldEdit.{h,cpp}`: data model (named sets, stable ids `<realm sublevel>/<actor name>` + realm), JSON save/load,
  hash, savings, settings (`Content/Data/WorldEdit.json`), undo/redo document, mirror twins, protection (Guard) and
  validation (Issues), unit index / picking / reversible removal, runtime (load hooks, live switch, nav signature,
  replication hook), `cire.WorldEdit` console command, `-CireWorldEditProbe` / `-CireWorldEditDump`.
- `CireWorldEditTests.cpp`: native tests (in `-CireCombatExpansionProbe`).
- `CireWorldEditorState.h` + `CireWorldEditorHUD.cpp`: the WORLD tab (in progress: wiring into the layout HUD).
- Hooks: `CireTownMap.cpp` (BeginLoad / FilterLevelInstances / ApplyLevel / LogSummary), `CireNavCache.cpp` (signature),
  `CireGame.h` + `CireWorld.cpp` (replicated set name + hash, client selects before streaming, OnRep live switch, probe tick).

## Not done / Next
- Wire the WORLD tab into `CireLayoutEditorHUD.cpp` / `CireHUD.h` / `CireLayoutEditorState.h`; build.
- Dump the town's units (`-CireWorldEditDump`), author `Content/Data/WorldEdits/ProbeTest.json`, `Tools/RunWorldEditProbe.py`.
- Docs/WorldEditor.md; gates.

## Assumptions / questions for Eric
- (filled in as the work goes)

## Shared-file edits
- `CireTownMap.cpp`: 4 one-line hooks next to the trim's.
- `CireNavCache.cpp`: `CireWorldEdit::Signature()` in the cache key.
- `CireGame.h` (ACireWorld): `WorldEditHash`, `WorldEditSet` (ReplicatedUsing=OnRep_WorldEdit), `OnRep_WorldEdit()`.
- `CireWorld.cpp`: replication, BeginPlay (server sets / client selects), Tick (probe), OnRep.
- `CireCombatExpansionProbe.cpp`: `CireWorldEdit::RunTests`.

## Gate logs
- (pending)
