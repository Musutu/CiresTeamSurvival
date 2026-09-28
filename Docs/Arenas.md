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

## World scale: arenas twice as large (September 25)

Eric: "PvP arenas about 2x the size; the wheat field is my favourite, keep its farm doodads." Every arena's playable floor is
now **104 x 80 m** (half extents 5200 x 4000; was 52 x 40 m). `Tools/AuthorArenas.py` still authors each layout in the original
coordinates: `K = 2.0` scales every position (never a piece's size) and every scatter region, and `fill()` re-adds the original
in-bounds pieces at their 1x positions. The inner half therefore keeps exactly the cover it had and the outer ring carries the
same pattern spread out: **twice the blockers per arena** (Sunlit Fields 34 -> 68 bale stacks, pyramids, round bales, stooks,
walls, wagons and wells; the others 22-26 -> 44-52), all still mirror-symmetric (validated: 100% of the floor reachable, every
spawn clear, 20-40 tall sight blockers per arena). Spawns stay 3.5 m inside the short edges (97 m apart), 3 m apart.

Continuous rows (field fences, hangar bulkheads and deck barriers, canyon rim walls, the basalt cliff and lava shelves) are
authored at half the step so their spacing is unchanged along the doubled edges; attached pieces (windmill sails, stacked
crates) keep their offsets. Scatter counts are doubled (stubble 3.5x, the Fields' near wheat ring 2.2x) but kept on the
560 m ground plate: a 1.1 km plate rendered black under distance-field lighting, so open-air plates stay 560 m (the hangar
deck, 74 m, doubles to 148 m).

Fab (local only): `FAB_OVERRIDES` now puts the Medieval Kingdom scanned European beech on the Fields' oaks and young trees and
the Hornbeam Glade's trees, saplings, shrubs, stumps and logs, and its wild grass on the glade's grass tufts. `CireArenas`
skips candidates inside the Fab pack folders when `-CireNoFab` is set, so a machine with the packs can render the clean-clone
look. Captures: `Tools/RunArenaGallery.py` (the overview and plan cameras now rise with the half extents).

## Shadow portals (September 26)

Eric: "when it is time for arena, a shadow portal opens for all players that teleports them to the arena that was chosen, the
portal should match the arena so players have an instant knowledge of the arena they are going to."

**Flow (server-authoritative, `CireArenaPortal.h/.cpp`).**

1. The prep banner still names the arena ("The portal opens onto ..."). When the prep minute reaches `portal.leadSeconds`
   (12 s), the server opens one **shadow portal beside every human champion**: 3.3 m ahead and to one side of them (so it
   is in the default camera view), on open ground with headroom, inside the Play Bounds, facing them. Each client raises a
   **SHADOW PORTAL** banner (arena style) with the arena's name, and the HUD draws a merchant-style plate over the ring: the
   arena's name, `<Shadow Portal>`, and near it a prompt capsule "Walk in to enter · drawn through in 0:08".
2. **Walking in** (a server trigger overlap) takes that champion straight to its arena spawn slot (the same slot the arena
   phase would give it). It waits there for the rest of the minute: its client shows the arena, lighting, ambience and music
   early; the town Play Bounds leash and the realm lane clamp skip it; it can still shop (B). Bots never use the portals.
   **Everyone through (Eric, September 27):** when every human champion has stepped through, the prep minute is cut to
   `flow.countdownSeconds` (7 s, see "Arena flow" below): an **ALL THROUGH** banner names the arena and the match clock counts down, then the arena
   phase begins as usual. Bots do not count; they are pulled through.
3. **When the minute ends** the arena phase moves everyone else exactly as before, so nobody is ever left behind. The town
   portals fold shut, and a matching **arrival rift** opens behind each team's spawn line for 4 s.
4. **Recovery** sends everyone back as before; a matching **return rift** opens at each team's return point (the realm's
   Rift marker, else the gate) for 6 s.

**Look (per arena, data-driven).** `Content/Data/Arenas.json` (written by `Tools/AuthorArenas.py`, `PORTAL` and
`PORTAL_LOOKS`) has a top-level `portal` block (lead time, ring radius/height, placement offset, rift durations, materials,
optional Shadow_Magic Niagara layers, sound cues) and a `portal` look in every arena:

| Arena | View inside the ring | Tint | Motes |
| --- | --- | --- | --- |
| The Sunlit Fields | wheat, bales, windmill | gold | wheat chaff spiralling out |
| The Black Shore | basalt and black sand under overcast | ice blue | snow blown out of the ring |
| Redrock Canyon | red sandstone and the arch | red-orange | a stream of red grit |
| Hornbeam Glade | the hornbeam clearing | green | tumbling leaves |
| The Drowned Sanctum | columns, arch and light shafts under water | teal | rising bubbles |
| Star Station Hangar | the hangar deck, dropships, the blue planet | blue | twinkling stars |

- The **view** is a square capture of the arena from behind the Ember spawn line (`Tools/RunArenaGallery.py --portal-views`,
  rendered with `-CireNoFab` so no licensed art is baked into it; sources in `Art/Arenas/PortalViews`), imported as
  `/Game/Arenas/Portal/T_PortalView_<id>` by `Tools/BuildArenaPortalContent.py`.
- `M_ArenaPortal` (same script) draws the view with a slow inward swirl, a dark shadow band and a wobbling edge, thin tinted
  filaments and an inner lip in the arena's tint; it flashes when someone steps through. `M_ArenaPortalMote` is the additive
  speck material for the motes (instanced spheres animated per style on the client).
- The **Shadow_Magic** pack (local Fab, never committed) adds a swirling ring (`NS_Shadow_Magic_Area4`, stood upright), a
  shadow pool at the base (`Area2`), an opening burst (`Shield_Splash2`) and a swallow burst when someone steps in
  (`Shield_Splash1`), all previously unused. Without the pack the portal is complete from the committed material alone.
- **Sound**: `arena_portal_open` (Dark Magic Spell 8), the `arena_portal_loop` hum (Dark Loop 1) and `arena_portal_enter`
  (Dark Magic Spell 6) from the Magic Spell SFX pack, falling back to the shipped teleport sounds (`AudioCues.json`).

**Replication.** `ACireArenaPortal` is a replicated, always-relevant actor carrying only the arena index, kind (entry, arrival,
return), team, collapse flag and a swallow multicast; every client builds the visuals from the data. A dedicated server builds
none of them.

**Checks.**
- Native (`CIRE_ARENA_PORTAL_TESTS_PASS`, in `RunExpansionChecks --only native`): the portal data parses, every themed arena
  has its own look, view texture and motes style (six styles), both materials and all three cues exist, and a portal builds,
  opens and collapses for every arena.
- Interface smoke (`Tools/RunInterfaceSmoke.py`): the server opens a portal beside both remote champions
  (`CIRE_INTERFACE_SERVER_PORTAL_OPEN_PASS`); each client sees its own portal named and themed after the replicated pick
  (`CIRE_INTERFACE_CLIENT_PORTAL_OPEN_PASS`); Ember's champion walks into its portal and lands in the arena during prep
  (`..._PORTAL_ENTER_PASS`); at the arena phase all 10 champions stand in the chosen arena, both town portals fold and both
  arrival rifts open (`CIRE_INTERFACE_SERVER_PORTAL_PASS`); each client is in the arena with it shown
  (`CIRE_INTERFACE_CLIENT_PORTAL_PASS`).
- Review: `Tools/RunArenaGallery.py --portals` renders every arena's portal in town (gameplay framing with the HUD plate,
  and a near-straight close-up) into `Saved/PortalGallery/<stamp>`.

