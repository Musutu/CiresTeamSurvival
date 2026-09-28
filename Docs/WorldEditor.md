# World editor: choose which town pieces are removed (world-editor)

Eric: "we will need to make a world editor, so the houses and things we don't exactly need that many of can be removed in
game with an undo in case a remove was not wanted. Many buildings we don't use other than for visual. I'd like to choose
what's removed."

Code: `Source/CiresTeamSurvival/CireWorldEdit.{h,cpp}` (data, runtime, public API), `CireWorldEditorHUD.cpp` +
`CireWorldEditorState.h` (the WORLD tab), `CireWorldEditTests.cpp` (native tests), `Tools/RunWorldEditProbe.py`
(server + client town-load probe).

## Using it

1. Run `RouteEditor.cmd` (the map layout editor, `-CireRouteEdit`). Press **B** (or click the **WORLD** tab at the top).
2. Walk the town (walk view) or press **M** for the map camera (WASD pan, Q/E rotate, wheel zoom, G other realm).
3. **Hover**: the whole building under the pointer lights up (the pack Level Instance with every wall, roof and prop of
   it; a loose prop is the prop plus what is attached to it). The tag shows its name, kind, size and piece count.
4. **Click** selects, **Shift-click** adds / removes one, **drag a box** (best in map view) selects every piece inside it.
   The inspector shows the selection's name / kind / size, pieces, draw calls and whether it is protected.
5. **Delete** (or X) removes the selection. A box remove is **one** undo step.
6. **Ctrl+Z** undo (also Backspace), **Ctrl+Y** / **Ctrl+Shift+Z** redo, as many steps as you like.
7. The **REMOVED** list (left) shows every removed piece with a **RESTORE** button; **RESTORE ALL** is on the action bar.
   Click a row to select it, double-click to fly there.
8. **H** toggles **ghosts**: removed pieces show again with a red outline (no collision) so you can find them; click one
   and press **R** to restore it.
9. **Y** toggles **mirror** (on by default): removing or restoring a piece does the same to its twin in the other realm.
10. **V** validates against the map layout: pieces a marker depends on, markers that lost their ground or the navmesh,
    monster paths **blocked** or **unblocked** since the tab opened (the navmesh rebuilds around the changes first).
11. **Ctrl+S** saves the set (named on first save: `Content/Data/WorldEdits/<name>.json`) and makes it the **active** set
    every match loads. **SAVE AS**, **LOAD** (undoable), **SET OFF** (no set: the whole town). **Ctrl+T** saves and
    launches a real match on it (TEST).

