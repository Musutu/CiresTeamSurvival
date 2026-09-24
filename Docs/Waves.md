# Waves: the wave director and composer

`Source/CiresTeamSurvival/CireWaves.h/.cpp` (runtime), `CireWaveData.cpp` (data),
`CireWaveEditor.cpp` (F8 panel), `CireWaveSoak.cpp` (headless soak), `CireWaveTests.cpp`
(native tests). Data: `Content/Data/Waves.json`.

## Match flow

A cycle is **N cleared waves → prep (60 s) → arena (90 s) → recovery (15 s)**. N is
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
  "breatherSeconds": 8,          // cleared wave -> next spawn            (0..120)
  "wavesPerCycle": 5,            //                                      (1..10)
  "cycles": 0,                   // 0 = loop forever with scaling; N = match ends after cycle N, most lives wins (0..50)
  "cycleScaling": { "healthGrowth": 0.15, "damageGrowth": 0.10, "extraUnits": 1 },  // per completed cycle
  "failsafe": { "enabled": true, "maxWaveSeconds": 210, "action": "march", "graceSeconds": 45, "stuckSeconds": 5 },
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
×0.1–20, damage ×0.05–10, size ×0.5–3, spawn interval 0–5 s, delay 0–120 s, reward ×0–10,
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

Eric's example: **1 Normal, 2 Normal, 3 Armored, 4 Armored Escort, 5 Boss**, as one
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
- Cycle scaling is gentle (+15% health, +10% damage, +1 unit per row per cycle), because
  health already grows +80 per global wave number (five more waves per cycle). In the first
  soak at +30% the growth compounded to about ×1.8 health per cycle, and bots-only
  teams collapsed in cycle 3.
- Walking time: the route is about 155 m, so a unit at 2.1 m/s needs roughly 75 s to reach
  the castle unopposed (the boss needs about 105 s). Clears therefore take about 90–200 s
  even with good defence. That is the expected pace, not a stall. The stall failsafe's
  210 s limit sits above it.

Measured with the headless soak (bots only, 30 Hz fixed step). See
`Saved/WaveSoak/*.txt` for the per-wave clear times and lives.

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
- **Race per wave.** A row with a `slot` (line, bruiser, tank, caster, ranged, special, warlord, colossus, boss) takes that
  slot's unit from the wave's race; `archetype` keeps the hollow unit as the default. `boss` alternates the race's colossus
  (odd cycles) and warlord (even cycles). A wave's `race` overrides the rotation; `a+b` rotation entries mix races row by row.
- **Default campaign.** Cycle 1 hollow basics, cycle 2 Blightwood, cycle 3 the Drowned Deep, then Ironhide, a mixed
  Hollow + Blightwood host, Stoneborn, Drakkari, Drowned + Voidborn, Feral Kin, Fallen Order, Voidborn, Ironhide + Drakkari.
  Every cycle ends on one of its race's bosses; bosses are mythic from cycle 3. From cycle 2 every 4th normal attacker is
  promoted (veteran, elite from cycle 3, champion from cycle 4). The second lap of the rotation reskins with palette 1.
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

1. **Lane leash**: a wave unit drops a victim more than 18 m away and returns to the route.
2. **Stuck detection**: sampled every second. A unit that moves under 60 cm for `stuckSeconds`
   while not attacking, casting, paused or escorting is **nudged** onto its route 4.5 m closer
   to the castle (routes are kept clear of props), dropping any unreachable victim for 6 s.
   A unit knocked outside its realm is returned to the route.
3. **Stall failsafe**: `maxWaveSeconds` after a wave's last spawn, its leftovers stop
   fighting, ghost through characters and **march** to the castle (a leak costs lives).
   After `graceSeconds` anything still alive despawns. `action: "despawn"` skips the march.
4. **Bot steering**: bots that stop making progress detour along the road, or sidestep if
   they are already on it (`CireWaveDirector::BotSteer`).

Phases after prep, arena and recovery are clock-driven. Only survival waits on an objective,
and with the failsafe on it can never exceed roughly `maxWaveSeconds + graceSeconds` per wave.

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
