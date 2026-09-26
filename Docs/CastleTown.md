# The Medieval Kingdom town (main map)

The Medieval Kingdom pack's own assembled town (`/Game/CastleTown/Levels/Persistant/PL_CastleTown`) is the survival
map. Its houses keep their interiors, and the castle, landscape, roads and props stay as the pack built them. Only the
gameplay spots (route, breach, packs, base, goal) come from data, and those are **provisional**: Eric authors them with
the map layout editor (feat/dev-route-tools).

Code: `Source/CiresTeamSurvival/CireTownMap.*` (realms, lighting, explore mode) and `CireLanePath.*` (realm frames,
spots). The pack is Fab-licensed and never committed. A checkout without `Content/CastleTown` falls back to the
procedural town automatically.

## Requirements: the pack install

The pack's World Partition landscape (`SL_Landscape`) and several of its Level Instances (the curtain walls, the front
gate, the full castle, some buildings) keep their actors as One File Per Actor packages. They live in
`Content/__ExternalActors__/CastleTown` and `Content/__ExternalObjects__/CastleTown`, which "Add to Project" did not
copy into the main checkout.

To install them, run `Tools/LinkFabContent.py`, or `Tools/InstallFabExternals.py` on its own. It:
- finds the pack in the Epic launcher VaultCache
  (`C:/ProgramData/Epic/EpicGamesLauncher/VaultCache/<entry>/data/Content/`);
- copies the missing external files into the main checkout, never overwriting;
- junctions the folders into every worktree.

Both paths are gitignored. `--check` reports without writing.

Without them:
- The town has no terrain and floats over the void.
- Loading `SL_Landscape` asserts in `-game`. `CireTownMap` skips it and logs `CIRE_TOWN_NO_LANDSCAPE`.

## Two realms

Each team's PvE realm is one full copy of the town. `CireTownMap::LoadRealms` streams the pack's sublevels twice
with `ULevelStreamingDynamic::LoadLevelInstance`, once per realm offset. This happens on the server and on every
client, with deterministic level names (`<SL_Name>_CireRealm<team>`). `SL_Lighting` is never streamed, because each
realm brings its own lighting.

Both copies are identical: the same buildings, layout, pathing and spots. The only difference is the lighting:

| Team | Realm | Look |
|---|---|---|
| 0 | **Daylight** | Midday sun and the pack's `MI_sky_Day` sky sphere. |
| 1 | **Darknight** | Dusk to nightfall. A low cool moon, the pack's `MI_sky_Night` sky sphere, a darker cool grade, and warm fill lights at the pack's torches, lanterns and chandeliers. |

How the realms stay apart:
- **Lighting channels.** Realm 0 uses channel 0, the default. Every primitive and local light of the realm-1 copy is
  moved to channel 1, and each realm's sun lights only its own channel. Heroes and monsters are re-channelled when
  they change realm (`CireTownMap::ApplyActorRealm`, called from `CireRealm::UpdateVisibility`). In the PvP arena,
  everything stays on channel 0.
- **Separate sky spheres.** Each realm sits inside its own sky sphere, which is smaller than half the realm
  separation, so neither camera ever sees the other realm.
- **Bounded post-process grades.** Each realm has its own `UPostProcessComponent` inside a box. Its priority is 1:
  above the global town grade, below the arena's.
- **Shared by both realms:** one sky light and the height fog.

## Data

### `Content/Data/CastleTown.json`: realm transforms (read by feat/dev-route-tools)

| Key | Meaning |
|---|---|
| `levels` | The sublevels streamed per realm. `SL_Lighting` is rejected. |
| `realms[team].offset` | `[x, y, z]`: the world translation of that copy of the pack. Yaw is fixed at 0, so both copies are axis-aligned and one realm-local layout applies to both. |
| `realms[team].lighting` | `sky`, `sunPitch`, `sunYaw`, `sunIntensity`, `sunColor`, `exposureBias`, `saturation`, `tint`, `torchIntensity`, `torchRadius`, `torchColor`. |
| `frameCenter` | `[x, y]`, in pack coordinates: the realm-local origin. |
| `zRange` | The ground-trace and navmesh height window, relative to the realm offset Z. |
| `defaultMap` | `true` makes the town the map for normal play. |
| `explore` | `start` and `landmarks`, used by explore mode. |

The frame transforms work like this:

```
realmOrigin(team) = realms[team].offset.xy + frameCenter      (z = realms[team].offset.z)
world.xy          = local.xy + realmOrigin(team)
local.xy          = world.xy - realmOrigin(team)
world.z           = ground(world.xy) + local height            (ground = downward trace against static geometry)
```

In C++: `CireLanePath::RealmOrigin`, `ToLocal`, `ToWorld`, `CireTownMap::Ground` and `CireTownMap::RealmAt`.

### `Content/Data/CastleTownRoutes.json`: gameplay spots (PROVISIONAL)