The draft autosaves to `Saved/WorldEditDraft.json` (like the layout's draft). In the editor removal only **hides**
(nothing is destroyed), so everything can come back. Removed pieces stay hidden in the MAP LAYOUT tab too.

## Protection

`CireWorldEdit::Guard`. A piece is **PROTECTED** (Delete keeps it and says why) when a marker the game needs stands in
its footprint (+ `protectPad`): player / monster / boss spawn, challenge pack, vendor (NPC, sign or stall), objective
(and its radius), respawn, recall point, rift, in that realm. A castle / gate / keep piece (`protectWords`) on a monster
path is protected too. A monster path through a house is a clear **warning** (the piece goes; VALIDATE checks the
paths), as is any castle / gate piece elsewhere. Game actors (vendors, stalls, objectives, units) are never pickable:
only pieces of the pack's realm sublevels have ids.

## Data

`Content/Data/WorldEdit.json`:

| Key | Default | Meaning |
|---|---|---|
| `active` | `""` | The set matches load (`""` = the whole town). SAVE / SET OFF write it. |
| `mirror`, `ghosts` | `true`, `false` | The WORLD tab's defaults. |
| `protectWords` | `Castle, Gate, Keep` | Name words that guard a piece. |
| `protectPad` | `150` | cm around a footprint for the marker checks. |
| `boxSelectMaxSize` | `8000` | Box select skips pieces larger than this (cm): the mountain, the curtain walls. |

`Content/Data/WorldEdits/<name>.json`: `{"name", "map", "removed": [{"id", "realm", "kind", "label", "local", "size",
"actors", "drawCalls"}]}`. The key is **`id` + `realm`**: `id` = `<realm sublevel>/<actor name>` (for example
`SL_Houses/LevelInstance_17`), the pack's own sublevel without the `_CireRealm<N>` suffix CireTownMap gives each realm
copy. Actor names come from the pack's packages, so they are the same on every load, on the server and on every client,
and the twin in the other realm has the same id. `local` / `size` / `actors` / `drawCalls` are informative.

## Runtime

- **Load** (`CireTownMap::LoadRealms` / `PrepareRealmLevels`, next to the trim): the active set is frozen when the town
  starts streaming. A removed building (Level Instance) is dropped from the streaming queue before it loads (or unloaded
  if it already did: every nested piece goes with it); a removed prop is destroyed. Nothing is left behind: no
  collision, shadows, lights or ticking. Not in the layout editor itself (it shows the draft by hiding).
- **Navmesh**: the nav cache key carries the set's hash (`CireWorldEdit::Signature`), so a new set rebuilds the navmesh
  once and later loads reuse it.
- **Multiplayer**: every peer streams its own town copy, so every peer applies the set itself. The server replicates the
  set name and hash (`ACireWorld::WorldEditSet / WorldEditHash`); a client selects the same set before its town streams
  and loads it from its own `Content/Data/WorldEdits` (same checkout, like `MapLayout.json`). A different file logs
  `CIRE_WORLD_EDIT_MISMATCH`; a missing one runs no set (never another one).
- **Live switch**: `cire.WorldEdit <name>|off` (or `CireWorldEdit::ApplySet`) mid-match hides + un-collides the new
  set's pieces and updates the navmesh; pieces removed at load come back when they are buildings (their Level Instance
  loads again), props destroyed at load come back on the next load (`restart_to_restore` in the log). Clients follow
  through the replicated name.
- Logs: `CIRE_WORLD_EDIT_BEGIN`, `CIRE_WORLD_EDIT realm=`, `CIRE_WORLD_EDIT_SAVINGS set= units= actors= draw_calls=`,
  `CIRE_WORLD_EDIT_NOT_FOUND` (an id not in the loaded town: trimmed, or the pack changed).

## Public API (feat/game-profiles)

```cpp
#include "CireWorldEdit.h"
CireWorldEdit::ApplySet(World, TEXT("FewerHouses")); // before the town loads: the load applies it; after: live
CireWorldEdit::ApplySet(World, TEXT("off"));         // the whole town
FString Name = CireWorldEdit::ActiveSet();            // "" = none
```
A game type can call `ApplySet` from its setup (server); the choice replicates. `-CireWorldEdit=<name|off>` overrides
`WorldEdit.json` for one run; `-CireNoWorldEdit` disables it.

## Performance

Every entry records the pieces (actors) and draw calls (mesh sections) it held when it was removed in the editor. The
inspector's **THIS SET SAVES** line and the `CIRE_WORLD_EDIT_SAVINGS` log give the totals per match (both realms).

## Tests

- Native (`-CireCombatExpansionProbe`, `CIRE_WORLD_EDIT_TESTS_PASS`): stable ids, JSON round trip on disk, hash,
  undo / redo (batch = one step, multi-step, redo cleared by a new edit, LOAD undoable), mirror twins, protection
  (vendor, path warning, gate on a path, realm-specific markers), validation, settings, the nav cache key per set,
  reversible removal with attached pieces.
- `python Tools/RunWorldEditProbe.py --port 18010`: dedicated server + client on the real town with
  `Content/Data/WorldEdits/ProbeTest.json`; every removed piece gone on both, twins kept, live switch-off restores the
  buildings and the navmesh over them changes.
- `-CireWorldEditDump` writes every removable unit of the town to `Saved/WorldEditUnits.json`.
