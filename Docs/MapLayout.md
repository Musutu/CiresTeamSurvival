# Map layout editor

`RouteEditor.cmd` opens the map layout editor. It's the tool for placing spawns, monster paths, challenge packs,
vendors, the objective and the rest in the town. It runs as a clean **edit mode** (`-CireRouteEdit`). Waves, monsters,
the draft, shop, economy, prep timer, threat, the combat HUD and win/lose all stay dormant. You get the town, your
champion (walk and run both realms) and the editor. Game code checks `CireRouteEditMode::IsActive()`.

The same editor opens inside a development match from F8 > Developer > Paths > **MAP LAYOUT EDITOR**.

Once `feat/medieval-kingdom` lands, `RouteEditor.cmd` passes `-CireTown` and opens the medieval pack town directly.

## One layout, two realms, two teams

- Both realms share one town layout: **DAYLIGHT** (Team 1) and **DARKNIGHT** (Team 2).
- Every marker is authored once, in **realm-local** centimetres. Each realm places it through its frame:
  - `CireLanePath::ToWorld(Realm, Local, Z)` and `ToLocal` / `RealmOrigin`.
  - This branch uses a stub provider: the procedural town's side-by-side origins (0, -2100) and (0, +2100).
  - `feat/medieval-kingdom` supplies the pack town's data-driven frames behind the same three signatures.
- Every marker has an **owner**: Team 1, Team 2 or Shared (JSON `"team"`: 1, 2, 0).
  - A team marker lives in its team's realm.
  - A shared marker shows in both realms.
- Monster spawns and monster paths also have a **target**: the team their waves attack.
  - Changing a spawn's target also changes the target of its paths.
- **Mirroring** is on by default. A Team 1 marker placed in DAYLIGHT gets a Team 2 twin at the same realm-local spot in DARKNIGHT, with the target swapped: "Monsters -> T1" becomes "Monsters -> T2".
  - `pair` links the twins.
  - Every edit to one twin is copied to the other.
  - Turning **Mirror off** (Y) breaks the pair, so the layout can be asymmetric on purpose.
- Gizmos and labels show the team: a blue ring and "T1 ..." for Team 1, a red ring and "T2 ..." for Team 2, gold for shared markers.

## Setters

The setter table is data-driven: `Content/Data/MapMarkerTypes.json`, with a built-in copy as the fallback.

Each setter is a skill on the editor's action bar, with its own icon, colour and in-world gizmo. The keys are **action
bar 1**'s bindings, so they follow the player's keybinding settings. The defaults are:

| Key | Setter | Gizmo | Notes |
| --- | --- | --- | --- |
| 1 | Player Spawn | pillar + ring + facing arrow | up to 10 per team (one per hero slot), radius = spawn area |
| 2 | Monster Spawn | pillar + facing arrow | named; target team; links to its paths |
| 3 | Monster Path | chained line with direction chevrons | named; starts at a monster spawn; ends in the target's objective or **merges** into another path; label shows length and walk time |
| 4 | Challenge Pack | ring (radius) | numbered per team (PACK n), radius 2-15 m, tier 1-10; 16 per team at most |
| 5 | Shop / Vendor | NPC silhouette + sign + stall | vendor type from `Vendors.json` (built-in: weaponsmith, armory, arcane); sign and stall are separate handles |
| 6 | Objective / Castle Defend Point | ring | one per team: where waves head and what they attack |
| 7 | Boss / Pack Leader Spawn | pillar + facing arrow | named |
| 8 | Rift / Portal / Arena Entrance | ring + facing arrow | where teams go to PvP |
| 9 | Respawn Point / Graveyard | pillar + facing arrow | where dead heroes come back |
| 0 | Play Bounds | polygon | one shared polygon; each press adds a corner |
| Shift+1 | No-Spawn / Blocker Zone | crossed ring | radius |

### Placing markers

**Walk view** (the default):

- Walk the town.
- Pressing a setter key arms it and shows a reticle at the cursor.
  - Click to place the marker there.
  - Press the key again to place it at your feet, facing where you look.
