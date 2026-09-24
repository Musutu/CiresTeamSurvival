# Role-specific skill choices

Champions begin with no learned skills but one skill point (the opening offer
below). Each later three-level breakpoint presents four unique choices and
reserves six active slots, one passive, and one ultimate. Before a passive is learned, offers contain one or
two passives; a final passive-only slot presents four passives. Role filtering
never reduces the number of choices or borrows another role's healing spells.

## Opening skill point (Eric's ruling, September 24)

Every champion starts with **one skill point**. The opening offer appears as
soon as the champion is locked in (level 1) and contains four **active,
non-ultimate** skills from the champion's **primary** role only (hybrids open in
their primary role); it never contains passives or ultimates. The pools
(`Cires::IsOpeningSkill` / `OpeningSkillPool`):

| Primary role | Opening pool |
|---|---|
| Tank | Shield Slam, War Cry, Iron Guard, Cleaving Strike, Runestone Wall |
| Support | Restoring Light, Sanctuary, Purify, Aegis Dome |
| DPS | every damage-tagged active that is not universal (11 skills) |

After the opening pick, the normal rules continue unchanged. Breakpoints are now
levels **1, 3, 6, 9, 12, 15, 18, 21** (`Cires::BreakpointForSkill`), so the
eighth skill arrives at level 21. The server validates the opening offer as
strictly as the others: a forged passive, ultimate or out-of-pool active is
rejected. Bots receive the same offer in `DraftProfile` and learn it on their
next `BotThink`. The offer cards read "Choose Your Opening Tank / DPS / Support
Ability" (see the skill-offer section below).

## Class baseline traits

Always-active traits by **main** role (`Cires::Traits` maths in the rules module,
applied server-side by `CireClassTraits` through the single damage/stat
pipeline, icons `T_trait_*` in the shared ability icon set):

| Role | Trait | Effect |
|---|---|---|
| Support | Mending Strikes | 50% of the damage the Support deals (after all modifiers) heals the living party member with the lowest health percentage, the Support included (ties: lower absolute health, then stable id). Shown as healing named "Mending Strikes" in combat text, meters and threat. Supports also deal **-20%** damage to enemies and attack **+10%** faster. |
| Tank | Natural Defense | Every incoming damage instance (basic or ability) is reduced by a flat **10, applied last**: after guard, Stone Skin, armor and spell ward. Floor 0 (small hits and DoT ticks can be fully absorbed). |
| DPS | Keen Edge | **10%** base critical-strike chance (tuning base 5% for other roles); items add on top. |

Hooks (all marked `champion-draft`): `CireCombat::ApplyDamage` (Support
outgoing multiplier + Mending Strikes), `ACireHero::TakeDamage` (Tank flat
reduction after `CireItems::ModifyIncomingDamage`), `ACireHero::Recalculate`
(DPS crit) and the basic-attack timer (Support attack speed). Summons, monsters
and undrafted heroes have no trait. The draft screen details panel, the champion
tile tooltips and the action-bar stats row show the trait with its icon and a
passive-style tooltip. Other agents' fixtures that asserted exact damage on a
Tank now compute their expectation with `CireClassTraits::ModifyIncomingDamage`.

## Role tags (Tank / DPS / Support)

Every skill in the pool carries a role **tag set** (bit mask; hybrids and
cross-class skills carry several). `Cires::SkillRoleTags(id)` in
`Source/CiresTeamSurvival/Rules/CiresRules.cpp` is the single source of truth;
unknown IDs have no tags and fail closed. `Content/Data/AstraAbilities.json`
mirrors the tags as `"roles"` for the Astra-authored skills (checked by
`Tools/TestSkillRoles.py`; the game still reads the native table).

| Tags | Skills |
|---|---|
| Tank only | Shield Slam, War Cry; ultimates Last Stand, Challenge of Iron, Seismic Reprisal |
| DPS only | Venom Ground, Cinder Cone, Grave Line, Ashen Ward, Blight Sigil, Piercing Shot, Spectral Pack; ultimates Cataclysm, Executioner's Verdict, Starfall, Spectral Hunt |
| Support only | Restoring Light, Sanctuary, Purify; ultimates Renewal, Mass Aegis, Wellspring |
| Tank + DPS | Cleaving Strike, Shadow Step |
| DPS + Support | Chain Spark, Ember Lance |
| Tank + Support | Bastion of Dawn (ultimate) |
| All roles | Iron Guard, Frost Bind, Runestone Wall, Aegis Dome, Oathbound Guardian, Second Wind; passives Stone Skin, Battle Rhythm, Deep Reserves, Soul Conduit |

Per role: Tank 10 actives / 4 passives / 4 ultimates, DPS 17 / 4 / 4, Support
11 / 4 / 4. Tank healing stays self-only (Second Wind, Last Stand, Bastion's
self-heal); ally heals, cleanses and group heals are Support-only; DPS gets no
monster taunts. Passives are universal so every role keeps the four
alternatives the final passive-only offer needs.

## Champion roles and offers

