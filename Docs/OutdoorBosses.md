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
- **Health: 10,000 flat** (Eric, 2026-09-26) x the boss's multiplier (`OutdoorBosses.json` `bossHealth`, per boss id) x the
  marker's **HP x** (layout editor inspector: HP x - + under OUTDOOR BOSS, or `[` `]` with the Boss marker selected; steps
  of 0.25, 0.1..20, saved as the marker's `"hp"`, mirrored to the twin). The inspector shows the resulting HEALTH. Damage
  and skills are a wave boss's at wave `strengthWave` (12, or the live wave when later), with its race's complete kit, the
  warlord colours and the boss frame. It is never a lane boss: it never marches and never costs lives. Its nameplate reads
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

## Death and SUDDEN DEATH

A slain world boss **stays dead** (`respawnSeconds`: 0, Eric's ruling; a positive value would bring it back after that long).
A new wave cycle never revives it.

**SUDDEN DEATH** begins when the match passes `suddenDeathMinutes` (60; 0 = never), once per match:

- every dead world boss returns at its marker, at full health;
- with `suddenDeathHostile` (true) the returned bosses, and the ones still alive, are **hostile**: never neutral again,
  they attack any champion (players and bots) who comes within `hostileAggroRadius` (15 m, twice the 7 m of an ordinary
  unit) and anyone may fight them;
- they stay **leashed to their lair** (the boss radius, 32 m): they chase only inside it, evade home when kited out, and
  after every threat holder dies they walk home and reset, still hostile;
- it is telegraphed on every screen: the banner "SUDDEN DEATH - the world bosses return" and the announcement
  "SUDDEN DEATH | The world bosses return and hunt every champion near their lairs"; their target line reads "SUDDEN
  DEATH: hunting champions near its lair".

The match clock is the time the match has run (counted by the boss runtime from the first survival tick; Alt+F5
`cire.Layout restart` resets it and brings every boss back neutral).

## Wave bosses

Confirmed by Eric: wave bosses stay with their waves; the world bosses are additional. `waveBossesAtMarkers` (default
**false**): a wave boss comes through its wave's own monster spawn (the realm's paths in turn) instead of spawning at a Boss marker. `true` restores the old rule
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
  threat kept, waits at home), health 10,000 x boss x marker (30,000 for x1.5 x2), stays dead, sudden death at the
  configured time bringing every boss back hostile (replicated flag for the banner, resets hostile, aggroes a champion
  within the hostile radius), once per match.
- `CireLayoutWiring::RunTests` (`CIRE_LAYOUT_WIRING_PASS`): the marker's boss mirrors, round-trips and compiles in both
  realms; Validate flags unknown and duplicate bosses and markers outside the realm; the realm grows to the Play Bounds
  (Eric's polygon -> x -30000..35000, y +-27500), never shrinks, never for the procedural town; wave bosses spawn at their
  wave's spawn with outdoor bosses on and at the Boss marker with `waveBossesAtMarkers`.
- `Tools/RunOutdoorBossProbe.py` (`-CireOutdoorBossProbe -CireUseMapLayout -CireTown`, `CIRE_OUTDOOR_BOSS_PROBE_PASS`):
  Eric's MapLayout.json on the town, trimmed like a match, both realms: every Boss and Challenge Pack marker on the navmesh
  (the editor's own test), VALIDATE with navmesh checks raising nothing about them, each lair's leash area walkable inside
  the Play Bounds and reachable from the player spawn, one different world boss per marker on the navmesh, a kill paying
  the boss bounty and the boss returning after its respawn time.
