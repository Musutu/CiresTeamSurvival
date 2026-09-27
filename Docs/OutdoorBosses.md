# Outdoor world bosses

Eric, playtest 5 (2026-09-26): "the boss markers i placed should be OUTDOOR bosses that are always there, each marker is a
different boss, the waves can still have bosses, or rare special mobs with loot like already, just the outdoor monster
packs should be there with the bosses i planted."

The outdoor world is now **the jungle packs + the world bosses**. Wave bosses, Rare Spawns and Bonus Loot creatures are
unchanged.

Code: `Source/CiresTeamSurvival/CireOutdoorBosses.{h,cpp}` (rules, spawn, respawn, tests) and `CireOutdoorBossProbe.cpp`
(the town probe). Data: `Content/Data/OutdoorBosses.json` (built-in defaults of the same values).

## What a world boss is

- **One per Boss marker, in each realm.** Every `bossSpawn` marker of `MapLayout.json` holds one race boss, spawned at match
  start. A marker and its mirrored twin hold the same boss, so both realms face the same boss at the same spot.
- **Always there.** World bosses persist across wave cycles (the jungle packs refresh each cycle; the bosses do not).
- **Neutral until attacked** (the challenge-elite ruling): yellow nameplate, "Neutral: attack to provoke the world boss".
  Bots and wave monsters never engage it; a player's hit turns it hostile.
- **Boss-grade.** It is configured like a wave boss of at least wave `strengthWave` (12, or the live wave when later) with its
  race's complete kit at the top skill tier, the warlord colours and the boss frame, times `healthMultiplier` (1.5) and
  `damageMultiplier` (1.0). It is never a lane boss: it never marches and never costs lives. Its nameplate reads
  `<Race | World Boss>` with its tier (`lootTier`, T4).
- **Bounty:** Eric's boss value, 10x the current mob value (`CI::BountyKind::Boss`), times `goldMultiplier` (1.0), shared by
  the teammates who fought it (like the packs).
- **Loot:** personal loot from the **lane-boss table** (boss-grade, at the lane-boss tier of the round) **plus the
  challenge-pack completion table** at `lootTier` (the jungle packs' bonus loot). The loot window says "you helped slay a
  world boss".
- **Leash:** the existing leash (`MonsterLeash.json`), anchored at the marker instead of a path: the **boss radius** (32 m).
  It pursues threat holders within radius - pursuitMargin of its lair; kited past the radius it evades home (immune,
  regenerating, faster), keeping every bit of its threat (only death drops threat). Back home with its holders out of reach
  it waits in its lair, still hostile; once every holder is dead it resets like a pack (home, full health, neutral again).
- **Minimap:** a gold-framed square at each lair (yellow while neutral, purple once provoked).

## Which boss sits on which marker

In order of precedence:

1. The marker's own pick: layout editor, select a Boss marker, **OUTDOOR BOSS  < name >** in the inspector (K cycles,
   AUTO clears it). Saved as the marker's `"kind"` in `MapLayout.json`; the twin follows.
2. `OutdoorBosses.json` `byMarker`: marker name -> boss id.
3. `OutdoorBosses.json` `roster`, in marker order (the markers of a realm without a name or a byMarker entry).