This file uses the same schema as `BattlefieldRoutes.json` (Docs/BattlefieldRoutes.md), plus a few keys:

| Key | Meaning |
|---|---|
| `frame: "castletown"` | Enables the town rules. The route may run in any direction, and the realms are separated by distance, not by the cliff at Y = 0. |
| `base` | The hero spawn and recall point. |
| `respawn` | Optional. Where dead heroes revive. Defaults to `base`. |
| `boss` | Optional. Where wave bosses spawn. Defaults to the breach, which is the first route point. |
| `bounds` | The realm's play bounds, realm-local: `minX..maxX`, `|y| <= halfWidth`. |
| `goal` | The castle objective. |
| `bays` | The challenge packs, per team. |

Every spot is stored once, in realm-local coordinates. Lanes are team-owned (team 0 and team 1). The route editor's
"linked" mode mirrors one team's layout to the other, and both lanes ship identical.

When the town is active, `CireLanePath::DataPath()` points at this file, so the F8 path editor saves here.

**One source of truth (layout-wiring).** This file is the *provisional default*. The map layout editor's
`Content/Data/MapLayout.json` (written by Apply, `"map": "castletown"`) is compiled over it at every match start and
wins: its Player Spawn, Respawn and Boss markers replace `base`, `respawn` and `boss`, its paths, spawns and packs replace
`lanes`, its objective replaces `goal`. What a layout does not author (realm bounds, lane width, escort tuning) still comes
from here. Apply never rewrites this file. See Docs/MapLayout.md "What the game reads".

### `Content/Data/TownVendors.json`: vendor spots (PROVISIONAL)

This file holds realm-local spots for the three stat shops: Arcane (INT), Armory (STR and tanks), and Weaponsmith
(AGI and melee). The feat/vendors branch consumes it.

## Which map runs

`CireTownMap::WantTown` picks the map:

| Condition | Map |
|---|---|
| `-CireProcedural` | Always the old procedural town. |
| The pack is missing | The procedural town, with a warning. |
| `-CireTown` or `-CireExplore` | The pack town. |
| Developer probes, galleries, previews, labs, soaks and captures | The procedural town. Their fixtures were authored in that frame. |
| Otherwise | `defaultMap`. |

Clients follow the server through the replicated `ACireWorld::bCastleTown`.

## Explore mode: `ExploreTown.cmd` (`-CireExplore`)

This mode loads the town with no waves, bots or packs, and auto-drafts your champion. You start at `explore.start`.

| Key | Action |
|---|---|
| F5 | Toggle fly / walk. While flying: Space up, X down, Shift fast. |
| F6 | Mark a route point. |
| F7 | Mark a challenge-pack spot. |
| F4 | Mark a vendor spot. |
| Backspace | Undo the last mark. |
| F2 / Shift+F2 | Next / previous landmark: start, base, goal, breach, packs, then the authored landmarks. |
| Home | Jump to the same spot in the other realm. |

Marks are written to `Saved/Explore/TownMarks.json` in realm-local centimetres. The overlay shows your realm-local
position and draws the provisional route, breach, goal and pack spots in the world.

`-CireExplore -CireExploreCapture` is an automated tour, with results in `Saved/TownGallery/<stamp>/`:
1. It shoots the fixed views: realm overview, top-down route, both realms, castle goal, breach, base street, the
   realm-1 goal, and the landmarks.
2. It then marches a six-unit column along the route and follows it to the castle goal.
3. It prints `CIRE_EXPLORE_CAPTURE_PASS` when every marcher leaks into the castle.

The navmesh march test also runs on the town: `-CireNavProbe -CireTown`.

## Status and findings (2026-09-26)

Measured on this machine, in a windowed `-game` run:

| Stage | Time |
|---|---|
| Streaming both realms | 80-145 s, 20 levels plus 300 nested Level Instances |
| First navmesh build | 50-85 s, 4,626 tiles per agent |
| Total time to playable | about 3.5 minutes |

The first run is slower still while shaders and distance fields build.

**Frame time.** The town nav probe's performance window (10 heroes, 40 monsters, both realms loaded) averaged
146 ms per frame. Performance is an open item: candidates are LODs, Nanite, the second realm's Niagara, and
not streaming the realm that is not being viewed.

**Waves reach the castle.** `-CireTown -CireNavProbe` marched 10 marchers per realm down the provisional route, and all
20 left the field at the castle goal. That run used 16 stuck nudges, 6 rescue marches and 4 rescue despawns, so the
probe's strict no-stall checks still fail.

**Where the provisional route stalls.** These spots are realm-local and also tell Eric where not to route:

| Spot | Pack coordinates | Nudges | Cause |
|---|---|---|---|
| (-3500, 6000) | about (-9500, 8600) | Most: 8-11 per run | North market. Market stalls, carts and an anvil crowd the lane. |
| (-1500, 8000) | | Some | The next street along; stalls by the anvil. |
| (-16500, -1000) | | Some | The breach itself: the spawn crowd on uneven ground. |
| (14500, -4000) | | Some | Inside the castle goal zone: arrivals queue at the keep. |

