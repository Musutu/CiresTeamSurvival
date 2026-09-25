# Navigation: runtime navmesh, pathing and the path editor

Status September 24 (branch `feat/nav-paths`). Monsters and bots now walk real navmesh paths in
both town realms and in every arena, and F8 has a live path editor. Code: `CireNav.h/.cpp` (navmesh,
queries, steering), `CireRouteEditor.h/.cpp` (editor model and validation), `CireRouteEditorHUD.cpp`
(editor UI), `CireNavTests.cpp` (checks, probe, gallery). Runner: `Tools/RunNavChecks.py`.

## The navmesh

The town, its props and the arenas are built at runtime from data, so the navmesh is too.

- **Config** (`Config/DefaultEngine.ini`): `RuntimeGeneration=Dynamic`, 1600 cm tiles, 20 cm cells,
  45 cm step height, and two agents:

  | Agent | Radius / height | Used by |
  | --- | --- | --- |
  | Hero | 48 / 200 cm | heroes (tanks are 46 cm at 1.15x), ordinary monsters, the Siegebreaker (51 cm, 6 cm tolerance) |
  | Large | 72 / 300 cm | the 1.7x Pack Leader (65 cm) and any oversized wave row |

  `CireNav::NavData(World, Radius)` picks the smallest mesh that fits, so a unit scaled by a wave row
  moves to the Large mesh automatically.
