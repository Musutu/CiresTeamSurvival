# Role-specific skill choices

Normal champions still begin with no learned skills. Every level-three
breakpoint presents four unique choices and reserves six active slots, one
passive, and one ultimate. Before a passive is learned, offers contain one or
two passives; a final passive-only slot presents four passives. Role filtering
never reduces the number of choices or borrows another role's healing spells.

The server snapshots a `Cires::SkillDraftRole` in progression. It follows the
selected profile's gameplay role, not its body or primary attribute:

| Bucket | Profiles |
|---|---|
| Tank | Knight, Bear, Righteous Paladin, Dwarf Miner, Granite Golem, Orc Chieftain, Totemic Behemoth, Drakish Footman |
| DPS | Ranger, Lancer, Summoner, Felfire Golem, damage Wizard, both Troll variants |
| Support / Healer | Scholar, Holy Paladin, Verdant Golem, Dryad, Whisp, Evergrove Centaur, Keeper of the Light |

Tank actives (10): Iron Guard, Shield Slam, War Cry, Cleaving Strike, Frost Bind,
Shadow Step, Summoned Wall, Protection Dome, Oathbound Guardian, Second Wind.
Tank ultimates: Bastion of Dawn, Last Stand, Challenge of Iron, Seismic Reprisal.
Tank healing is self-only: Second Wind, Last Stand, and Bastion. Bastion also
guards teammates but does not heal them. Ally-targeted Restoring Light/Purify,
group Sanctuary/Renewal, and Wellspring are excluded from Tank and DPS offers.

DPS actives (17): Iron Guard, Chain Spark, Ember Lance, Venom Ground, Cinder Cone,
Grave Line, Ashen Ward, Blight Sigil, Frost Bind, Cleaving Strike, Piercing Shot,
Shadow Step, Summoned Wall, Protection Dome, Oathbound Guardian, Spectral Pack,
Second Wind. DPS ultimates: Cataclysm, Executioner's Verdict, Starfall,
Spectral Hunt. DPS receives no monster-taunt skills.

Support actives (11): Restoring Light, Sanctuary, Purify, Iron Guard, Chain
Spark, Ember Lance, Frost Bind, Summoned Wall, Protection Dome, Oathbound
Guardian, Second Wind. Support ultimates: Renewal, Bastion of Dawn, Mass Aegis,
Wellspring. Offensive options let healers contribute between healing needs.

All buckets share Stone Skin, Battle Rhythm, Deep Reserves, and Soul Conduit.
These affect the owner's defenses, attacks, resources, or healing; Soul Conduit
also benefits self-healing. Keeping four passive alternatives is necessary for
the four-choice final passive offer. Each bucket has at least nine actives and
four ultimates, so taking either special slot first or last cannot strand a
build. Unknown recipes and other-role skills fail closed in role-filtered drafts.

## New native abilities and tuning

`Content/Data/CombatTuning.json` adds optional `roleSkills` entries. Missing
entries retain native defaults for old configurations; existing Astra patches
preserve this section through the game importer. Native and Python validation
reject unknown IDs, duplicates, nonfinite/out-of-bounds numbers, and damaging
area warnings shorter than 0.2 seconds. The Astra UI does not yet expose a role
recipe form; edit these validated JSON entries or import a roleSkills patch.

| ID | Effect and initial tuning |
|---|---|
| second_wind | Self heal 18% max HP; 30 energy, 20s cooldown |
| last_stand | Self heal 40% max HP and cleanse slow; 50 energy, 85s cooldown |
| challenge_of_iron | 850cm monster taunt (10s cap) and 12s self guard; 65 energy, 90s cooldown |
| seismic_reprisal | Self-centered 450cm circle, 0.8s warning, 160 + 3× primary damage; 60 energy, 65s cooldown |
| starfall | Aimed 500cm circle, 1.25s warning, 180 + 3.5× primary damage, 1500cm range; 130 mana, 80s cooldown |
| spectral_hunt | Three AI hunters for 18s, each attack 58 + 0.4× primary, requires hostile target within 1200cm; 125 mana, 90s cooldown |
| mass_aegis | Nearby allies within 900cm: slow cleanse and 12s guard, no healing; 120 mana, 85s cooldown |
| wellspring | Selected ally or self: heal 220 + 4× primary and 5s guard, 1200cm range; 130 mana, 75s cooldown |

