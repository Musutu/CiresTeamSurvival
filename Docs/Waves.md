# Waves: the wave director and composer

`Source/CiresTeamSurvival/CireWaves.h/.cpp` (runtime), `CireWaveData.cpp` (data),
`CireWaveEditor.cpp` (F8 panel), `CireWaveSoak.cpp` (headless soak), `CireWaveTests.cpp`
(native tests). Data: `Content/Data/Waves.json`.

## Match flow

A cycle is **N cleared waves (15 s Skill Shop breather between them) → prep (30 s) → arena (≤60 s) → recovery (10 s)**,
all set by the `pacing` block (see **Pacing** below). N is
`wavesPerCycle` (default **5**, Eric's example progression; it was 3). Waves are pulled
from `waves[]` in order and wrap if the list is shorter than N; entries after N stay in
the list but are not played (the editor greys them out). The server is authoritative;
`ACireGameState` replicates `WavesPerCycle`, `CycleWavesDone`, `Wave`, `NextWaveSeconds`
and the new `WaveLabel` / `NextWaveLabel` shown on the HUD match plate.

A wave advances when it is **clear**: every must-clear unit is dead, leaked or despawned
and nothing is still queued to spawn. A wave with `mustClear: false` lets the next wave's
timer start once it has fully spawned; the cycle still waits for all wave units before prep.
Challenge packs never count toward a clear.

## Waves.json

```jsonc
{
  "schemaVersion": 1,
  "breatherSeconds": 12,         // cleared wave -> next spawn: the Skill Shop window (0..120)
  "wavesPerCycle": 5,            //                                      (1..10)
  "cycles": 3,                   // 0 = loop forever with scaling; N = match ends after cycle N, most lives wins (0..50)
  "waveOrder": "campaign",       // "campaign": waves[] is played straight through the match (cycle 2 starts at
                                 // waves[wavesPerCycle]) and wraps; "cycle": every cycle replays waves[] from the start
  "cycleScaling": { "healthGrowth": 0.08, "damageGrowth": 0.10, "extraUnits": 0 },  // per completed cycle
  "failsafe": { "enabled": true, "maxWaveSeconds": 100, "action": "march", "graceSeconds": 20, "stuckSeconds": 5 },
  "pacing": { "spawnAlongRoute": 0.0, "marchSpeed": 1.4, "firstWaveDelay": 8, "earlyContinue": true,
              "prepSeconds": 25, "arenaSeconds": 60, "recoverySeconds": 8 },  // spawnAlongRoute 0 = at the rift (Eric)
  "waves": [
    { "label": "Breach Vanguard", "type": "normal", "spawnInterval": 0.6, "delayBefore": 0,
      "mustClear": true, "rewardMultiplier": 1,
      "units": [ { "archetype": "hollow_infantry", "count": 3, "health": 1.1, "damage": 1.25, "size": 1,
                   "elite": false, "nonAttacking": false, "escortee": false, "boss": false, "leakCost": 0 } ] }
  ]
}
```

Counts are **per lane** (both teams get the same wave). Limits (validated and clamped;
structural errors reject the whole file and keep the previous config): 1–20 waves,
1–8 rows per wave, count 1–20, at most 30 units per lane and 3 bosses per wave, health
×0.1–20, damage ×0.05–10, size ×0.5–3, spawn interval 0–5 s, delay 0–120 s, reward (XP only) ×0–10,
leak cost 0–100, stall limit 30–900 s, grace 5–300 s, stuck 1–30 s. Archetypes must exist
in `NPCArchetypes.json`.

Unit flags:

| Flag | Effect |
| --- | --- |
| `elite` | ×1.6 health, ×1.25 damage, Elite classification (gold nameplate marker) |
| `nonAttacking` | Marches the route, never attacks or aggroes, walks through heroes; must be killed (reuses the armored-escort behaviour) |
| `escortee` | The protected tank of an escort wave (implies `nonAttacking`); attackers in the same wave and lane walk beside it and turn on whoever damages it |
| `boss` | Lane boss: leaks for the archetype `leakCost` (10) |
| `leakCost` | Lives lost on reaching the castle (0 = default: 1, boss 10) |

Unit health is `(waveHealthBase + waveHealthPerWave × globalWave) × archetype factor ×
row health × (1 + healthGrowth × cycle)`; the per-wave base growth from `CombatTuning.json`
is unchanged.

### Wave types (templates)

`APPLY TEMPLATE` in the editor (or `CireWaveDirector::Template`) fills a wave:

| Type | Template |
| --- | --- |
| Normal | 3 infantry, 2 bruisers, 1 hunter, 1 caster |
| Armored | 4 armored shieldbearers (non-attacking, ×1.3 HP, size ×1.2, leak 2 each) |
| Armored Escort | 1 escorted shieldbearer tank (non-attacking, ×5 HP, size ×1.45, leak 5) + 4 defenders (2 infantry, bruiser, hunter) |
| Boss | 2 infantry, 2 bruisers, 1 caster (×1.25 HP) + 1 Hollow Siegebreaker |
| Caster / Melee / Ranged / Hybrid Pack | shieldbearer front + 4 casters / infantry+bruisers / hunters / one of each |
| Custom | 4 infantry, edit freely |

The old every-4th-wave escort from `BattlefieldRoutes.json` (`armoredEscort.everyWaves`)
is superseded: escorts are now an authored wave type. Its route data is still used.

### Default progression and difficulty curve

**Rules conformance (25 September 2026):** the default match is a 15-wave campaign played in order
(`waveOrder: "campaign"`), so every wave type appears in a default 3-cycle match and every cycle keeps
its armored march, its **Armored Escort** and its boss:

| Cycle | Wave 1 | Wave 2 | Wave 3 | Wave 4 | Wave 5 |
| --- | --- | --- | --- | --- | --- |
| 1 | Breach Vanguard (normal, hollow) | Breach Column (normal, Blightwood) | Iron Procession (armored, Ironhide) | Armored Escort (Drowned Deep) | Siege Host (boss, hollow) |
| 2 | Shield Wall (**melee pack**, Stoneborn) | Hex Circle (**caster pack**, Aetheri) | Iron Procession (Feral Kin) | Armored Escort (Drakkari) | Siege Host (Blightwood boss) |
| 3 | Arrow Storm (**ranged pack**, Voidborn) | Warband (**hybrid pack**, Fallen Order) | Iron Procession (Aetheri + Ironhide) | Armored Escort (Stoneborn + Feral Kin) | Siege Host (mythic Drowned Deep boss) |

The race changes **every wave** (`campaign.rotateEvery: "wave"`), so all ten races, the Aetheri
included, appear in a default match. Pack waves are shieldbearer-fronted and differ in composition:
melee = tank + 3 line + 3 bruisers; caster = tank + 5 casters; ranged = tank + 5 hunters; hybrid = one of
everything plus hounds. Cycle 2 and 3 rows carry less health per unit than cycle 1 (×0.6-0.85, late
escort ×3 / ×0.7, late boss ×0.2) because those cycles add champion ranks, mythic bosses, more monster
skills and threat that is never dropped; the bots-only soak stays on the old pace (below).

The earlier default (kept as `waveOrder: "cycle"` for custom files) replayed Eric's example cycle:
**1 Normal, 2 Normal, 3 Armored, 4 Armored Escort, 5 Boss**, as one
5-wave cycle. The previous 3-wave cycle would have split it across two cycles and put
the boss mid-cycle. With five waves per cycle, each cycle ends on its boss, and prep and
arena follow the hardest wave.

Tuning reasoning (feedback: "early waves are easy"):

- The old wave 1 was 5 units per lane at base damage. **Wave 1** is now 7 units (3 infantry,
  2 bruisers, hunter, caster) at ×1.1 health and ×1.25 damage. **Wave 2** is 8 units at ×1.15
  health and ×1.3 damage. Both include ranged and caster pressure, so a defence must split
  its focus.
- **Wave 3** (armored marchers) tests damage throughput, since they don't fight back.
  **Wave 4** tests target priority: the escortee leaks for 5, and its guards peel damage
  dealers off it. **Wave 5** mixes a boss (10 lives) with upgraded mobs.
- Cycle scaling is gentle (now +10% health, +10% damage, no extra units; see Pacing), because
  health already grows +80 per global wave number (five more waves per cycle). In the first
  soak at +30% the growth compounded to about ×1.8 health per cycle, and bots-only
  teams collapsed in cycle 3.
- Walking time: see **Pacing** below. Waves now spawn 30% down the road and march 25% faster,
  so the unopposed walk is about 35 s instead of 75 s.

Measured with the headless soak (bots only, 30 Hz fixed step). See
`Saved/WaveSoak/*.txt` for the per-wave clear times and lives.

## Pacing (September 25: "the gameplay seemed to take too long")

Target feel: a wave resolves in about 45-90 s, a full cycle (5 waves + prep + arena + recovery) takes
6-8 minutes, and the first real power spike lands within 2-3 minutes. All knobs are data (`pacing`,
`breatherSeconds`, `failsafe`, `cycleScaling` and the wave rows), editable live in F8 → Waves (second
globals row). The route shape itself stays in F8 → Paths.

| Knob | Before | Now | Why |
| --- | --- | --- | --- |
| `spawnAlongRoute` | 0 (breach gate) | **0 (at the rift)**; it was 0.30 for one pass, then reverted | The route is about 155 m, so an unopposed walk takes about 75 s at base speed. For one pass waves appeared 30% of the way down the road; Eric ruled that waves spawn at the rift, so pace is tuned with wave health and march speed instead |
| `marchSpeed` | 1 | 1.25 | Wave units (bosses included) walk 25% faster while they have no victim. Chase and combat speed are unchanged |
| spawn interval | 0.6 / 1.2 / 0.5 / 0.7 s | 0.4 / 0.8 / 0.5 / 0.5 s | The column arrives together instead of trickling in |
| `breatherSeconds` | 8 s | **15 s** | The Skill Shop opens after every cleared wave (progression-shop); 15 s is enough to buy, and Ready-up ends it early |
| prep / arena / recovery | 60 / 90 / 15 s | 30 / 60 / 10 s | Shopping now happens every breather, so prep only needs a final top-up. Arenas usually end in 25-45 s when a team is wiped |
| unit health | waves 1-2 ×1.1 / ×1.15, boss ×1, escortee ×5 | ×0.9 / ×0.95, boss ×0.4, escortee ×4 | Waves ran long because of kill time, not threat, so damage multipliers are unchanged (×1.25-1.3). Wave 2 keeps its grave hounds and drops to 1 hunter |
| `cycleScaling` | +15% HP, +1 unit per row | +10% HP, +0 units | Extra units per row grew clear time linearly (wave 2 reached 15 units by cycle 3). Race ranks, skills and per-wave health growth already raise difficulty |
| `failsafe.maxWaveSeconds` | 210 s (+45 s grace) | 120 s (+30 s) | No wave can hold a cycle more than about 150 s. Leftovers march and cost lives |

Measured with the bots-only headless soak (`-CireWaveSoak`, 30 Hz fixed step,
`Tools/SummarizeWaveSoak.py`; files in `Saved/WaveSoak/pacing-*`):

| Run | Waves ≤90 s | Mean / median wave | Longest | Cycle lengths | Mean hero level 2 / 3 / 4 | Failsafe march / despawn | Lives lost |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Before (main c48df07, 3 cycles) | 2 / 15 | 150 / 127 s | 264 s | 12.2, 15.7, 18.3 min | 78 / 173 / 209 s | 22 / 17 | 70 (3 cycles) |
| After, pass 1 (route + speed + phases) | 5 / 15 | 119 / 111 s | 188 s | 10.2, 13.6, 14.4 min | 56 / 120 / 152 s | 61 / 23 | 115 (3 cycles) |
| After, pass 2 (+ no extra units, boss ×0.6) | 11 / 15 | 90 / 78 s | 183 s | 8.8, 11.2, 11.2 min | 61 / 125 / 161 s | 12 / 8 | 76 (3 cycles) |
| **Final (4 cycles)** | **16 / 20** | **78 / 66 s** | **143 s** | **8.0, 8.8, 9.3, 9.7 min** | **51 / 114 / 140 s** | **7 / 0** | 99 (4 cycles) |

Reading it: normal, armored and escort waves now resolve in about 53-85 s. The cycle-ending boss wave is
still 84-143 s against **bots only**. Bots don't shop, and they deal the least damage. In cycles 2+ the
race boss is a warlord/mythic rank. A human team using the Skill Shop should fall inside the 6-8 minute
target. The bots-only cycle is an upper bound: 8.0 min for cycle 1, and 8.8-9.7 min later, mostly from
that one boss wave. If playtests still feel long, lower the boss row's `health` (0.4) or `wavesPerCycle`
in F8 → Waves before anything else. The first level-up now comes at about 50 s and level 3 by about 2
minutes (before: 78 s and 173 s). Wave 1 clears and opens the Skill Shop at about 1 minute.

### Balance pass (feat/balance, September 25: spawns at the rift, early waves harder)

Spawns stay at the rift (`spawnAlongRoute` 0). Pace comes from wave health/speed, the clock and the
failsafe; early waves hit harder instead of dying faster.

| Knob | Before | Now |
| --- | --- | --- |
| `cycles` | 0 (endless) | 3 (match ends after cycle 3, most lives wins) |
| `breatherSeconds` / prep / recovery | 15 / 30 / 10 s | 12 / 25 / 8 s |
| `marchSpeed` | 1.25 | 1.4 |
| `cycleScaling.healthGrowth` | 0.10 | 0.08 |
| wave 1 damage | ×1.25 | ×1.4 |
| wave 2 health / damage | ×0.95 / ×1.3 | ×0.85 / ×1.45 |
| boss row health | ×0.4 | ×0.3 |
| `failsafe.maxWaveSeconds` / grace | 120 / 30 s | 100 / 20 s |

`python Tools/RunPacingSoak.py --cycles 3 --label <name>` (bots only, `Saved/WaveSoak/pacing-*.txt`):

| Run | Cycle lengths | Mean / longest wave | Lives left (Ember / Dusk) | Match |
| --- | --- | --- | --- | --- |
| Before (1a5d2f3) | 9.3, 9.5, 10.9 (+12.4 for cycle 4) min | 94 / 153 s (first 3 cycles) | 75 / 75 after 3 cycles | 42.0 min, never ends by itself |
| Clock + march + growth (`after-round1`) | 9.0, 8.8, 10.8 min | 93 / 153 s | 74 / 73 | 28.6 min |
| + early-wave damage, wave 2 / boss health (`after-round2`) | 8.6, 9.0, 10.5 min | 91 / 152 s | 73 / 71 | 28.1 min |
| **+ failsafe 100 / 20 s (`after-round3`)** | **8.0, 8.7, 9.9 min** | **85 / 123 s** | **68 / 73** | **26.6 min** |

The longest waves are the wave-2 slot and the boss wave in cycles 2-3, held by one or two stragglers
until the failsafe marches them; the tighter failsafe turns that dead time into leaked lives.

**Rules conformance (`feat/rules-conformance`, `Tools/RunPacingSoak.py --cycles 3`, bots only):** no leash,
no stuck/unreachable threat drops, failsafe only on units without threat, the 15-wave campaign with
pack waves and a race per wave, champion ranks in cycle 3, and the cycle 2/3 health trim.

| Run | Cycle lengths | Longest wave | Lives (Ember / Dusk) | Match |
| --- | --- | --- | --- | --- |
| before (main, run 1) | 8.1, 8.4, 9.2 min | 123 s | 76 / 70 | 25.7 min |
| before (main, run 2) | 7.6, 8.0, 9.1 min | 122 s | 71 / 71 | 24.8 min |
| after, first pass (no health trim) | 8.0, 8.6, 12.1 min | 162 s | 70 / 68 | 28.7 min |
| **after, final (run 1)** | **7.8, 9.3, 9.2 min** | **128 s** | **73 / 71** | **26.4 min** |
| **after, final (run 2)** | **7.8, 8.0, 9.0 min** | **137 s** | **71 / 71** | **24.8 min** |

Mean 25.6 min against 25.25 min before (+1.4%; −3.8% against the 26.6 min reference). Without the leash
a unit chases a retreating bot across the map, so single waves vary more (up to about 2.5 min).

### Skill Shop breather and Ready-up

`breatherSeconds` is the window between a cleared wave and the next spawn. The progression-shop Skill
Shop reads it from `CireWaveDirector::Config(World).BreatherSeconds`. The live countdown is the
replicated `ACireGameState::NextWaveSeconds`, and `CireWaveDirector::IsBreather(Mode)` says whether the
window is open (server).

**Ready-up** (`pacing.earlyContinue`, default on): each human player can press **READY UP** under the
match plate, or send `ACireController::ServerAction(10, 1|0)`. The server calls
`CireWaveDirector::SetPlayerReady(Hero, bReady)`. Once every drafted human is ready, the next wave
starts in 1 s. Bots are always ready, and a bots-only match keeps the full breather, so soaks stay
comparable. `ACireGameState::BreatherReady` / `BreatherPlayers` replicate the count ("READY 1/2").
Presses reset when a wave starts or the phase changes.

### Economy hook (monster gold)

Eric's rule: 1 gold per mob at the start, +1 every 3 waves, armored ×2, bosses ×10. progression-shop
implements the gold; the wave director exposes the inputs. The wave's `rewardMultiplier` (escort ×1.25,
boss ×1.5) scales **XP only**; it no longer stacks on gold (`ACireGameMode::MonsterKilled`).

