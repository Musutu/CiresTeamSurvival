# Randomised PvP arenas

Status September 24: six themed arenas, picked at random for every arena phase. They are playable prototype
spaces built from CC0 art and an original mesh kit; the Fab packs Eric owns can replace pieces later without
code changes (see "Fab packs" below).

## The arenas

All six share one footprint 600 m north of the town (`origin` in `Content/Data/Arenas.json`), so only one exists at
a time. Ember always spawns on the west (-X) half and Dusk on the east; every layout is mirror-symmetric across the
centre line, so neither team has a spawn, cover or sight-line advantage. The sun always shines across the arena
(from the side), never into one team's eyes.

| Arena | Theme | Line-of-sight blockers | Cover | Lighting and sky | Ambience | Music |
| --- | --- | --- | --- | --- | --- | --- |
| **The Sunlit Fields** | Harvest wheat field at golden hour | 5-high square-bale stacks, pyramids of round bales, the hay wagon, the well, a ruined wall | round bales, stooks, dry-stone walls | low warm sun (10 degrees), god rays, volumetric haze, plains-sunset HDRI, wheat swaying in the wind, windmill and barn beyond the wheat | wind through the wheat, skylarks | Crusade |
| **The Black Shore** | Icelandic black-sand beach | basalt column clusters, basalt ridges, mossy lava boulders | stepped basalt, lava rocks | overcast North Atlantic sky, sea mist, sea to the north, basalt cliff to the south | surf, gulls, cold wind | Oppressive Gloom |
| **Redrock Canyon** | Moab desert canyon | the arch legs (walk under the span), hoodoos, fallen sandstone blocks | red boulders | harsh high sun, clear sky, terraced canyon walls | dry desert and canyon wind | Five Armies |
| **Hornbeam Glade** | Forest clearing | two big trees per side, upturned root plates, mossy boulders | fallen trunks, stumps | dappled light (light function), woodland HDRI, dense tree ring | woodland, bird chorus | Death and Axes |
| **The Drowned Sanctum** | Sunken temple on the seabed | columns, broken columns, ruined walls, the arch piers, statues on plinths | fallen column drums, sea rocks | animated caustics, blue-green fog, light shafts, kelp and drifting motes, procedural underwater sky | underwater hum, deep-sea drone | Black Vortex |
| **Star Station Hangar** | Orbital hangar deck | containers, parked dropships, pylons | crates, deck barriers | space sky with stars and a blue planet, hangar lights, light strips | spacecraft hum, station drone | Killers |

Playable bounds are 52 x 40 m (half extents 2600 x 2000 cm; the old court was about 31 x 29 m). Spawns are 3.5 m
inside each short edge, five per team, 3 m apart. Ember and Dusk spawns are 45 m apart; the middle has a
central obstacle (bale pyramids, arch, root plates, the temple arch, containers) so the teams cannot see each
other from spawn in any arena.

The legacy stone court from the first build is kept as **The Sundered Court**, the fallback arena. It is never in
the normal rotation. It is used when `Arenas.json` is missing or malformed, or when no themed arena is usable.

## How it works (`CireArenas.h/.cpp`)

1. **Prep phase starts (phase 1):** the server calls `CireArenas::ServerPrepare`, which picks the next arena at
   random from the rotation, never the previous arena, weighted by `weight`. The pick goes into
   `ACireGameState::ArenaIndex` (already replicated). Every peer's `UCireArenaSubsystem` sees phase 1 plus the
   index and builds that arena **hidden** during the prep minute, so the build cost (about 140 ms for the
   Fields, under 110 ms for the rest) never lands at arena start. The prep banner names it ("The portal opens
   onto The Sunlit Fields").
2. **Arena phase (phase 2):** `ServerBegin` makes sure a pick exists (late joins, developer phase skips), the stage
   is shown and players are teleported to `ArenaPosition`, which reads the picked arena's authored spawns. The
   banner announces **"ARENA / The Sunlit Fields"** with the arena's subtitle, and the minimap shows the arena's name,
   ground colour and every blocker footprint.
3. **Recovery / survival / finish:** the stage actor is destroyed (it owns every component, so cleanup leaves nothing
   behind) and the town lighting comes back.

Only the index is replicated. The geometry is deterministic on every peer, like `ACireWorld`. A dedicated server
builds only the invisible collision (no meshes, no lights): 24-34 collision instances per arena.

While an arena is shown, the arena brings its own directional sun, sky light, height fog, sky dome and an unbound
post-process, and the town's sun, sky light, fog, sky dome and the whole town world actor are hidden (the town is
550 m away and would otherwise show through the dome). The legacy court keeps the town's lighting, as before.