Boss ids are the race bosses of `Races.json` (each race's warlord and colossus). The defaults for Eric's six markers, one
different race each:

| Marker | Boss | Race |
| --- | --- | --- |
| Boss 1 | `maw_of_the_deep` | The Drowned Deep (Eric's favourite) |
| Boss 2 | `elder_oakheart` | The Blightwood (Eric's favourite) |
| Boss 3 | `drakkari_ashwing` | The Drakkari Brood |
| Boss 4 | `stoneborn_colossus` | The Stoneborn |
| Boss 5 | `voidborn_devourer` | The Voidborn |
| Boss 6 | `feral_ursoth` | The Feral Kin |

Validate flags a marker holding an unknown boss (error) and two markers of a realm holding the same boss (note).

## Respawn

`respawnSeconds` (default **300**: a slain boss returns to its marker 5 minutes after it dies, neutral and at full
strength, with a "WORLD BOSS | ... has returned to its lair" announcement). `0` = it stays dead for the match. A new wave
cycle never revives a boss early; a boss that vanished without a kill comes back on the same timer. `cire.Layout restart`
(Alt+F5) brings every boss back.

## Wave bosses

`waveBossesAtMarkers` (default **false**): the Boss markers are the world bosses' lairs, so a wave boss comes through its
wave's own monster spawn (the realm's paths in turn) instead of spawning at a Boss marker. `true` restores the old rule
(at the Boss markers in turn, marching the nearest path). Rare Spawns and Bonus Loot creatures are untouched.

## The navmesh fix ("outside of play area" / "off the navmesh")

**Cause.** Validate's navmesh check was right: there was no navmesh at Boss 4 (3756, 23595) and Boss 5 (27561, 12073).
The navmesh volume of each realm is built from the route file's provisional realm bounds (`CastleTownRoutes.json`
`bounds`: x -23500..23500, y +-19500). Eric's Play Bounds polygon reaches x -26220..31936 and y -17410..24937, so both
bosses stood inside the Play Bounds (Eric could walk there: heroes move on collision, not on the navmesh) but outside the
realm. The runtime rules also reject spots outside the realm ("boss spawns must be inside the realm"), which is the
"outside of play area" error.

**Fix.**

- The realm now always covers the Play Bounds: when a layout has a Play Bounds polygon, the town realm grows to the
  polygon plus 20 m (the 15 m trim margin and 5 m clearance), in 25 m steps, never shrinking
  (`CireLanePath::GrowRealmToPlayBounds`, applied by the layout compile). Eric's layout: x -30000..35000, y +-27500.
- The navmesh follows: the realm's nav volumes are the grown realm clipped to the Play Bounds plus the trim margin, so the
  trim still cuts everything outside (no navmesh is added outside the polygon + margin). The nav cache key includes the
  realm bounds, so the first run rebuilds the navmesh once and caches it.
- Validate now names the real problem: a marker inside the Play Bounds but outside the realm reads "outside the realm the
  game builds (no navmesh, nothing spawns there): draw the Play Bounds around it", instead of only "off the navmesh".
  When the Play Bounds are drawn past the navmesh the editor session was started with, Validate adds a note: APPLY, then
  restart the editor to build it there.

## Tests

- `CireOutdoorBosses::RunTests` (`CIRE_OUTDOOR_BOSS_TESTS_PASS`, part of `Tools/RunExpansionChecks.py`): the file, strict
  parsing, marker resolution (own pick > byMarker > roster), the pack-id block (never a jungle pack or fixture id), three
  Boss spots per realm spawning one different boss each (the same in both realms), neutral, boss-classified, never a lane
  boss, the boss bounty, bots never provoke it while a player does, the lair leash (boss radius, evade home immune with
  threat kept, waits at home), respawn after the timer, "stays dead" with 0, no early revive on a new cycle.
- `CireLayoutWiring::RunTests` (`CIRE_LAYOUT_WIRING_PASS`): the marker's boss mirrors, round-trips and compiles in both
  realms; Validate flags unknown and duplicate bosses and markers outside the realm; the realm grows to the Play Bounds
  (Eric's polygon -> x -30000..35000, y +-27500), never shrinks, never for the procedural town; wave bosses spawn at their
  wave's spawn with outdoor bosses on and at the Boss marker with `waveBossesAtMarkers`.
- `Tools/RunOutdoorBossProbe.py` (`-CireOutdoorBossProbe -CireUseMapLayout -CireTown`, `CIRE_OUTDOOR_BOSS_PROBE_PASS`):
  Eric's MapLayout.json on the town, trimmed like a match, both realms: every Boss and Challenge Pack marker on the navmesh
  (the editor's own test), VALIDATE with navmesh checks raising nothing about them, each lair's leash area walkable inside
  the Play Bounds and reachable from the player spawn, one different world boss per marker on the navmesh, a kill paying
  the boss bounty and the boss returning after its respawn time.