```cpp
#include "CireWaves.h"
int32 Wave = CireWaveDirector::CurrentWaveIndex(Mode);                 // global wave number, 1-based (0 before wave 1)
FCireWaveUnitInfo Info = CireWaveDirector::UnitFlags(Monster);         // spawn-time facts for this unit
// Info.bValid (false for challenge packs / non-wave actors), Info.WaveNumber (the wave it spawned in),
// Info.WaveInCycle, Info.Cycle, Info.Type (ECireWaveType), Info.bArmored (non-attacking marcher:
// armored wave or escortee), Info.bEscortee, Info.bBoss, Info.bElite (rank elite or higher, not boss)
```

Use `Info.WaveNumber` rather than `CurrentWaveIndex` when paying out a kill, because a unit can die
after the next wave has started (non-must-clear waves, test spawns). `UnitFlags` is valid inside
`ACireGameMode::MonsterKilled` / `CireLoot::OnMonsterKilled` and `Leak`. The director forgets the unit
after those return.

## Races, ranks and monster skills (monster-races)

Full bible: `Docs/Races.md`. Waves.json gains three things:

```jsonc
"skillProgression": { "firstSkillWave": 4, "unlockEveryWaves": 3, "maxSkills": 3, "tierEveryWaves": 5, "maxTier": 3,
                      "tierDamage": 0.2, "tierCooldown": 0.1, "tierDuration": 0.15 },
"campaign": { "raceRotation": ["hollow", "blightwood", "drowned_deep", "ironhide", "hollow+blightwood", "stoneborn", "drakkari",
                               "drowned_deep+voidborn", "feral_kin", "fallen_order", "voidborn", "ironhide+drakkari"],
              "reskinOnWrap": true, "veteranFromCycle": 2, "eliteFromCycle": 3, "championFromCycle": 4,
              "mythicBossFromCycle": 3, "promoteEvery": 4 },
"waves": [ { "label": "...", "race": "drowned_deep",            // optional; absent = the rotation's race for the cycle
             "units": [ { "archetype": "hollow_shieldbearer", "slot": "tank", "rank": "champion",
                          "palette": 1, "skills": 2, "skillTier": 3, "count": 1 } ] } ]
```