- Monster Path and Play Bounds work differently: **each press chains a point to the previous one**, at your feet. Clicking chains a point at the cursor.

**Map view** (M): a top-down camera.

- Pan with WASD, rotate with Q/E, tilt with Home/End, zoom with the wheel.
- Setters place at the cursor.

### Paths

- A path starts at the monster spawn that is selected when you begin. Otherwise it starts at the nearest spawn of the team within 40 m. The inspector's **NEW PATH FROM HERE** also starts one.
- Chaining a point inside the target's objective ends the path.
- Chaining a point within 2.5 m of another path of the same team merges into that path and snaps onto it.
- Enter finishes a chain.
- The next press after a finished path starts a new one.

### Vendors

A vendor is a group of three handles, each with its own facing:

- the **NPC** (spawn point and facing)
- the **sign** anchor (post and board at a height)
- the **stall** footprint (a rectangle)

Default offsets come from the vendor type: the sign 150 cm to the side at 250 cm, the stall 140 cm in front.

- Select the NPC, SIGN or STALL handle, either in the world or from the inspector.
- Replace, rotate and remove then act on the handle you selected.
- The NPC handle moves and turns the whole group.
- Removing the sign or stall handle puts it back at its default spot.

## Editing

| Action | Key | Button |
| --- | --- | --- |
| Select | click a marker or path point; aim and press **F** (nearest within 6 m) | list row |
| Replace (move) | **R**, then click the new spot (or R again: your feet) | REPLACE |
| Remove | **X** / Delete (packs renumber; a path re-chains its neighbours) | REMOVE |
| Rotate | **,** / **.** (Shift: 45 degrees) | FACING - + |
| Radius | **[** / **]** | RADIUS - + |
| Tier | **-** / **=** | TIER - + |
| Owner (T1 / T2 / Shared) | **O** | T1 T2 SHARED |
| Target team | **T** | WAVES ATTACK |
| Mirror to the other team | **Y** | MIRROR |
| Vendor type | **K** | TYPE |
| Rename | **N** | RENAME |
| Undo | **Ctrl+Z** / Backspace | UNDO |
| New layout | **Ctrl+N** | NEW |
| Clear (the armed setter's type, the list's type filter, or everything) | | CLEAR |
| Validate | **V** | VALIDATE |
| Preview | **P** | PREVIEW |
| Apply | **Ctrl+S** | APPLY |
| Save as / Load a named layout | | SAVE AS / LOAD |
| Walk / map view | **M** | |
| Other realm | **G** | REALM |

**Panels:**

- **Setter bar:** WoW-style, bottom centre. Each slot shows how many markers of that type exist.
- **Marker list:** left.
  - Filter it by team (ALL / SHARED / T1 / T2) and by setter type.
  - Click a row to select it. Double-click it (or use FLY TO) to go there.
  - The team filter also filters the world gizmos.
- **Inspector:** right. It shows the selection, each path's length and walk time, validation problems (click one to go to it) and the autosave state.

## Validation

Validation runs lightly on every edit. **VALIDATE** adds the navmesh checks. It flags:

