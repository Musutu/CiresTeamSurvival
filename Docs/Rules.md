# Cire's Team Survival: rules contract

The engine-independent C++17 module in `Source/CiresTeamSurvival/Rules` defines progression, drafting, phase timing, and reward calculations. Unreal uses the same module in the actual game. Its values are prototype tuning, not a claim of Enfo's balance parity or production readiness.

## Implemented progression

- Level 1 begins with basic attacks and no learned skills. Each additional level gives +2 primary stat and +1 to each other stat.
- Each STR gives 25 maximum health; each INT gives 30 maximum mana; each AGI adds one percentage point to the attack-speed multiplier. The primary stat adds one basic-attack damage per point, plus the weapon baseline. The playable champions use a 12-damage baseline weapon; the reusable module's default tuning is 10.
- Energy is a separate 100-point resource. Cooldown reduction is separate from damage, multiplicative on duration, and capped at 60% for the prototype.
- The user confirmed the final build contains one innate basic attack plus **six active abilities, one passive, and one ultimate**. These are eight learned slots; the basic attack is separate. Four unique choices still appear at each three-level breakpoint, through level 24. A skipped level threshold queues a choice; it is not lost. Stats continue to grow afterward; later skill upgrades/replacements are not implemented.
- The starter catalog contains 12 active skills, four passives, and four original ultimate options. Learned skills cannot reappear. Active/passive/ultimate capacities are enforced independently, and invalid or undersized catalogs fail explicitly rather than violating slot restrictions.
- IDs are sorted before a portable seeded generator shuffles the eligible catalog. A fixed seed gives identical ordered offers regardless of catalog input order. The server retains the offer and validates selection; clients send only a choice index.

## Match cycle and rewards

The confirmed cycle is **three cleared PvE waves → 60 seconds of town preparation → an arena lasting at most 90 seconds → 15 seconds of recovery → the next wave cycle**. PvE has no five-minute deadline. A wave completes when all its ordinary advancing monsters in both lanes have died or leaked; optional challenge packs never block completion. The next wave follows an eight-second breather, while the third clear starts preparation. The first match has a ten-second spawn warmup after drafting. Recovery ends directly in the next cycle's first spawn.

Each team starts with **100 defense lives**. A regular creep reaching its city costs one life; a boss costs ten, clamped at zero. Reaching zero immediately ends the match as an overrun. As an initial encounter schedule, the final wave of each cycle includes a Hollow Siegebreaker alongside its normal escorts. The boss has eight times a same-wave creep's health, 2.5 times its damage, 80% movement speed, and a larger silhouette. The boss timing and combat values are playtest defaults; its ten-life leakage cost is a user requirement. Bosses count toward wave completion, while challenge-pack membership is identified independently by `PackId`.

An elimination or timeout resolves the arena once. The Unreal game mode compares surviving players, then their remaining health fractions; equal results are draws. Players return to their own base for recovery with restored resources. Monsters cannot attack or spawn during preparation, arena, or recovery. Optional challenge packs refresh for the next PvE cycle, without cleanup kill rewards.

`MatchClock::BeginIntermission` is called by the authoritative wave controller after the cycle is cleared. `RemainingSeconds` returns -1 during untimed PvE. `Advance` accepts finite, nonnegative deltas up to one day and tolerates normal floating-point frame accumulation. It crosses at most one timed boundary per call and discards excess, so a long hitch cannot skip arena teleport or recovery. Early arena resolution enters recovery; the round number increases only when recovery ends. Finished is terminal. `PhaseEvent::Round` identifies the round that just transitioned. Team movement, waves, and winner selection belong to the Unreal layer.

Each arena victory currently grants +3% team power and +8% loot, capped at +12% and +40%. These bounded defaults prevent unlimited reward compounding; they need match telemetry and player balance testing.

Challenge tiers 1–10 increase health, damage, XP, gold, rare-drop chance, and Greater Stat Tome chance. Difficulty also scales with survival rounds (bounded at 100 rounds). Loot modifiers are bounded from 1 to 1.4. Gold has ±10% variance. Rare chances cap at 35%; Greater Tome chances cap at 70%. A normal tome grants one point per stat; a Greater Tome grants two to five, depending on tier. The Unreal layer grants a pack's completion reward only after all three members die and only once per pack ID. The current playable map exposes tiers 1–3.

## Runtime combat

