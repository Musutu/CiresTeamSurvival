# Battlefield routes and armored escort rounds

`Content/Data/BattlefieldRoutes.json` is the authoritative route authoring file. Distances are Unreal centimeters. Each team has its own ordered list of local XY points; local Y is relative to that team's realm center (Ember -2100, Dusk +2100). The first point is the wave spawn and the last point must reach the team's town at local (-1850, 0).

The default battlefield extends from X -2350 to 13000 with a half-width of 1120 per realm. Nine points form a winding path from the distant spawn to the town. Ordinary unengaged wave monsters follow these points, while their existing player combat and challenge-pack behavior remain available.

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

The standalone `-CireEnvironmentGallery` development flag captures four native rendered views under `Saved/EnvironmentGallery`: town, street, whole-lane overview, and a real armored escort. It also checks the basalt paving material, current route revision, supporting floors, and 45 cm radius / 90 cm half-height capsule clearance along each route segment in both realms. It emits `CIRE_ENVIRONMENT_GALLERY_PASS` only after all four PNG files exist and all checks succeed. The final frame uses the current prototype creature body; it does not claim a finished escort model.