## Arena flow (September 27, Playtest 6)

Eric: "After a PvP wave (5/10/15/20) completes: a portal opens next to EACH player to the designated arena; both teams get a
30 s prep. Players may enter early; anyone not in at timer end is auto-teleported. Once all are loaded: 7 s countdown banner,
then the match starts." Rewards: +50 g per killing blow; winners 250 g split and a stacking +15% PvE damage team buff (not PvP);
losers a stacking -15% PvE damage debuff.

**Schedule.** A wave cycle that ends on a scheduled PvP wave leads to the prep + arena; any other cycle end rolls straight into
the next cycle (`CireArenaFlow::SkipArena`). The schedule is read through `CireArenaFlow::PvPAfterWaves`, which returns
`CireWaveDirector::Schedule(World).PvpAfterWaves` (Waves.json `match.pvpAfterWaves`, default 5/10/15/20, a game-type preset
may override it). Sudden Death waves (after `match.totalWaves`) never lead to an arena.

**Timeline (server, `CireArenaPortal.cpp`).**
1. The cycle clears on a PvP wave: prep (phase 1) starts and lasts `flow.prepSeconds + flow.countdownSeconds` (30 + 7 s).
   A shadow portal opens beside **every** human champion at once (`ArenaStage = 1`). The match plate, the PvP Prep banner and
   the portal prompt show only the 30 s prep part.
2. Walking in stages that champion at its arena spawn. When the last human is through, the bots follow and the prep is cut to
   the countdown.
3. When the 30 s run out, everyone still in town (humans and bots) is drawn through (`ServerPullAll`).
4. Either way, once everyone is in the arena, `ArenaStage = 2`: the ALL THROUGH banner plus a big centre countdown
   (`ACireHUD::DrawArenaCountdown`, 7..1) run, then the arena phase begins.

**Rewards (`CireArenaFlow`).**
- Killing blow on an enemy champion in the arena: `flow.killGold` (50 g) to the killer, win or lose. Pets, summons and
  constructs credit their owner.
- Winners: `flow.winGold` (250 g) split across the drafted team (50 g each at 5v5), plus 100 XP each (kept from before), and one
  more stack of **Arena Victor** (+`pveBuffPercent` = 15% damage to monsters per stack).
- Losers: one more stack of **Arena Vanquished** (-`pveDebuffPercent` = 15% per stack), no gold.
- Draw: nothing.
- The multiplier is `1 + 0.15 x wins - 0.15 x losses` (clamped 0.1..10), applied in `ACireMonster::TakeDamage` only, so
  champion-vs-champion damage is untouched. The old +3% power / +8% loot per win also affected PvP; it is off unless
  `flow.legacyPowerLoot` is true.
- Icons: both are derived effects (`CireEffects::Gather` reads the replicated stacks on the game state), so they appear in the
  existing buff row with a stack count and a rich tooltip ("Damage to monsters +15%", N stacks). Rows live in
  `BuffModifiers.json` and `BuffVisuals.json`.

All numbers are in `Content/Data/Arenas.json` `flow` (written by `Tools/AuthorArenas.py` `FLOW`).

Checks: `CireArenaFlow::RunTests` (inside the arena-portal native checks: data, schedule, split, stacking, icons, killing-blow
gold) and the interface probe (portals at prep start, 37 s prep, stage 1 -> 2).