Guard uses the existing 40% damage reduction and extends the expiry rather than
stacking mitigation. Damage and effective healing use the authoritative combat
event pipeline. Area ultimates use native replicated ground warnings and floor
checks; summon limits, collision, realm and phase cleanup reuse existing actors.
Spectral Hunt inherits hunter health/movement/attack interval from Spectral Pack,
then overrides count, damage, lifetime and cast range. Costs/cooldowns are paid
only after a valid effect or successful spawn. These are initial authored
numbers, not a claim of measured balance.

`CireTargeting::Describe` is the HUD's structured Self, Ally, Enemy, and Ground
metadata, including range, self fallback, area origin, and native footprints.
`CireChampionProfiles::SkillTargeting` remains a compatibility text helper.
`DraftRole` and `Cires::DraftRoleName` provide HUD role captions. Implemented
thematic developer loadouts must also respect their role; loading them advances
to at least level 24 and keeps progression valid. Planned roster skills remain
design records outside the draft catalog.

## Verification

`Tests/Run-Tests.ps1` runs the engine-independent draft tests, including every
offered alternative's next-step validity across thousands of role/build paths.
`Tools/TestChampionRoster.py` checks role examples and range profiles.
`Tools/TestImportCombatTuning.py` checks patch preservation and bounded role
recipes. `CireRoleSkills::RunSmoke` is the separate in-engine check for self vs
ally healing, failed-cast payment, taunt/guard, and telegraph damage timing.
Offline rule/authoring tests do not establish runtime balance or rendering.

Recorded verification for this pass: 608,488 offline rules assertions with no
failures, eight roster tests, seven tuning-import tests, and native role smoke
28 checks after the successful rebuild. The native run includes Starfall LOS
and Spectral Hunt duration regressions and is recorded in
`Saved/ExpansionChecks/20260924T053213951677Z/report.json`.
Detailed measured findings and the user's stop status are in
[BalanceFindings.md](BalanceFindings.md).

## Measured loadout comparisons

`CireBalanceLab::StartLoadout` or `-CireBalanceLoadout=thematic` selects a full
six-active/one-passive/one-ultimate fixture for each of the five representative
roles. Presets `tank_last_stand`, `tank_challenge`, `tank_seismic`,
`support_aegis`, `support_wellspring`, `dps_starfall`, and `dps_hunt` change only
that role's ultimate. The old partial-skill fixture remains `baseline`.
Fixture level still follows the wave, so full builds at low levels explicitly
bypass unlock pacing for comparison. These are development tests, not new
starting-skill behavior.

After compiling the game, explicitly run:

```powershell
python Tools/RunLoadoutLab.py --waves 10 --repeats 3
```

The runner launches bounded headless encounters sequentially and stops only
the processes it starts. It saves raw reports and continuously updates
`Saved/BalanceLab/loadouts-*/loadouts.json`, `.csv`, and `.md`. Each raw report
records exact skills, starting stats/range, damage/healing totals, per-hero
DPS/HPS/share, threat, and the tuning snapshot. The report compares same-wave
medians against `thematic`; default review thresholds are DPS above 1.25× or
victory time below 0.8×, configurable with `--dps-ratio` / `--ttk-ratio`.
Losses never count as fast victories. Fewer than three repeats is flagged.
Random combat and AI mean these flags prompt investigation; they do not prove
an ability is overpowered. No simulation result automatically changes tuning.

Increase encounter population without editing combat defaults with `--enemies`
(1–20). For a bounded support-pressure comparison:

```powershell
python Tools/RunLoadoutLab.py --waves 20 --enemies 15 --presets thematic,support_aegis,support_wellspring --repeats 3 --seconds 90
python Tools/AnalyzeLoadoutLab.py Saved/BalanceLab/loadouts-<run>/loadouts.json
```

The analysis writes per-champion contribution medians and exact equipped IDs.
It separates both wave and enemy count. Raw reports do not yet record per-skill
cast/hit counts, so equipping an ultimate is not proof it fired. Short fights
with little injury under-sample healing; increasing only the wave also levels
the heroes and may not create enough pressure to test sustain.
