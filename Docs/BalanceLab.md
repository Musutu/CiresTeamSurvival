# Balance lab and progression forecasts

The lab is an explicit development sandbox. It runs actual authoritative heroes,
NPCs, projectiles, summons, area effects, damage, healing, and threat. It does not
change authored tuning. Offline forecasts are a separate expected-value model.

## Measured in-engine fights

Use the developer panel's balance controls or launch a development game with:

```text
-CireBalanceLab -CireBalanceWave=10 -CireBalanceKind=-1 -CireBalanceBots=5 -CireBalanceEnemies=5 -CireBalanceSeconds=60
-CireBalanceLab=Player -CireBalanceWave=10
-CireBalanceLab=Arena -CireBalanceBots=5
-CireBalanceLab=PlayerArena -CireBalanceBots=5
```

All-AI wave mode uses five NPC-controlled hero roles (tank, healer, ranger,
lancer, summoner) versus actual PvE monsters. Arena mode uses opposing hero AI
teams with the existing arena hostility rules. Player mode temporarily gives
the player the tank fixture; Stop restores the original champion and camera.
The fixture loadout bypasses the ordinary skill draft only in this sandbox.
Wave tests use hero level = wave, bounded to 1000; arena tests use level 10.
Kind `-1` mixes basic/bruiser/caster/ranged; `0–3` selects one kind. Teams cap at
5, enemies at 20, and duration at 300 seconds. The GUI may offer narrower ranges.

Original combat actors, movement and transient spell actors are suspended. The
fixture uses a visible elevated floor inside the proper lane or arena bounds.
The normal match clock/wave orchestration pauses during the run; original
rosters, rewards, lives, phase, camera and relevant status timers are restored.
Watch mode keeps the camera on the AI tank. Avoid manual combat inputs during
an all-AI measurement. Player mode permits ordinary movement and casting.
This is an encounter test, so eliminated fixtures do not respawn. It ends when
one side is eliminated, the duration cap is reached, the phase changes, or Stop
is pressed. Developer overrides are applied through the same NPC Configure path
as normal spawns; each report records the effective initial NPC health/damage.

Reports go to `Saved/BalanceLab/runtime-<time>-<id>.json`. They include:

- `kind: measured-runtime`, scenario, result and game-time duration;
- effective team damage/DPS and healing/HPS from actual combat meters;
- initial actor health, damage, level, skill loadout, and final hero results;
- remaining enemy health, surviving units, per-second samples;
- tank share of NPC target time, victim switches, mean tank/highest-other threat;
- tuning file snapshot and developer profile at completion;
- review flags for timeout, zero damage, very short or long encounters.

Live hit/crit rolls and engine navigation make these measurements nondeterministic.
Repeat a scenario to assess variance. Tank target share is an aggro metric, not
a damage reduction metric; summons can legitimately hold aggro. Arena has no NPC
threat table, so its threat metrics are zero. Effective damage may include damage
to enemy summons/constructs; it is the game's actual meter, not merely initial
enemy health divided by time. A surviving enemy's health can change through normal
abilities. The source JSON snapshot is not a substitute for recorded effective
actor values when developer overrides are enabled.

Integration: call `CireBalanceLab::Initialize(Mode)` at the end of GameMode
BeginPlay. Near the start of GameMode Tick, `if(CireBalanceLab::Tick(this,Delta))
return;` prevents normal encounter orchestration while active. Native callers
can use `Start`, `StartArena`, `Stop`, `IsActive`, `Snapshot` and `Summary` from
`CireBalanceLab.h`. Shipping builds refuse to start the sandbox.

## Deterministic forecasts

From the project directory, using Python 3:

```powershell
python Tools/SimulateBalance.py
python Tools/SimulateBalance.py --fixed-level 10 --out Saved/BalanceLab/fixed-level-10
python Tools/SimulateBalance.py --waves 1,10,100,1000 --min-ttk 8 --max-ttk 90 --min-tank-share .5 --fail-on-gate
python Tools/TestSimulateBalance.py
```

The input is the actual validated `Content/Data/CombatTuning.json`; `--tuning`
selects another file. The script writes JSON, CSV and Markdown. If matplotlib is
available, it also writes `forecast-curves.png`. Default scenarios cover waves
1 through 1000 at selected milestones. `--fixed-level` isolates NPC health growth
from hero progression. Without it, hero level equals wave, an explicit scenario
assumption rather than a model of the XP/shop economy. The native level formula
supports level 10000, which can also be stress-tested offline.

The model uses authored crit, threat, NPC damage/health, projectile and summon
values, with native base stat/attack formulas. It uses fractional expected hit
and crit damage, not random trials. Projectile contact defaults to `.75`, area
contact to `.5`, controlled by `--projectile-contact` and `--area-contact`.
It includes five roles' basic attacks, piercing shots, summoner attacks/companions
and effective single-target healing. It excludes tank guard/taunt skills,
navigation, body blocking, team target spreading, area multi-hit, companion
deaths, gear, economic rewards and most authored skill combinations. Continuous
attack cadence has no rendering frame-rate cap. These assumptions appear in
every report alongside source hashes and the complete tuning snapshot.

Default review bands are 8–90 seconds TTK, at most 15% DPS growth per hero level,
and at least 50% tank target share. These are **review thresholds**, not approved
game design targets. `--fail-on-gate` returns exit code 2 if any threshold flags;
ordinary reporting returns 0. Invalid inputs return 1. No setting is changed
automatically. Flags should lead to a measured lab run and a design decision.

The first forecast with current inputs found a late-game acceleration: modeled
five-enemy TTK was 25.3 seconds at wave/level 1, 34.05 at 20, 24.75 at 100, and
4.8 at 1000. Basic damage and attack speed both grow with stats, while ordinary
NPC health grows linearly and NPC hit damage stays fixed. At fixed hero level
10, increasing enemy health instead produced long fights and eventual timeouts.
This contrast identifies a progression question; it does not establish that
real gameplay is balanced or authorize a stealth stat cap. Low-level tank-share
flags also omit the real tank's taunt/guard toolkit, so verify them in-engine.

Run the bounded measured comparison after building the current game DLL:

```powershell
python Tools/RunBalanceLab.py --waves 1,10,30 --seconds 90
```

The runner starts one headless process per case and writes comparison JSON,
CSV and Markdown beside its logs under `Saved/BalanceLab/measured-*`. Native
`-CireBalanceExit` requests exit after the report and fixture cleanup. For older
binaries, the runner waits ten seconds after a complete report, stops only its
own child process, and records that termination separately from the combat
result. A timeout without a complete report fails the run. Measured results
are single random trials; the forecasts are deterministic simplified models.