The server snapshots the champion's roles into `Cires::Progression` at draft:
`DraftRole` is the primary bucket (from the profile's `threatRole`) and
`SecondaryRoles` is the mask of any other roles in its `roles` list
(`CireChampionProfiles::DraftRole` / `SecondaryRoles`). An offer draws only from
skills whose tags intersect *primary | secondary*; `LearnSkill` re-applies the
same mask to the server-stored offer, so a forged support skill is rejected for
a pure tank. `DraftRole == Any` remains the legacy unrestricted catalog mode
used by old fixtures.

| Bucket | Profiles (primary) | Hybrids also drafting this pool |
|---|---|---|
| Tank | Knight, Bear, Righteous Paladin, Dwarf Miner, Granite Golem, Orc Chieftain, Totemic Behemoth, Drakish Footman | Felfire Golem |
| DPS | Ranger, Lancer, Summoner, Felfire Golem, Cinder Arcanist, both Troll variants | Evergrove Centaur |
| Support / Healer | Scholar, Holy Paladin, Verdant Golem, Dryad, Whisp, Evergrove Centaur, Keeper of the Light | Cinder Arcanist, Orc Chieftain |

So the Cinder Arcanist (DPS + Support) can be offered Restoring Light, the Clan
Warlord (Tank + Support) can be offered Sanctuary or Renewal, and the Felfire
Golem (DPS + Tank) can be offered War Cry; single-role champions never see
another role's exclusive skills. Hybrids are deliberately a design choice made
in the roster (`roles`), not inferred from bodies or primary attributes.

The confirmed offer rules are unchanged: four unique choices per three-level
breakpoint; one or two passives until one is learned; four passives when only
the passive slot remains; six actives + one passive + one ultimate. Each role
set has at least nine actives and four ultimates, so taking either special slot
first or last cannot strand a build, and role filtering never shrinks an offer.

## Level-up skill offer (the four cards)

`Source/CiresTeamSurvival/CireSkillOfferHUD.cpp` draws the offer in the same
visual language as the draft screen (style kit + shared ability icons):

- **Cards.** Painted icon in its action-bar frame (ultimate = gold frame, gold
  outer trim and marching glow; passive = octagon frame, violet clipped-corner
  card; active = school-coloured trim), name, role tags (TANK / DPS / SUPPORT,
  HYBRID for cross-role, ANY ROLE for universal), type ribbon, a stats row
  (cost in MP/EN, cooldown, range, target SELF / ALLY / ENEMY / AIM / PASSIVE),
  the numeric description, a synergy line against the learned kit (e.g. slows
  + area skills, taunts + guards, Soul Conduit + your heals, Deep Reserves + the
  mana/energy your kit spends; school match as fallback), and the slot it
  fills ("ACTIVE SLOT 3 / 6", "PASSIVE SLOT", "ULTIMATE SLOT"). Hover lifts the
  card and shows the full tooltip. The header states skill N of 8, the role
  pool(s) and the active rule (1-2 passives until one is learned; final slot =
  four passives).
- **Kit strip.** The eight learned slots (six actives, passive, ultimate); the
  hovered card's destination slot glows.
- **Pick.** Click or the action-bar 1-4 keys (labels come from the keybinding
  map). The learned card bursts, its icon flies into its kit slot and
  `S_SkillLearned` plays; hover ticks and the open chime are `S_SkillHover` /
  `S_SkillOffer` (`Tools/BuildDraftSounds.py`, original synthesized audio).
- **Never blocks combat.** A new offer opens only when you have not dealt or
  taken damage for ~1.5 s; otherwise it waits as a pulsing "NEW ABILITY READY"
  reminder above the action bar. `ToggleSkillOffer` (N) or DECIDE LATER hides
  it; N or a click reopens it. While hidden, action-bar keys cast, clicks
  target and ground aiming works (controller/targeting honour
  `ACireHUD::IsSkillOfferOpen`). The shared Level Up banner plays first; the
  offer header fades in after it.

Verification: `python Tools/RunSkillOfferGallery.py` renders five 1920x1080
states (normal offer with a hovered card, ultimate offer, passive-only final
offer, deferred reminder, pick animation) into `Saved/SkillOfferGallery/<stamp>`.
Offer contents and rules still come from the server (`GenerateAugmentOffer`);
the fixture only stages known offers for the captures.

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

Recorded verification (September 24, 2026, champion-draft pass):
**1,930,816 native rules assertions, 0 failures** (MSVC `/W4 /WX /O2`). The new
`RoleTagRules` suite checks every skill's tags, the documented exclusive and
cross-role tags, all 3 primary x 8 secondary role sets over 384 full build
paths each (every alternative learnable, offers never leave the role set, a
pure tank never sees a Support-only skill, every allowed skill including
hybrids is actually offered, pools never run dry), hybrid-only reachability
(e.g. Tank+Support gets Restoring Light, Tank alone never does) and forged-offer
rejection. `Tools/TestSkillRoles.py` (5 tests) keeps the Astra mirror, roster
examples and role buckets consistent; `Tools/TestChampionRoster.py` 8 tests.
In-engine `CireChampionProfiles::RunSmoke` passed 250 checks (including that
each drafted hero snapshots primary + hybrid roles) and role-skill smoke 28
checks in `Saved/ExpansionChecks/20260924T090219703895Z/report.json`.

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