- **No monster skills in the first waves.** Global waves before `firstSkillWave` (default: waves 1-3 of cycle 1) use basic
  attacks only, bosses included. Then 1 skill, +1 every `unlockEveryWaves`, up to `maxSkills`; ranks add skills (elite +1,
  champion +1, warlord +2, mythic +3). Tier II/III every `tierEveryWaves` (+20% damage, -10% cooldown, +15% control per tier).
  Challenge packs are the exception: optional elite camps always fight with skills at the current count and tier.
- **Race per wave.** `campaign.rotateEvery` picks the rotation step: `"wave"` (default since rules-conformance: rotation
  index = cycle × wavesPerCycle + wave) or `"cycle"` (one entry per cycle). A row with a `slot` (line, bruiser, tank, caster, ranged, special, warlord, colossus, boss) takes that
  slot's unit from the wave's race; `archetype` keeps the hollow unit as the default. `boss` alternates the race's colossus
  (odd cycles) and warlord (even cycles). A wave's `race` overrides the rotation; `a+b` rotation entries mix races row by row.
- **Default campaign (rules-conformance).** One race per wave: hollow, Blightwood, Ironhide, Drowned Deep, hollow (boss);
  Stoneborn, Aetheri, Feral Kin, Drakkari, Blightwood (boss); Voidborn, Fallen Order, Aetheri + Ironhide, Stoneborn +
  Feral Kin, Drowned Deep (boss). Every cycle ends on a race boss; bosses are mythic from cycle 3. From cycle 2 every 4th
  normal attacker is promoted: veteran in cycle 2, then from cycle 3 elite and **champion alternating** (champions were
  unreachable before: `championFromCycle` 4 with 3 cycles). The second lap of the rotation reskins with palette 1.