- **Bounds**: `CireNav::Initialize` (called by `ACireWorld::BeginPlay` after the town and its props
  exist, server and standalone only) spawns three runtime `NavMeshBoundsVolume`s: one per realm (from
  the castle ward's edge wall to past the breach, stopping short of the Sundering Cliff, Z -400..900)
  and one over the shared arena footprint. A runtime volume has no editor brush, so its bounds come
  from a box body setup. It then builds both navmeshes with a blocking `Build()`: **about 1.06 s for
  252 + 252 tiles** on this machine, once, at match start.
- **What carves it**: collision that blocks pawns. Town pieces (houses, walls, the shrine, stalls,
  lamps, crates) now have `CanEverAffectNavigation` on when their slot has collision; the arena's
  invisible blocker proxies carve the arena. Summoned walls do not carve on purpose: monsters walk
  into them and breach them, as before. Characters never carve.
- **Instanced props**: an instanced static mesh registers with navigation on its first instance and
  its octree element can keep that one instance's bounds, so later instances would not carve tiles
  elsewhere. `CireNav::RefreshActor` re-registers an actor's instanced collision after bulk adds; it
  runs after every arena build and after the town re-places pieces on a route edit.
- **Rebuilds** are dynamic: the arena built at the start of prep is generated in the background
  (**about 0.2 s** in the probe, well inside the 60 s prep), removed arenas clear their tiles, and a
  live route edit rebuilds around re-placed pieces. `UCireNavSubsystem` tracks rebuilds and bumps a
  navigation revision so cached paths refresh.
- **Dedicated servers** build it too (the navigation system exists on the server; clients have none,
  `bAllowClientSideNavigation=False`). Look for `CIRE_NAV_READY netmode=1` in a server log.
- Recast voxelises surfaces, not solids: a hollow building shell or a blocker taller than an agent
  can hold a sealed navmesh island inside it. Nothing can path into a sealed island, and the tests
  check exactly that (no path in from the road or the arena floor).

## Steering

`CireNav::Steer(Agent, Goal)` is the one movement primitive: it returns the 2D direction to feed
`AddMovementInput`. It keeps a per-agent path cache and:

- repaths when the goal moves (more than 1 m or 20% of the distance), every 2.5 s for moving goals
  and 10 s otherwise, when the unit leaves its corridor by more than 2.6 m, after navmesh rebuilds
  and route edits, and when the unit stops making progress (1.2 s under 30 cm);
- after two stalls in a row it sidesteps for 0.6 s (crowd or corner deadlock), then repaths;
- projects a goal that is off the navmesh (a hero standing on a prop) onto it, and follows partial
  paths to their reachable end, then holds instead of grinding into a wall;
- reports `GoalUnreachable` after 2.5 s of partial/failed paths;
- spends at most `cire.NavQueryBudget` (24) path queries per frame per world; the rest keep their
  previous path for a frame;
- turns on RVO crowd avoidance for the monsters and bots it steers (never for player pawns);
- falls back to the old straight line when no navmesh covers the agent (clients, test fixtures far
  above the town). `cire.Nav 0` restores straight-line steering everywhere for comparison.

**Monsters** (`CireNPCCombat`, in `// nav-paths:` blocks): lane marching to the next route waypoint,
chasing a victim, walking beside an escortee, packs walking home after a reset, and displaced
neutral packs returning to camp all use `Steer`. Kiting casters and hunters back away along open
navmesh. A victim the navmesh cannot reach for 2.5 s is dropped by wave units (they return to the
lane, aggro suppressed for 6 s) and makes a pack reset home. The waypoint arrival radius is now 150 cm.
The stuck nudge and the stall failsafe are unchanged and remain the last resort.

**Bots** (`CireHero::BotThink`, `CireWaveDirector::BotSteer`): every bot movement paths on the navmesh
in every phase: to the lane, to targets, falling back to healers, the castle approach hold, walking
home in prep/recovery and in the arena. A **ranged** bot without line of sight asks
`CireNav::FiringPosition`: points on three rings around the target (80/55/30% of its range) are
projected onto the navmesh, traced for sight from eye height, and the cheapest one to walk to wins
(cached 1.5 s). The old road-detour logic stays as the no-navmesh fallback.

## Route data added for the editor

`Content/Data/BattlefieldRoutes.json` accepts three optional keys (older files still load):

| Key | Default | Meaning |
| --- | --- | --- |
| `laneWidth` | 520 | Road width in cm (360..1000). The road mesh uses it and town pieces within half of it + 70 cm of the route are removed (was a fixed 330 cm margin). |
| `goal` | `{x:-1850, y:0, depth:900, width:1800}` | The castle leak zone. `ACireTownGoal` follows it. It must stay inside the castle ward (max x <= -900); the route's last point must lie inside it and no other point within 100 cm of it. |
| `lanes[].bays` | computed | Exactly three `[x, y]` challenge bays (tiers 1..3), realm-local; otherwise computed from path length as before. |

The extras replicate with the points (`ACireGameState::LaneLayout`). `CireLanePath::Validate` holds
every rule and is shared by the JSON loader and the editor; `ApplyLive` is server-authoritative and
rejects bounds changes (they need a restart).

## F8 > Developer > Paths

A ninth developer tab. It shows the live route revision, lane width and goal zone, the navmesh
status (tiles, initial build, rebuilds), path-query statistics and a per-segment reachability table
for the draft, with **OPEN PATH EDITOR**, world navmesh and minimap navmesh toggles, and
**APPLY DRAFT / SAVE JSON / LOAD JSON / DEFAULTS / REVERT**.

**OPEN PATH EDITOR** switches to a top-down editor camera over your realm (the gameplay HUD is
replaced by the editor toolbar and the minimap):

- **Move**: WASD or arrows pan, Q/E rotate, R/F tilt, mouse wheel zooms, Tab switches realm.
- **Handles** on the ground: waypoints (gold, numbered), the breach spawn (red), the goal point
  (teal), challenge bays (purple diamonds) and the goal zone (teal box). Click to select, drag to move.
  Dragging a bay turns the computed bays into overrides.
- **INSERT AFTER** (Ins) adds a point halfway to the next one, **DELETE** (Del) removes one (the
  breach and goal points stay, at least three remain). Steppers set lane width, goal depth and width.
- **REALMS LINKED** mirrors every edit to the other realm; **PER REALM** edits Ember and Dusk
  separately.
- **Live validation** (throttled, and again after each navmesh rebuild): each segment is coloured by
  reachability for both agent sizes (green direct, yellow detour, orange partial, red no path), the
  navmesh path a unit really walks is drawn in white, and segments that would remove town pieces
  under the route-clearance rule are labelled with the count. Bays show a reachability ring and their
  own clearance count. The toolbar summarises rules, unreachable segments and pieces in the lane.
- **NAVMESH ON/OFF** toggles the engine navmesh debug draw (the `Navigation` show flag) and the
  minimap coverage overlay.
- **APPLY LIVE** applies on the server: the road and goal zone update, town pieces re-check
  clearance, the navmesh rebuilds around them, every unit reprojects onto the new route without
  teleporting and re-paths. **SAVE JSON / LOAD JSON / DEFAULTS** (the authored town route) / **REVERT**
  (the live route) / **EXIT** (Esc).

Validation of a draft runs against the current navmesh. A segment that is blocked only by pieces the
clearance rule would remove shows red before apply and green after the rebuild; the prop count says
why. Like the rest of F8 it only exists in development standalone sessions.

## Verification

- `python Tools/RunNavChecks.py` (native + probe + gallery; `--only native|probe|gallery`).
- Native (`-CireNavTests`, also inside `-CireCombatExpansionProbe`): `CIRE_NAV_PASS checks=55`.
  Both navmeshes exist; every route sample (100 cm) of both realms is on the Hero and Large meshes
  (160/160); full paths breach to castle for both agent sizes (about 144 m of path against the 159 m waypoint route,
  since paths cut the corners); hero spawn to mid-route; every challenge bay reachable; 264/264 large
  colliding town pieces block (the shrine in both realms); a path across the town square goes around
  the shrine; no-navmesh fallback keeps the straight line; all six arenas: 10/10 spawns on the
  navmesh, 20/20 paths to the centre and to the opposing spawn, every blocker carved, the navmesh
  gone after the arena is cleared; editor: the authored route validates, linked edits stay mirrored,
  an out-of-realm edit is rejected, a valid edit applies live and is published, pieces re-check
  clearance, units reproject without teleporting, the edited route validates after the rebuild,
  save/load round-trips and `BattlefieldRoutes.json` equals the editor defaults.
- Probe (`-CireNavProbe`): 20 marchers (every archetype, including the Pack Leader and Siegebreaker)
  walk breach to castle in both realms with the heroes stood aside: 20/20 leak, 74-105 s, **0 stuck
  nudges, 0 failsafe actions**, longest standstill 0.2 s. Then 10 bot heroes against 40 monsters for
  60 simulated seconds: **0.013 ms steering per frame**, 22.5 path queries/s at 0.011 ms average
  (0.093 ms peak). The arena navmesh appears 0.2 s after an arena is built during play.
- Gallery (`-CireNavGallery -RenderOffscreen`): `Saved/NavGallery/<stamp>/` 01 navmesh over the
  market, 02 the path editor mid-drag, 03 editor overview, 04 Star Station Hangar navmesh.

## Honest limits

- Sealed navmesh islands exist inside hollow buildings and tall blockers. They are unreachable and
  harmless for pathing; a goal right against such a wall could in theory project onto the inside.
- The town's backdrop pieces far outside the realm bounds are not in the navmesh (nothing walks there).
- Summoned walls deliberately do not carve: monsters still walk into them and breach.
- RVO avoidance keeps crowds from locking but can make a column of monsters fan out on the road.
- The bot-only soak still hits the 210 s failsafe on the late boss waves: that is kill speed against
  the cycle-3/4 Siegebreaker while it is being tanked (monsters in melee, bots attacking), not pathing.
  See `Docs/Waves.md` for the before/after numbers.
- Validation before apply uses the current navmesh; pieces the clearance rule would remove still
  block it until the rebuild after apply.

## World scale (September 25)

* The realm navmeshes grew with the 460 m realm: 702 + 702 tiles (Hero + Large), built in 2.2-2.7 s at match start (was
  252 + 252 in 1.2 s).
* `DefaultMaxSearchNodes` / `DefaultMaxHierarchicalSearchNodes` = 16384 (`Config/DefaultEngine.ini`; the engine default is
  2048). Bots now chase waves from the castle to the far districts (495 m of road); with the default budget the A* search
  gave up and every bot walked to the same partial-path end and waited there.
* The timed march probe scales its limit with the route: probe marchers walk at their base speed (they are not wave units),
  so the limit is max(240 s, route length / 100 cm/s * 0.8), about 400 s on the new road.