Everything else is unchanged: elimination and timeout scoring, the 90-second cap, arena deaths, team buff and loot
rewards, realm privacy (teams see each other only in the arena phase), falling out of the arena eliminates you,
and recovery returns everyone to their town. Arena bounds for targeting, skill aim and the out-of-arena check now
come from the picked arena (`CireArenas::InBounds`).

### Collision and fairness

Blockers get an **invisible collision proxy** (a box, or a cylinder for `shape: round`) that matches the slot's
authored footprint exactly. It is solid WorldStatic, so it blocks movement, projectiles, cursor targeting and line of
sight. Visual meshes never collide. Collision, symmetry checks, the minimap and path tests therefore all describe the
same shapes, whatever art the slot resolves to. Four invisible walls and a floor slab close the playable bounds.

### Data (`Content/Data/Arenas.json`, written by `Tools/AuthorArenas.py`)

Edit `Tools/AuthorArenas.py`, not the JSON. It runs the same validation as the runtime before writing.

- `slots`: reusable art. `candidates` is a list of object paths in priority order. `a|b|c` places several meshes that
  share one pivot, for example a chest and its lid. Each slot also has a guaranteed engine-shape `fallback`, a
  `footprint` in cm, `fit` (`footprint` stretches into the box, `uniform` keeps proportions, `none` uses native size),
  `shape`, per-section `materials`, and the flags `shadow`, `cull`, `wpo` (wind), `spin` (windmill sails), `hidden`
  (collision only) and `essential`.
- `arenas[]`: `id`, `name`, `subtitle` (banner line), `theme`, `weight`, `halfExtents`, `spawns.ember/dusk`, `ground`,
  `groundSize`, `pieces`, `scatter`, `lighting`, `ambience`, `music` and `minimap` colours.
- `pieces` are compact rows: `[slot, x, y, z, yaw, scaleX, scaleY, scaleZ, blocker]` in arena-local cm.
- `scatter` fills a region with a seeded, deterministic random pattern (wheat, kelp, rocks, motes). It can skip the
  playable bounds (`outside`) or keep clearance from blockers and spawns.
- `lighting`: sun pitch/yaw/intensity/colour/source angle, light shafts, sky material, tint, brightness, yaw and
  horizon haze, sky light, height and volumetric fog, exposure, saturation, contrast, white balance, bloom, vignette,
  gain, shadow tint, an optional light function (caustics, dappled leaves) and point lights.

**Validation** (`CireArenas::Validate`, run on load and by the native checks): 5 spawns per team, mirrored; spawns
inside the bounds and at least 1.5 m from every blocker; every blocker has a mirror twin with the same shape, size
and height; known slots; lighting values in range; a walkable path for a 50 cm capsule from Ember spawn 0 to every
other spawn and to the centre; at least 90% of the floor reachable. An arena that fails validation, or whose
`essential` slots have no real asset, leaves the rotation and logs `CIRE_ARENA_ASSETS_MISSING` or
`CIRE_ARENA_DATA`. If nothing is left, the fallback court is used.

## Art pipeline

| Step | Tool | Output |
| --- | --- | --- |
| CC0 sources | `Tools/FetchArenaAssets.py` (Poly Haven API, MD5-checked, nothing executed) | `Saved/ArenaSources` (not committed), `Art/Arenas/SourceManifest.json` |
| Original mesh kit | `Tools/BuildArenaMeshes.py` (+ `Tools/ArenaMeshKit.py`) | `Art/Arenas/Meshes/*.obj`, 46 meshes |
| Import | `Tools/ImportArenaContent.py` (unattended editor commandlet; `--only textures,materials,meshes,props,trees`) | `/Game/Arenas/{Textures,Materials,Sky,Meshes,Props,Trees}`, `Art/Arenas/ImportReport.json` |
| Layouts | `Tools/AuthorArenas.py` | `Content/Data/Arenas.json` |
| Audio | `Tools/FetchAudioSources.py` (arena_* keys), `DecodeAudioSources.py`, `ProcessAudio.py arenas`, `BuildAudioContent.py` with `CIRE_AUDIO_FOLDERS=Arenas` | `Content/Audio/Arenas` (18 sounds) |
| Provenance | `Tools/WriteArenaProvenance.py` | `Art/Arenas/PROVENANCE.md` |
| Review | `Tools/RunArenaGallery.py` | `Saved/ArenaGallery/<stamp>/`: overview, gameplay camera (real HUD), vista and plan per arena, 1920x1080 |