- **Ranks.** `rank` normal/veteran/elite/champion/warlord/mythic sets colour, stats and extra skills (the legacy `elite`
  flag is rank elite). `palette` picks a race reskin set (-1 = the rotation lap). `skills` / `skillTier` override the schedule.

## Stall protection (never hangs a cycle)

Root cause of the playtest stall (wave 2 never cleared): wave monsters hold threat forever
(no decay) and chase their victim in a straight line (no navmesh). A unit chasing a hero
who had respawned at the gate climbed onto the **town shrine** (the `Town_shrine_0`
movement-base replication warnings in the playtest log) and pinned itself against
geometry at 0 velocity. Bots targeting it walked straight into the same prop and never
had line of sight to attack. Nothing could kill it or leak it, and the clear rule has no
timeout, so the cycle stopped forever. `-CireWaveNoRescue` reproduces it (wave 1 stuck for
40 minutes in `Saved/WaveSoak/repro-norescue-idle.*`).

Protection layers, per wave unit, every server tick:

Eric's threat ruling applies to every layer: **threat is lost only when a unit dies or an ability says
so**. There is no lane leash (the old 18 m leash is gone) and no layer drops a target.

1. **No leash**: a wave unit chases its threat holder at any distance. A victim the navmesh cannot
   reach is kept: the unit holds at the nearest reachable point of its partial path.
