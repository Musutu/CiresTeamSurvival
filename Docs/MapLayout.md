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
| 4 | Challenge Pack | ring (radius) | numbered per team (PACK n), radius 2-15 m, tier 1-4, pack type (race or Mixed), composition; **no limit** (Docs/JunglePacks.md) |
| 5 | Shop / Vendor | NPC silhouette + sign + stall | vendor type from `Vendors.json` (built-in: weaponsmith, armory, arcane); sign and stall are separate handles |
| 6 | Objective / Castle Defend Point | ring | one per team: where waves head and what they attack |
| 7 | Boss / Pack Leader Spawn | pillar + facing arrow | named |
| 8 | Rift / Portal / Arena Entrance | ring + facing arrow | where teams go to PvP |
| 9 | Respawn Point / Graveyard | pillar + facing arrow | where dead heroes come back |
| 0 | Play Bounds | polygon | one shared polygon; each press adds a corner |
| Shift+1 | No-Spawn / Blocker Zone | crossed ring | radius |
| Shift+2 | Recall Point | pillar + ring + facing arrow | named, radius; where Recall (Teleport to Base) takes the team's heroes (the nearest one) |

**No marker limits** (jungle-packs, Eric: "unlimited of any type"). Every setter places as many markers as you like.
Where the game only reads some of them, Validate says so:

