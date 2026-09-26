# Measured loadout findings — 24 September 2026

Balance testing stopped at the user's request. The completed 24-case comparison
and three pressure cases are preserved. No simulation processes remain, no more
cases are scheduled, and no balance defaults or stat-per-point formulas changed.

## Completed comparison

Actual authoritative five-hero AI versus five mixed NPCs, wave/hero level 10,
eight equipped builds, three independent runs each. Every fight was won with
all five heroes alive; every process exited naturally with code zero. All builds
had six actives, one passive, and one ultimate, deliberately bypassing normal
unlock timing inside the fixture. Ordinary champions still begin without skills.

| Equipped variation | Median victory seconds | DPS | HPS | Tank target share |
|---|---:|---:|---:|---:|
| Thematic reference | 14.11 | 589.7 | 12.1 | 72% |
| Last Stand | 13.21 | 629.7 | 12.9 | 73% |
| Challenge of Iron | 13.87 | 599.9 | 13.2 | 87% |
| Seismic Reprisal | 13.77 | 604.3 | 14.1 | 81% |
| Mass Aegis | 13.34 | 623.6 | 14.3 | 70% |
| Wellspring | 13.82 | 601.8 | 12.4 | 71% |
| Starfall on three DPS heroes | 12.51 | 665.3 | 14.4 | 79% |
| Spectral Hunt on three DPS heroes | 15.02 | 553.9 | 11.4 | 72% |

No variation crossed the review gates of 1.25× reference DPS or below 0.8×
reference victory time. Starfall's equipped group reached 1.13× DPS and 0.886×
victory time. This is a small, unpaired stochastic comparison, not proof of final
balance or an individual ultimate's power. Reports record equipped IDs and
champion totals; they do not record per-ability casts or hits.

Thematic Lancer median contribution was 86.9 DPS and 14.7% of team damage,
versus Ranger 133.5 DPS/21.8% and Summoner 183.6 DPS/31.2%, including credited
summon damage. These cases show no Lancer overperformance signal. The Scholar
had zero effective healing in all three thematic runs, limiting sustain analysis.

Evidence: `Saved/BalanceLab/loadouts-20260923-224712-6c9d56/` contains
`loadouts.json/.csv/.md`, raw-report links, and `contributions.json/.md`.

## Limited pressure sample

Three completed cases of nine planned: five level-20 heroes versus 15 mixed
NPCs, one run per build. All five heroes survived each fight.

| Equipped build | Victory seconds | DPS | Team HPS | Scholar effective healing |
|---|---:|---:|---:|---:|
| Thematic | 46.00 | 829.6 | 44.7 | 1795.6 |
| Mass Aegis | 48.61 | 785.1 | 52.9 | 2308.5 |
| Wellspring | 46.05 | 828.7 | 51.6 | 2116.2 |

This exposed meaningful healing demand but has only one sample per build;
it cannot rank the support ultimates. The last child completed its report and
normal shutdown after its runner stopped; its exit code was not observed.
Evidence: `Saved/BalanceLab/loadouts-20260924-012529-99e2b0/`, marked
`stopped_at_user_request`, three completed cases of nine planned.

## Confirmed implementation corrections

Read-only review found that bot/native Starfall casts omitted the controller's
line-of-sight check, and Spectral Hunt applied an enabled developer duration
multiplier twice. Source fixes now reuse ground preflight and apply summon
duration scaling once. Native regressions cover blocked-cast resource rollback,
accepted aim after obstacle removal, and all three hunters' actual duration and
replicated expiry. The rebuild passed, and native role checks passed 28/28 in
`Saved/ExpansionChecks/20260924T053213951677Z/report.json`; targeting checks
also passed 17/17 and 18/18. The measured cases above used neutral overrides
and an unobstructed platform. These corrections do not change
authored damage, healing, stat growth, or default durations.

# Champion lab, round 1 — 25 September 2026 (feat/balance)

`python Tools/RunChampionLab.py --waves 3,10,20 --repeats 3 --parallel 3`: each champion takes one
fixture slot beside the knight/scholar/ranger/lancer/summoner team against 8 mixed NPCs. Ratios are
personal DPS (or HPS) against the role median; the review band is 0.8-1.25×.

