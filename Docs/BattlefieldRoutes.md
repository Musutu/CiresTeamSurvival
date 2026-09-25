# Battlefield routes and armored escort rounds

`Content/Data/BattlefieldRoutes.json` is the authoritative route authoring file. Distances are Unreal centimeters. Each team has its own ordered list of local XY points; local Y is relative to that team's realm center (Ember -2100, Dusk +2100). The first point is the wave spawn and the last point must reach the defended castle gate at local (-1850, 0).

## The town route (September 24 redesign)

Each realm is now a walled medieval town (see `Docs/EnvironmentProps.md`). The realm spans X -2350..13000 with a half-width of **1400** (widened from 1120 so streets can be lined with houses; the two realms still cannot touch: the Sundering Cliff between them is 200 cm thick and town pieces stop 60 cm short of it). Twelve points, identical for both teams:

| # | Local point | Where |
| --- | --- | --- |
| 0 | (12500, 0) | The Breach: monster spawn at a burning rift in the fields outside the town wall |
| 1 | (10800, 0) | Through the **town gatehouse** at X 11400 (8 m passage, first chokepoint) |
| 2 | (10100, -550) | Gate road bend past the watchtower |
| 3 | (8700, -550) | Market entrance |
| 4 | (7400, 500) | Diagonal across the open **market square** (open fight, covered market hall) |
| 5 | (5800, 500) | Between the two stall rows |
| 6 | (4700, -550) | S-bend into **Cooper's Lanes** (residential; houses on both sides make a 7-14 m lane) |
| 7 | (3300, -550) | Lane exit |
| 8 | (2400, 500) | **Town square** (open fight around the shrine, chapel, well and statue) |
| 9 | (1000, 500) | Square exit |
| 10 | (0, 0) | Banner-lined **castle approach** |
| 11 | (-1850, 0) | Through the **castle gatehouse** at X -1050 into the bailey: the leak zone |

No waypoint sits under a gatehouse (the environment gallery traces ground at every waypoint), and the old intermediate point at (-1100, 0) was removed for that reason. Challenge bays are still computed by `ChallengePosition`: with this route they land in the town square (tier 1), at the market/lanes corner (tier 2) and in a yard off the gate road (tier 3). Town pieces keep 450 cm of box clearance around each bay so the 1.7x Pack Leader has room.

The leak zone (`ACireTownGoal`, 900 x 1800 cm centred on the route end) is unchanged in size and position; the castle gatehouse was placed so its passage (X -1550..-550) straddles the zone's front edge at X -1400. A monster therefore loses the team a life when it is two thirds of the way through the castle gate. Heroes still spawn and recover at local (-1700, 0), which is now inside the castle bailey behind the gate. Monster marching goes through `CireNPCCombat::RouteDestination` / `ReachedGoal`, which already resolve to `CireLanePath::NextWaypoint` and the town-goal actor, so no NPC code changed.

### Route query API (for wave, boss and HUD code)

`CireLanePath.h` now also exposes world-space helpers that always use that world's replicated route:

- `RoutePoints(World, Team, Z)` - ordered waypoints, breach to castle.
- `RouteLength(World, Team)`, `PointAlongRoute(World, Team, Fraction, Z)` (0 = breach, 1 = castle gate).
- `RouteProgress(World, Team, Location)` - 0..1 progress of the nearest point on the route (e.g. for "wave at the market" callouts or threat scaling).
- `GoalPosition(World, Team, Z)` - centre of the castle-gate leak zone.
- `CireEnvironmentProps::DistrictAt(World, Team, Location)` / `Districts()` / `DistrictName(Id)` - which town district (breach, gate, market, residential, square, approach, castle) a location is in, for minimap labels or announcements.

**Path editor and navigation (nav-paths):** routes can now be edited live in F8 > Developer > Paths (drag waypoints, bays and the goal zone on the ground, validated against the navmesh). The document gained three optional keys, `laneWidth`, `goal` and per-lane `bays`; monsters and bots follow navmesh paths between waypoints and the waypoint arrival radius is 150 cm. See `Docs/Navigation.md`.