`CireHero.cpp` implements server-authoritative damage, mana/energy costs, cooldowns, maximum-one-passive skill acquisition, shop purchases, level gains, bot behavior, pack aggro/leashing, and ten-second PvE revival. Arena deaths wait for the next phase. Human movement uses Unreal character movement replication. Healing and hostile-target selection work with replicated phase/team data on clients. Slows replicate their server-time expiry to support client movement prediction.

The starter skills provide melee and ranged damage, a four-target chain spell, radial cleave, slowing, a dash, monster/bot taunts, damage reduction, direct healing, group healing, and slow cleansing. Taunts affect monster and bot decisions; they do not force a human player's selected target. Passive choices provide 10% damage reduction, 20% faster basic attacks, 50% extra resource regeneration, or 25% stronger healing. Colored replicated debug traces are temporary ability feedback. Animation montages, production VFX, resistances/damage-school interactions, threat tables, cast interruption, and complete combat prediction remain future work.

The four ultimate options are Bastion of Dawn (self-healing and nearby ally protection), Cataclysm (an area strike centered on the selected enemy), Executioner's Verdict (a single-target finisher with a bounded missing-health bonus), and Renewal (nearby team healing and slow cleansing). They use the same authoritative target, resource, cooldown, damage/healing, and telemetry paths as normal skills. Ultimate costs, coefficients, ranges, and cooldowns remain prototype tuning. Renewal does not revive dead allies.

Bots use the same resource and cooldown gates. They favor survival waves over unengaged challenges, seek wounded allies for healing, and shop during the town window. Their current steering follows direct movement vectors and has no navigation/pathfinding integration; complex obstacle arenas require that work before serious balance testing.

## Verification

Run `Tests/Run-Tests.ps1`. It uses CMake when available, or automatically locates Visual Studio C++ Build Tools and runs `Tests/Run-MSVC.cmd`. A portable CMake entry point is also supplied for other C++17 toolchains.

Verified on September 23, 2026 using the installed MSVC toolchain, `/std:c++17 /W4 /WX /permissive- /O2`: **46,514 assertions, zero failures**. Tests cover stat formulas and overflow rejection; 1,000 deterministic unique drafts; every passive-selection position; queued breakpoints and six-slot limits; invalid and replayed selections; untimed PvE, explicit completion, timed phase boundaries, early arena resolution, recovery, long hitches and fractional frames; ordinary/boss leak cost and zero-life clamping; reward caps and 10,000 seeded challenge rolls. The latest native runner compiled and executed successfully; an earlier run's Windows Application Control block did not recur.

The development `-CireSmoke` engine fixture uses real wave spawns and leakage, accelerated preparation/arena/recovery durations, and surviving optional challenge packs to exercise the wave-driven transition path. It checks ten heroes, three cleared waves, every required phase, both bosses leaking, the resulting 75/75 lives, and entry into cycle two. This fixture validates transition behavior, not full-duration balance.

These native tests validate the pure rules. They do not establish Unreal networking correctness, complete match balance, production animation quality, or 1:1 parity with any particular version of Enfo's Team Survival.

## Dedicated server and remote client smoke test

Run `python Tools/RunNetworkSmoke.py` after building the Unreal Editor target. The default editor path is `F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe`; use `--editor` to override it. The runner starts its own hidden dedicated server on UDP 7781 and a separate remote client, both with rendering disabled. It refuses an occupied port and only cleans up the two child processes it created. Logs and a machine-readable report are saved under `Saved/NetworkSmoke`.

Explicit development flags enable bounded probes; their code is excluded from shipping builds. The test checks replicated match/team/champion state, selection through the normal server RPC, remote movement on both peers, hostile target selection/replication, rejection of illegal purchases and invalid skill/draft inputs, and preservation of the same champion and 5v5 membership after disconnection. A harmless target and stationary bots isolate the test from combat rewards. These fixtures are only created by the dedicated-server probe flag.

The full follow-up run passed on September 23, 2026 in 23.72 seconds with both processes exiting zero; evidence is `Saved/NetworkSmoke/20260923T124107686687Z/report.json`. The remote client observed 319.6 cm of movement; the server independently recorded 332.2 cm after deceleration. The server retained the same champion as a bot with five members on each team after the client disconnected. The INT draft replicated 250 HP and 600 MP, and rejected actions retained 120 gold and zero skills.

The initial run exposed unsupported procedural-floor network references. Marking identically named world components as network-addressable removed that warning spam. The follow-up client still logged one startup warning that its movement base had not yet resolved; later corrections and the movement probe succeeded. No fatal errors or assertions were found. A local smoke test does not cover Internet matchmaking, packet loss, high latency, reconnect identity, anti-cheat, or ten simultaneous human connections.
