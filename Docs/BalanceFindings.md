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