A march without the wave director's rescue (the explore capture column) left 3 units stuck for good at the north
market.

**Night lighting.** The Darknight sky, with its stars and the pack's night HDRI, reads well. Facades are still dark
silhouettes. The sky light is shared by both realms, so the night side relies on its moon, the grade and the torch
fills. Tune it in `CastleTown.json`.

**House interiors.** The houses are enterable Level Instances, but their interiors are dark. `SL_Lighting`, which
carried the pack's interior light, is not streamed.

## Performance and load (town-perf, 2026-09-26)

Measured with `Tools/RunTownPerf.py` (`-CireTown -CireTownPerfProbe`): editor binaries in `-game`, like Play.cmd,
1600x900 windowed at the default preset. The fight has 10 bot champions and ~55 monsters across both realms, and
each realm is viewed for 30 s.

| | Before | After |
|---|---|---|
| DAYLIGHT fps (avg / p95 frame) | 5.9-7.4 / 174-321 ms | 57.0-57.3 / 21 ms |
| DARKNIGHT fps (avg / p95 frame) | 5.7-11.6 / 115-262 ms | 48.2-51.9 / 24-25 ms |
| Game thread | 86-174 ms | 16-19 ms |
| GPU | 32-44 ms | 8-10 ms |
| Load to playable (cached navmesh) | 423-463 s | ~119 s |
| Navmesh | 61 s build, then a 236-250 s in-game rebuild | 0.1 s attach (first start: ~65 s build, then cached) |

What changed, and where it lives:
- **`TCireActorIterator`** (`CireActorIterator.h`) replaces `TActorIterator` in runtime code. In editor-binary `-game`,
  the engine iterator copies every actor of every loaded level (~113,000 in the town) each time it is created.
  The monster AI and the HUD created hundreds per frame.
- **Virtual shadow maps.** The pack's own optimizer script sets `r.Shadow.Virtual.Enable 0` while its levels load.
  The project's values are restored after streaming (`performance.restoreRendererCvars`).
- **Pack lights.** The pack's ~200 shadow-casting lights per realm keep their light but not their shadows
  (`performance.packLightShadows`).
- **Particles and cloth.** Pack particles and cloth simulate only within `performance.packFxRadius` of the camera.
- **Per-view realm culling** (`CireTownMap::UpdateLocalView`, cvar `cire.TownRenderBothRealms`):
  - Each viewer renders and animates only the realm its camera is in. The far realm's pack primitives, lights,
    particles, sun, sky sphere and fills are hidden and paused.
  - A dedicated server only stops the cosmetic ticking.
- **Sky light.** It is re-captured from the viewed realm's own sky sphere; the old real-time capture found no sky
  material.
- **Load order.** The landscape's World Partition cells now stream before the navmesh is built. Before, they arrived
  on the first frame and forced a full rebuild.
- **Navmesh cache** (`CireNavCache.*`): `Saved/NavCache/CastleTown-<key>.navcache`, never committed.
  - The key hashes CastleTown.json, the route and vendor files, DefaultEngine.ini, the pack's level files and
    `NavRulesVersion`.
  - Any change is a miss followed by a rebuild. `-CireNoNavCache` forces a build.
- **Collision cooking.** `p.BodySetupSkipDDCThreshold=0` (DefaultEngine.ini) takes collision bodies from the local DDC
  instead of re-cooking ~5,000 of them on every launch.
- **Champion-select preview.** It is now released after the draft even when the draft screen was only drawn during the
  load's first frame. The probe fails if it is still capturing.

Still open:
- **Load time.** ~88 s of the ~119 s is uncooked level streaming (physics state, package loading). Getting under 60 s
  needs a cooked build or deferring the far realm behind champion select.
- **Editor-build overhead.** The asset registry scan (~25-30 s) runs alongside the load on checkouts without an
  editor-written cache.
- **External actor errors.** The 312 "Failed to load Actor for External Actor Package" lines are 61 editor-only
  `GroupActor` packages in the castle Level Instances. They are harmless, and no pieces are missing.

Night look: per Eric (2026-09-26), both realms stay as they are for now. Darkening DARKNIGHT later is a data change
in `realms[1].lighting`: `sunIntensity`, `skyLightIntensity`/`skyLightColor`, `exposureBias` and `torch*`.

Interiors and doors: Eric (2026-09-26) deferred these, so they are off in data:
- `performance.openDoors` would clear the closed door leaves.
- `realms[].lighting.interiorIntensity` would place a warm fill inside every building doorway.

`-CireTownDoorProbe` lists doorway walkability. The last run found 9 of 260 walkable, and clearing the leaves alone
did not change that.

Awnings, cloths, banners and flags ignore the camera channel, so the boom no longer snaps in under them.