- **Per team:**
  - no player spawn
  - no monster spawn that targets the team
  - no objective
  - a monster spawn without a path
  - a path that doesn't start at a spawn
  - a path that doesn't reach **its target team's** objective, following merges (the objective must stand in the path's own realm)
- **Mirrored pairs:** a missing twin, or a twin out of sync.
- **Bounds:** markers outside the play bounds.
- **Navmesh:** markers off the navmesh (reported per realm), and path segments no monster can walk.
- **Vendors:** an unknown vendor type, a vendor type missing from a team, a stall standing in a monster path, and a sign clipping into town geometry.
- **Challenge packs:** a tier outside 1-10, a radius outside 2-15 m, or more than 16 packs per team.

## Preview and Apply

**PREVIEW** spawns a monster for every path in every realm it shows in (AI off). Each one walks the path on the
navmesh at the wave march speed, which is the mean `moveSpeed` of the wave composition, about 195 cm/s. The tag over
each monster shows its realm, metres walked, elapsed time and ARRIVED. Walk times in the inspector use navmesh path
lengths after a validation, and straight lines before one.

**APPLY:**

1. Writes the layout to `Content/Data/MapLayout.json`.
2. Writes the vendor markers to `Content/Data/TownVendors.json`.
3. Compiles what today's game runs and applies it live. The same compiled result is saved to `CireLanePath::DataPath()`:
   - Per realm, the **first path** that targets that realm's team becomes the march route (spawn -> path -> objective).
   - Its challenge packs, with their radius and tier, become the realm's packs.
   - The Team 1 objective becomes the goal zone.

Apply reports anything that can't be expressed yet. Today's waves march down one route per realm; extra paths are
kept in the layout for multi-path waves.

Your work **autosaves** to `Saved/MapLayoutDraft.json` 0.75 s after every change. The editor restores it on the next
launch. Named layouts are kept in `Content/Data/MapLayouts/<name>.json`.

## Files

`MapLayout.json` (schema 1, realm-local centimetres, degrees):

```json
{ "schemaVersion": 1, "units": "centimeters", "frame": "realm-local", "name": "Castle Town v1",
  "markers": [
    { "id": "monsterSpawn_4", "type": "monsterSpawn", "name": "The Breach", "team": 1, "target": 1, "x": 43200, "y": 0, "yaw": 180, "mirror": true, "pair": "monsterSpawn_5" },
    { "id": "monsterPath_8", "type": "monsterPath", "name": "Main road", "team": 1, "target": 1, "points": [[41500,0],[40800,-550]], "from": "monsterSpawn_4", "mirror": true, "pair": "monsterPath_9" },
    { "id": "challengePack_12", "type": "challengePack", "team": 1, "x": 6600, "y": -900, "radius": 380, "tier": 2, "mirror": true, "pair": "challengePack_13" },
    { "id": "vendor_14", "type": "vendor", "name": "", "team": 1, "x": 5200, "y": -1000, "yaw": 90, "kind": "weaponsmith",
      "sign": { "x": 5050, "y": -1000, "yaw": 90, "height": 250 }, "stall": { "x": 5200, "y": -860, "yaw": 90, "width": 220, "depth": 120 }, "mirror": true, "pair": "vendor_15" }
  ] }
```

`TownVendors.json` is the shared vendor-layout schema that `feat/vendors` reads (realm-local cm, degrees; sign `pos` is
`[x, y, height]`):

```json
{ "schemaVersion": 1, "units": "centimeters", "frame": "realm-local",
  "vendors": [ { "id": "vendor_14", "vendorId": "weaponsmith", "name": "", "team": 1, "pair": "vendor_15",
                 "npc": { "pos": [5200, -1000], "yaw": 90 },
                 "sign": { "pos": [5050, -1000, 250], "yaw": 90 },
                 "stall": { "pos": [5200, -860], "yaw": 90, "size": [220, 120] } } ] }
```

`Vendors.json` (from `feat/vendors`, optional here): `"vendors": [{ "id", "name", "sign": { "offset": [forward, right], "height" }, "stall": { "offset": [forward, right], "size": [width, depth] } }]`.

`BattlefieldRoutes.json` changes are described in `Docs/BattlefieldRoutes.md` (1-16 challenge packs, the shared `"route"` form).

## Checks and captures

- `CireRouteEditor::RunTests` (part of `-CireCombatExpansionProbe`, logs `CIRE_ROUTE_TOOLS_PASS`) covers:
  - the 1-16 pack generalisation: parse, rules, writer, replication, live visuals, spawning and pack ids
  - the realm frames
  - the layout model: typed markers, owners and targets, mirroring and sync, links, merges, renumbering, re-chaining, vendors, per-team validation, JSON and compile
- `Tests/ItemRulesTests.cpp` covers `RouteSchedule` for 1-16 packs.
- `Tools/RunLayoutGallery.py` runs the editor through a scripted session and saves seven captures to `Saved/LayoutGallery/<time>/`. It writes no data files.