Edit the JSON and use `cire.Routes reload` in the authoritative development console to apply waypoints and escort tuning. The development settings panel can call `CireLanePath::Reload(World, &Error)` and display its result. Clients cannot reload the server's path. Successful changes publish both routes and the revision through GameState; client world geometry and the minimap use that world's received cache. Each world owns separate runtime route state. Bounds changes require restarting the match because they change permanent world geometry.

Reload is transactional: malformed or out-of-bounds documents retain the active routes. Routes require exactly two unique teams, 3–64 finite points each, at least 50 cm between adjacent points, realm clearance, a distant starting point, and a final point within 100 cm of the actual town center. Unknown keys and oversized documents are rejected. The two private realms cannot overlap.

A monster projects onto the nearest route segment when it spawns. It then advances waypoint by waypoint, including after displacement. A successful live path edit reprojects its progress from its current position; it does not teleport the monster. Collision still applies along the route, and destructible summoned walls are attacked before movement continues.

`ChallengePosition(World, Team, Tier)` gives world geometry, pack spawning, and the minimap the same optional challenge bay. The three tiers are anchored at 75%, 50%, and 25% of path length from the spawn, then placed on the side with more clearance from nearby route segments. Bays retain 220 cm of realm-edge clearance; the default authored route gives every bay at least 500 cm of separation from the marching path. Arbitrary custom routes that fill both sides may provide less separation.

`armoredEscort` controls dedicated escort rounds:

| Field | Prototype default | Meaning |
| --- | --- | --- |
| `everyWaves` | 4 | Every fourth wave replaces the regular payload with escorts; 0 disables these rounds. |
| `count` | 1 | Number of escorts per team in an escort round, 1–4. |
| `leakCost` | 1 | Town lives lost for each escort that reaches the goal, 1–100. |
| `healthMultiplier` | 6 | Multiplier applied once to the configured unit's health, 1–50. |
| `moveSpeed` | 170 | Marching speed in cm/s, 50–500. |

These values are editable prototype tuning. Escorts ignore player aggro, damage threat, and taunts, and pass through player/summon and friendly monster movement blockers. They follow the same route, take hostile damage normally, and breach blocking summoned walls using their existing fixed structure damage. Their health multiplier does not multiply damage. Existing phase pauses, own-town goal detection, destruction, and once-only life deduction still apply.

`CireLanePath::RunSmoke` emits `CIRE_LANE_SMOKE_PASS` or `CIRE_LANE_SMOKE_FAIL` plus individual failure reasons. It checks schema rejection, private bounds, per-world cache isolation, replicated state publication, escort scheduling, fixed waypoint progression and reload reprojection, ordinary NPC aggro, escort health/damage rules, player avoidance, wall breaching, phase pause, and once-only town leaks. Runtime smoke must run in the coordinated native test process; no external service or paid asset generation is needed.

The standalone `-CireEnvironmentGallery` development flag (runner `Tools/RunEnvironmentGallery.py`) captures seven native rendered views under `Saved/EnvironmentGallery`: town gate, market, residential lanes, town square, castle gate, full-realm overview and a real armored escort marching. It waits for shader compilation before the first capture. It checks the cobblestone road material slot, current route revision, town instance/light counts, that the castle gate, keep, town gatehouse, shrine and market hall exist once per realm, district lookup of the goal and spawn, the route-progress API, that each leak zone contains its route end but not the approach, supporting floors, and 45 cm radius / 90 cm half-height capsule clearance along every route segment in both realms (this is what proves the gate passages are walkable). It emits `CIRE_ENVIRONMENT_GALLERY_PASS` only after all seven PNG files exist and all checks succeed. The final frame uses the current prototype creature body; it does not claim a finished escort model.