- more than one Objective per team is an error: the game runs one goal zone
- more than one Play Bounds polygon is an error: the game reads the first
- more than 10 Player Spawns per team is a note: one per hero slot

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
| Tier (1-4) | **-** / **=** (also sets the next pack's tier) | TIER T1-T4; NEXT TIER when nothing is selected |
| Retier many packs | | COPY TO ALL PACKS / COPY WITHIN 30 m (inspector, pack) |
| Pack type (race / Mixed) | **K** (on a pack) | PACK TYPE < > |
| Pack composition | | TANKS / HEALERS / DPS - +, AUTO |
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
| Test this layout (apply, then a real match in a new window) | **Ctrl+T** | TEST |
| Spawn split (even / by path weight) | | SPLIT (inspector, monster spawn) |
| Path weight | | WEIGHT - + (inspector, monster path) |
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
- **Challenge packs:** a tier outside 1-4, a radius outside 2-15 m, or a composition that breaks the rules (1-2 tanks, 1-2 healers, 1-3 DPS, 3-6 monsters). There is no pack limit.
- **Runtime rules** (layout-wiring): Validate compiles the layout exactly as a match loads it and runs the route rules
  the match enforces, so whatever the game cannot use is flagged: points outside the realm, paths that end outside the
  goal zone or enter it early, too many paths or spawns, too much data to replicate ("The game cannot run this layout:
  ..."). It also flags a spawn or path that targets the other realm's team (waves attack the team of the realm they march
  in), a path that targets another team than its spawn, and T1 / T2 objectives that differ. Notes (not errors): paths
  closed into the objective, paths thinned to 64 points, extra rifts, blocker zones.

## Preview and Apply

**PREVIEW** spawns a monster for every path in every realm it shows in (AI off). Each one walks the path on the
navmesh at the wave march speed, which is the mean `moveSpeed` of the wave composition, about 195 cm/s. The tag over
each monster shows its realm, metres walked, elapsed time and ARRIVED. Walk times in the inspector use navmesh path
lengths after a validation, and straight lines before one.

**APPLY** (Ctrl+S):

1. Writes the layout to `Content/Data/MapLayout.json`, stamped with the map it was authored on (`"map": "castletown"` or
   `"procedural"`).
2. Writes the vendor markers to `Content/Data/TownVendors.json` and re-places the merchants at their new spots right away.
3. Compiles the layout exactly as a match will (see "What the game reads") and applies it live: paths, spawns, packs,
   the goal zone, hero/respawn/boss spots, the rift and the bounds.

Apply does **not** touch `CastleTownRoutes.json` / `BattlefieldRoutes.json` any more. `MapLayout.json` is the one
source of truth: every match compiles it at startup over the route file, which stays the provisional default and
supplies what a layout does not author (realm bounds, lane width, escort tuning). A layout that does not validate yet
is saved but not run: matches keep the route file and log `CIRE_LAYOUT_REJECTED` with the reason.

**TEST THIS LAYOUT** (TEST button, Ctrl+T) applies, then launches a real match on the layout in a new window
(`-CireLayoutTest`, same map as the editor): waves, packs, vendors and bots, with a "LAYOUT TEST" banner. The editor stays
open, so you can fix what you saw and test again. In any running match (host or standalone), **Alt+F5** or the console
command `cire.Layout restart` re-reads `MapLayout.json` and restarts the match on it: routes, spots, packs, merchants,
waves, lives and heroes start over without a rebuild or a map reload. `cire.Layout` alone prints what the match runs.

Your work **autosaves** to `Saved/MapLayoutDraft.json` 0.75 s after every change. The editor restores it on the next
launch (a draft authored on the other map is left alone). Named layouts are kept in `Content/Data/MapLayouts/<name>.json`.

## What the game reads

| Marker | In a match |
| --- | --- |
| Monster Spawn | Every spawn that targets a realm's team spawns that realm's waves. Its breach (rift crack, light, its name) is drawn at the spawn. |
| Monster Path | **Every path marches.** A spawn owns the paths that start at it. A path that merges into another continues along it to the objective. A path that ends short of the objective is closed into it (the compile notes say so). |
| Split | A wave is shared evenly by the spawns; each spawn splits its share **evenly** across its paths or **by path weight** (inspector: SPLIT, and each path's WEIGHT, 0..100; 0 = no units). The split is deterministic (largest-deficit apportionment), so every stretch of a wave keeps the shares: 3 paths at 1/4, 1/4, 1/2 give 2 / 2 / 4 of 8 units, every time. Escort guards march their escortee's path. |
| Boss / Pack Leader Spawn | Wave bosses appear at the boss markers in turn and march the path that starts nearest to them (no marker: the route's `boss` spot, else the breach). |
| Challenge Pack | Every pack of the realm (no limit), with its radius, tier (1-4), pack type and composition: a jungle camp of 3-6 monsters of its race in formation (Docs/JunglePacks.md). |
| Recall Point | Recall / Teleport to Base takes a hero to the nearest Recall Point of his team (none: the base). |
| Objective | The goal zone the waves attack (a square of the objective's diameter; an unchanged radius keeps the route file's zone). One zone for both realms: Validate flags T1 and T2 objectives that differ. |
| Player Spawn | Hero spawn points per team, in order (one per hero slot), with the marker's facing; more heroes than markers share them with a spread. The first T1 spawn is also the base (shop radius, recall). |
| Respawn | A dead hero revives at the respawn marker nearest to where he fell (WoW graveyards). |
| Rift / Portal | The arena portal of each realm (a violet ring and ARENA PORTAL label); heroes come back from the arena through it. Only the first rift per team is used (Validate notes extras). |
| Play Bounds | The third-person camera stays inside the polygon (the boom shortens at the edge). A hero outside it is told OUT OF BOUNDS and pulled back inside after 3 s; a monster outside it (and off its path) is set back onto its path. |
| Shop / Vendor | `TownVendors.json` (per realm: T1 merchants in DAYLIGHT, T2 in DARKNIGHT, shared in both). |
| No-Spawn / Blocker | Editor guide only (Validate notes it). |

Paths, spawns and spots replicate to clients with the route (a tagged block at the end of `LaneLayout`; mirrored realms
are sent once). Validate refuses a layout whose block would pass the engine's replication array budget. Challenge packs
are not in that block: they replicate in `LanePacks`, compact int chunks of three ints per pack, so any number of packs
reaches the clients (Docs/JunglePacks.md).

## Leash (snap-back)

Every wave unit carries a leash to its own path (`CireLeash.h`, data in `Content/Data/MonsterLeash.json`,
`cire.Leash reload` re-reads it):

| Key | Default | Meaning |
| --- | --- | --- |
| `radius` normal / elite / boss | 1800 / 2400 / 3200 cm | How far from its path a unit may be pulled. Normal and veteran units use normal, elite and champion use elite, warlords, mythics and bosses use boss. |
| `pursuitMargin` | 300 cm | A unit only chases heroes within radius - margin of its path, so the one who reaches them stays inside. |
| `disengageDistance` | 700 cm | A chaser whose target leaves that zone walks back (evading) if it stands farther than this from its path, else it simply marches on. |
| `returnTo` | `anchor` | Where it walks back to: the path point it left to fight (WoW "home"), or `nearest`. |
| `returnSpeedMultiplier` | 1.6 | Faster than its march on the way back. |
| `immuneWhileReturning` / `untargetableWhileReturning` | true / false | The evade: no damage while returning (no damage, so no new threat); untargetable also clears heroes' target on it. |
| `regenPerSecond` / `healOnArrive` | 0.2 / false | It regenerates 20 % of its max health per second on the way back (MOBA style); no full reset on arrival. |
| `resumeRadius`, `reengageSeconds` | 200 cm, 2 s | Back within 2 m of its return point it resumes the march and ignores targets for 2 s. |
| `maxReturnSeconds`, `stuckReturnSeconds` | 15 s, 4 s | A return that takes longer, or stops making progress, is set down at its return point (the stuck rescue). |

The state machine is March -> Chase -> Return -> March. The nameplate reads "(evading)" and the target line "Evading:
returning to its path" on every peer (`LeashState` replicates); everything else is server-authoritative.

**Threat** (Eric's ruling: only death drops threat). The leash never removes threat. It limits **pursuit**: a unit only
selects threat holders standing inside its leash zone, and none while returning. Everyone keeps his place and value in
the table; a kiter who comes back into the zone is the target again with all the threat he built. A unit whose holders
all stand outside the zone may take a new target on its path (a hero within 7 m), exactly like a fresh unit.

**Kited is not stuck.** The leash measures the distance from the unit's *path*, and only a chasing unit can be leashed.
Units jammed at the market stalls, the breach crowd or the castle queue stand on their path and have no target, so they
never leash; the wave director's stuck rescue keeps handling them (nudge along the unit's own path; a stuck chaser
repaths and keeps its target). The rescue also catches a marcher that keeps moving without getting anywhere (circling a
stall, looping on a navmesh detour): 15 s (3 stuck periods) without progress along its own path earns the same nudge
(`CIRE_WAVES_STALL_NUDGE`). Spawn formations are placed on the navmesh, so a spawn in a narrow alley never forms its
column inside a wall. A unit stuck on its way back is set down at its return point.

Units the director did not put on a path (challenge packs, armored escorts, forced marchers, bonus creatures, test
fixtures) keep their old behaviour: packs return to camp, the rest chase without a leash.

## Files

`MapLayout.json` (schema 1, realm-local centimetres, degrees; `"map"` names the map it drives, `"weight"` and `"split"`
are optional):

```json
{ "schemaVersion": 1, "units": "centimeters", "frame": "realm-local", "map": "castletown", "name": "Castle Town v1",
  "markers": [
    { "id": "monsterSpawn_4", "type": "monsterSpawn", "name": "The Breach", "team": 1, "target": 1, "x": 43200, "y": 0, "yaw": 180, "split": "weighted", "mirror": true, "pair": "monsterSpawn_5" },
    { "id": "monsterPath_8", "type": "monsterPath", "name": "Main road", "team": 1, "target": 1, "points": [[41500,0],[40800,-550]], "weight": 2, "from": "monsterSpawn_4", "mirror": true, "pair": "monsterPath_9" },
    { "id": "challengePack_12", "type": "challengePack", "team": 1, "x": 6600, "y": -900, "radius": 380, "tier": 2, "pack": "drowned_deep", "comp": [2,1,3], "mirror": true, "pair": "challengePack_13" },
    { "id": "vendor_14", "type": "vendor", "name": "", "team": 1, "x": 5200, "y": -1000, "yaw": 90, "kind": "weaponsmith",
      "sign": { "x": 5050, "y": -1000, "yaw": 90, "height": 250 }, "stall": { "x": 5200, "y": -860, "yaw": 90, "width": 220, "depth": 120 }, "mirror": true, "pair": "vendor_15" }
  ] }
```

`TownVendors.json` is the shared vendor-layout schema that `feat/vendors` reads (realm-local cm, degrees; sign `pos` is
`[x, y, height]`; `"team"` is the **realm** the merchant stands in: 0 DAYLIGHT, 1 DARKNIGHT, -1 both):

```json
{ "schemaVersion": 1, "units": "centimeters", "frame": "realm-local",
  "vendors": [ { "id": "vendor_14", "vendorId": "weaponsmith", "name": "", "team": 0, "pair": "vendor_15",
                 "npc": { "pos": [5200, -1000], "yaw": 90 },
                 "sign": { "pos": [5050, -1000, 250], "yaw": 90 },
                 "stall": { "pos": [5200, -860], "yaw": 90, "size": [220, 120] } } ] }
```

`Vendors.json` (from `feat/vendors`, optional here): `"vendors": [{ "id", "name", "sign": { "offset": [forward, right], "height" }, "stall": { "offset": [forward, right], "size": [width, depth] } }]`.

`BattlefieldRoutes.json` changes are described in `Docs/BattlefieldRoutes.md` (any number of challenge packs, the shared `"route"` form).

## Checks and captures

- `CireRouteEditor::RunTests` (part of `-CireCombatExpansionProbe`, logs `CIRE_ROUTE_TOOLS_PASS`) covers:
  - the pack generalisation: parse, rules, writer, replication, live visuals, spawning and pack ids
  - jungle-packs: 120 packs per team placed, numbered and renumbered, 3,000 packs through the chunked replication,
    119 packs per realm compiled and applied live, pack types and compositions synced to twins, legacy packs loading
    as Mixed, and `CireJunglePacks::RunTests` (tiers, compositions, pool, loadouts)
  - the realm frames
  - the layout model: typed markers, owners and targets, mirroring and sync, links, merges, renumbering, re-chaining, vendors, per-team validation, JSON and compile
- `Tests/ItemRulesTests.cpp` covers `RouteSchedule` with any number of packs (150 in the test).
- `Tools/RunJunglePackProbe.py` (`-CireJungleProbe`, town by default): 40 packs of mixed tiers and types in both realms, spawned and checked (`CIRE_JUNGLE_PROBE_PASS`).
- `CireLeash::RunTests` (logs `CIRE_LEASH_TESTS_PASS`): the leash state machine (kite past the radius -> return ->
  resume; a target leaving the zone; stuck is not kited), the rules file, and a world fixture (pursuit zone, threat kept,
  immunity, regen, return speed, re-engage delay, stuck-return rescue, packs and fixtures unleashed).
- `CireLayoutWiring::RunTests` (logs `CIRE_LAYOUT_WIRING_PASS`): the deterministic split (even and weighted), the
  two-spawn / three-path compile with a merge, replication of the extras, realm transforms of every path, Validate
  flagging runtime rejections, the vendor realm numbers, and director waves spawning 2 / 2 / 4 over three paths in both
  realms, bosses at the boss marker, heroes at their player spawns, respawns and rift.
- `Tools/RunLayoutProbe.py` (`-CireLayoutProbe`, on the town with `--town`): builds a layout with two monster spawns and
  three paths on the running map, applies it through the real pipeline, marches 12 director units per realm down every
  path until all arrive, then kites one unit per realm past its leash (realm 0 walked away by a kiting hero, realm 1
  pulled past the radius) and checks it returns to its path, keeps its threat, marches on and arrives. Logs
  `CIRE_LAYOUT_PROBE_PASS`.
- `Tools/RunLayoutGallery.py` runs the editor through a scripted session and saves seven captures to `Saved/LayoutGallery/<time>/`. It writes no data files.