Materials (all original, in `/Game/Arenas/Materials`): `M_ArenaWorld` (world triplanar with anti-tiling on grounds),
`M_ArenaBlend` (rock with moss or sand on upward faces, optional sandstone strata), `M_ArenaSurface` (metre UVs),
`M_ArenaWheat` (two-sided foliage, golden gradient, wind sway with gusts), `M_ArenaFoliage` (masked leaves),
`M_ArenaKelp`, `M_ArenaMotes`, `M_ArenaShaft`, `M_ArenaCaustics` (light function), `M_ArenaWater`, `M_ArenaHazard`,
`M_ArenaFlat`, plus the sky masters `M_ArenaSky` (HDRI with yaw, tint and horizon haze) and `M_ArenaSkyProcedural`
(underwater gradient, or stars and a planet).

Trees come from the Poly Haven **FBX** builds: the glTF builds' leaf cards are dropped by the Interchange glTF
importer, so those trees rendered bare.

## Fab packs

Eric's five named packs (Moab Desert and Iceland Megascans collections, European Hornbeam, Underwater World, Big Star
Station) are Unreal-format only on Fab, so they cannot be downloaded from the website. They are listed in
`Docs/FAB-ADD-TO-PROJECT.md` (section "Arenas") with the slots each would upgrade. After "Add to Project", put the mesh
paths in `FAB_OVERRIDES` in `Tools/AuthorArenas.py` and rerun it. The arenas use them first, fitted into the same
footprints, so collision and fairness do not change.

## Audio

Each arena names an ambience district in `Content/Data/AudioAmbience.json` (`arena_fields`, `arena_iceland`,
`arena_moab`, `arena_hornbeam`, `arena_underwater`, `arena_station`). `CireAmbience` plays it during the arena phase,
with `noNight` so the town's night crickets stay out. The one-shot cues `amb_skylark` and `amb_gull` were added.
`music` overrides the arena score through `CireMusic` with one of the existing CC BY tracks. All sources are CC0
Freesound recordings, licence-checked by the fetch script; see `Art/Arenas/PROVENANCE.md`.

## Verification

- Native (`Tools/RunExpansionChecks.py --only native`): `CIRE_ARENA_PASS checks=293`. It covers data validation,
  unique ids, mirrored spawns inside the bounds, blockers never crowding a spawn, walkable paths and reachable floor,
  at least six tall sight blockers and an own sky, ambience and valid music per themed arena, random selection with
  no immediate repeat over 600 seeded draws (every arena picked), fallback for unknown indices, and building every
  arena in the live world: one stage actor, collision proxies equal to blockers, every art slot resolved, all 10
  spawns on the floor with capsule clearance, tall blockers stopping sight traces, the bounds wall, and the town sun
  hidden then restored. Cleanup leaves exactly the baseline actor count. The real server flow is also checked: prep
  prebuilds hidden, the arena shows, recovery cleans up, and the next prep never repeats.
- Two-client network probe (`--only network`) PASS: the server builds collision only, both clients build the same
  arena, and PvP, projectile, wall, summon and movement checks run in it. Interface smoke PASS (arena targeting,
  damage and heal events, recovery privacy). `RunNetworkSmoke.py` PASS. `-CireSmoke` full cycle PASS.
- `Tools/RunArenaGallery.py` PASS: 24 captures at 1920x1080, about 17-19 ms per frame in the offscreen capture
  (60 fps cap). Treat that timing as indicative.

## Honest limits

- The art is prototype quality: CC0 scans plus a procedural kit. The hay, rocks and ruins read well at gameplay
  distance but have no bespoke sculpting. The Megascans and Fab packs would be a clear upgrade.
- Blockers use box or cylinder collision proxies. Irregular art (the hoodoos, the root plates) is a little smaller
  or larger than its proxy at the edges.
- (Superseded by nav-paths, `Docs/Navigation.md`: every arena gets navmesh when it is built; the collision proxies
  carve it and bots path around blockers and reposition for line of sight.)
- The HDRI sun positions are only roughly aligned with the directional light (the Fields sky is rotated to match).
- The build hitch of about 140 ms for the Fields (52k wheat and stubble instances) happens at the start of the prep
  minute, in town. It is not profiled on minimum-spec hardware.
- The arena music overrides reuse existing tracks; no new music was added.