2. **Stuck detection**: sampled every second. A unit that moves under 60 cm for `stuckSeconds`
   while not attacking, casting, paused or escorting is **nudged** onto its route 4.5 m closer
   to the castle (routes are kept clear of props) **only when it holds no threat**; a stuck
   chaser keeps its target and repaths (`CIRE_WAVES_STUCK_REPATH`).
   A unit knocked outside its realm is returned to the route (threat kept).
3. **Stall failsafe**: `maxWaveSeconds` after a wave's last spawn, leftovers that hold **no threat**
   ghost through characters and **march** to the castle (a leak costs lives); after
   `graceSeconds` they despawn. `action: "despawn"` skips the march. A unit that holds threat
   keeps fighting, and a marcher that is attacked stops marching and fights.
4. **Bot steering**: bots that stop making progress detour along the road, or sidestep if
   they are already on it (`CireWaveDirector::BotSteer`).

Phases after prep, arena and recovery are clock-driven. Only survival waits on an objective,
and with the failsafe on a wave that nobody is fighting can never exceed roughly `maxWaveSeconds +
graceSeconds`. A wave whose units hold threat lasts until they or their threat holders die (bots-only soak:
longest wave about 2.5 min).

## Neutral challenge packs

Challenge-pack units (their Pack Leader included) spawn **neutral**: yellow nameplate and unit
frame, "Neutral: attack to provoke the pack", no proximity aggro, and no threat from healing.
Brushing past them does nothing. The first damage from a **player** (or a player's summon)
turns the whole pack hostile together. Bot damage on a neutral pack is rejected. When every
threat holder is dead the pack walks home, heals, and becomes neutral again.

## Bots (lane defence)

- Survival targets are wave units in the bot's lane, scored by distance plus route urgency
  (units closest to the castle first), with bonuses for bosses and marchers and for units
  already hitting a teammate.
- Packs: never neutral ones. A provoked pack is attacked only in self-defence, or when it is
  fighting a teammate and no wave unit threatens the route.
- With nothing to fight, bots hold a loose line across the road at 80% of the route (the
  castle approach) instead of idling where their last fight ended.
- A threatened bot under 28% health retreats to a living healer, or else toward the gate,
  and rejoins at 60% health or after 9 s. Dead bots respawn at the gate and walk back.

## F8 → Developer → Waves (live composer)

A wave list with ADD / DUP / DEL / MOVE UP / MOVE DOWN (each wave shows its race; `*` = from
the rotation). For each wave: type (click to cycle), TEMPLATE, **RACE** (click: rotation, then
every race), must-clear toggle, spawn interval, delay and reward steppers, and composition rows:
**UNIT** (click cycles the race slots Line/Bruiser/Tank/Caster/Ranged/Special/Warlord/Colossus/Boss,
then the race's units by name), the **R** rank cell in its rank colour (click cycles normal,
veteran, elite, champion, warlord, mythic), M/T/B flags for marcher, escortee and boss, count,
health, damage, size, **SK** skill tier (A = schedule, 1-3 forced tier, - = no skills), remove,
+ ROW (a Line slot row). Globals: breather, waves per cycle,
cycles, cycle health growth, stall limit and failsafe on/off.

- **APPLY LIVE** validates and applies on the server. Changes take effect from the next
  wave; units already on the field are never rewritten.
- **SAVE JSON** / **LOAD JSON** write and read `Content/Data/Waves.json`. **DEFAULTS**
  resets the draft.
- **SKIP TO THIS** makes the selected wave the next one. **SPAWN NOW** spawns the draft wave
  immediately as an extra test wave.

Like the rest of F8 it is available in development standalone sessions. The live header
shows the replicated current and next wave.

## Verification

- Native: `CireWaveDirector::RunTests`, run by `-CireCombatExpansionProbe`
  (`Tools/RunExpansionChecks.py --only native`, `Tools/RunNPCChecks.py`). It covers data
  validation, clamping and round-trip; that `Waves.json` equals the defaults; template
  composition (escort = 1 non-attacking tank + 4 attackers); live edits applying from the
  next wave; skip and spawn-now; runtime escort guards; stuck nudge; out-of-realm return;
  failsafe march then despawn releasing the cycle; neutral packs (never attack first, ignore
  bots, group aggro on a player hit, neutral again after reset); bots choosing the wave over
  a pack; and bot retreat.
- Soak: `UnrealEditor-Cmd CiresTeamSurvival.uproject /Game/Maps/Citadel -game -CireWaveSoak
  -CireWaveSoakCycles=N [-CireWaveSoakPlayer=idle] [-CireWaveNoRescue] -nullrhi -benchmark
  -fps=30 -CireWaveSoakSummary=<file>`. It logs every spawn, clear, phase change and
  failsafe, dumps each unit's state when a wave outlives 150 s, and ends with
  `CIRE_WAVE_SOAK_PASS|FAIL`.
- `-CireSmoke` now computes its expected lives and boss leaks from what the director spawned.
- Captures: `-CireWaveGallery -RenderOffscreen -ForceRes -windowed -ResX=1920 -ResY=1080` writes the
  Waves editor, a neutral (yellow) pack and the same pack provoked to `Saved/WaveGallery/<stamp>/`.

## Navmesh pathing (nav-paths, September 24)

Monsters and bots now follow a runtime navmesh (`Docs/Navigation.md`); the stuck nudge and the stall
failsafe stay as the last resort. Headless soak, bots only, 4 cycles, same command as above
(`Saved/WaveSoak/baseline-bots.*` on main 4adb135, `nav-bots-1.*` and `nav-bots-2.*` on the branch,
summary in `Saved/WaveSoak/summary-nav-paths.txt`):

| Run | Lives lost (Ember + Dusk) | Mean wave | Longest wave | Waves over 210 s | Failsafe march / despawn | Stuck nudges |
| --- | --- | --- | --- | --- | --- | --- |
| Before (straight lines) | 59 + 53 = 112 | 158.1 s | 265.8 s | 3 | 59 / 52 | 83 |
| Navmesh, run 1 | 53 + 57 = 110 | 151.9 s | 265.8 s | 3 | 31 / 24 | 3 |
| Navmesh, run 2 | 52 + 52 = 104 | 165.6 s | 263.7 s | 6 | 49 / 31 | 6 |

Stuck nudges (straight-line stalls on geometry) fell from 83 to 3-6 and failsafe actions from 111
to 55-80. Lives lost and wave length are within run-to-run noise. What still trips the failsafe is
the cycle 3-4 boss wave: the stall reports show every unit engaged in melee with a tank that the
healer keeps up and a Siegebreaker losing health too slowly (for example 13.7k of 23.4k left at
210 s), which is bot damage output against cycle scaling, not pathing.