| Champion | Before w3 / w10 / w20 | After w3 / w10 / w20 | Change |
| --- | --- | --- | --- |
| Aetheri Artificer (DPS) | 1.31 / 1.36 / 1.24 | 1.10 / 1.23 / 1.08 | Turret 18 → 12 base; Skitter 70+0.8× → 45+0.6× INT, 3 live; Arc Mine 110+1.2× → 70+0.9× INT, 2 live; Obelisk 60+1× → 45+0.8× INT |
| Gunblade (DPS) | 0.79 / 0.91 / 0.98 | 0.54 / 0.94 / 0.92 | Silver Shot 95 → 150, Powder Flask 70 → 110, Blade Flurry 40 → 60 (4.2 m), energy 30/30/35 → 20/20/25, Hex Mark 15 → 20%, execute 20 → 15% |
| Witch Slayer (DPS) | 1.01 / 0.77 / 0.82 | 0.97 / 0.90 / 1.16 | Blunderbuss 110 → 135, Spectral Blade 90 → 115 |
| Aetheri Warden (HPS) | 1.38 / 1.29 / 1.00 | 1.04 / 1.10 / 1.06 | Aegis DR 15 → 10%, Nexus DR 40 → 25% (Ability DB data) |

Evidence: before `Saved/BalanceLab/champions-round1-20260925-120237-45ea52/`, after
`Saved/BalanceLab/champions-round1-after-20260925-141648-b18f8e/` (99 cases, 0 failures). The after
run's fights are shorter (10-20 s against 23-39 s), so compare ratios, not absolute DPS.

Still outside the band, not tuned in this pass:

- **Summoner** 1.58 / 1.53 / 1.23: summons front-load damage in short fights.
- **Lancer** 0.66 / 0.75 / 0.75 (six runs each, every wave).
- **Gunblade at wave 3** 0.54 (0.79 before): not yet explained, possibly its cooldowns in a 12 s fight; in band at 10 and 20.

# Champion lab, kits-complete — 25-26 September 2026 (feat/kits-complete)

The 13 roster champions whose kits were planned now fight with their real kits. `Tools/RunChampionLab.py` adds them
(DPS: both Trolls and the Ether Golem Bruiser; healers: Holy Paladin, Golem Support, Dryad, Whisp, Centaur, Keeper;
tanks in the knight's slot, compared on personal DPS against the tank median, with a survivability column).
Five tuning rounds (`--waves 3,10,20 --repeats 2-3`, 162-243 cases each, 0 failures); the measured coefficients live
in `LAB_SCALING` (Tools/ChampionKits.py). Verification run: `Saved/BalanceLab/champions-kits-verify-20260925-232938-02fb26/`.

| Champion | w3 / w10 / w20 vs role median |
| --- | --- |
| Bear (tank) | 1.25 / 1.20 / 1.19 |
| Righteous Paladin (tank) | 0.96 / 1.01 / 0.96 |
| Dwarf Miner (tank) | 0.94 / 0.86 / 0.73 |
| Ether Golem Tank | 1.22 / 1.06 / 1.04 |
| Orc Chieftain (tank) | 0.86 / 0.86 / 0.62 |
| Totemic Behemoth (tank) | 1.28 / 1.21 / 1.05 |
| Drakish Footman (tank) | 1.04 / 0.99 / 1.16 |
| Ether Golem Bruiser (DPS) | 0.90 / 1.07 / 0.95 |
| Troll Berserker melee (DPS) | 0.99 / 0.97 / 0.93 |
| Troll Berserker ranged (DPS) | 0.86 / 0.90 / 0.82 |
| Holy Paladin (HPS) | 1.25 / 1.44 / 1.23 |
| Ether Golem Support (HPS) | 0.81 / 0.89 / 1.02 |
| Dryad (HPS) | 1.15 / 1.01 / 1.42 |
| Whisp (HPS) | 0.80 / 1.22 / 0.83 |
| Evergrove Centaur (HPS) | 0.44 / 0.99 / 0.98 |
| Keeper of Light (HPS) | 0.85 / 0.55 / 0.49 |

Caveats: fights last 10-17 s and the whole team loses only ~5 HP/s, so healer HPS is bounded by missing health and
swings 0.4-1.9 between identical runs (the Scholar fixture itself ranged 0.79-1.93 across rounds); two samples per
cell. Still outside the band: Keeper of Light at waves 10/20 (consistently low: its heals mostly land on full-health
allies), Orc Chieftain and Dwarf Miner at wave 20 (tank DPS, low by design for a support tank), Holy Paladin at wave
10, Dryad at wave 20, Centaur at wave 3, Behemoth at wave 3 (1.28). The knight fixture sits at 0.6-0.7 of the tank
median: the roster tanks out-damage it, and it was not tuned here.
