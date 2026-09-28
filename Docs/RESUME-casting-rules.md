# RESUME — feat/casting-rules (Playtest 6, section G)

Worktree `F:\CiresTeamSurvival-agents\cts-casting-rules`, ports 17560-17569.

## What shipped

### 1. Data-driven cast / heal rules (`Content/Data/CastRules.json`, `CireSkillTuning`)
Every `Abilities.json` row runs through `CireSkillTuning::ApplyCastRules` when the Ability Database loads, so
**abilities added later (feat/ability-expansion) inherit the rules automatically**. Nothing in `Abilities.json` was edited.
A row can opt out or force a class with `"castRule": "exempt" | "aoeDamage" | "directHeal" | "aoeHeal"`.
Live reload: console `cire.ReloadCastRules` (re-reads the JSON and re-derives the whole Ability Database).

Classification (authored radius, i.e. before the 1.3x aoe-scale):
| Rule | Which rows | Cast time |
|---|---|---|
| aoeDamage | `scaling.component = damage`, radius >= 150, not section `attack`, targeting aim/self/enemy, not a self aura >= 6 s, label not "per bounce/target/hit/slash/tick" | 0.5 s .. 3.5 s by **impact** |
| directHeal | `component = heal`, targeting ally, or aim with radius <= 100 | 1.5 s .. 3.5 s by **power** |
| aoeHeal | `component = heal`, radius > 100, not ally-targeted | 0 (instant) .. 1.5 s by power; **effect cut** |
| none | passives, rolls, constructs, summons, pets, self-only heals, the exempt list (gap closers: shadow_step, troll_blood_leap, behemoth_stampede, sabercat_pounce, bear_charge) | authored |

Formulas (reference PRIMARY 40, reference max health 1500; casts snap to 0.1 s):
- amount = scaling.base + scaling.primary x 40 (or effect % x 1500 for "% max health" rows); "per second / per pulse / per mote" rows x duration.
- AoE impact = amount x sqrt(radius / 300) x 1.25 if it stuns/roots/silences/interrupts. cast = 0.5 + 3.0 x ramp(impact, 60, 420).
- Direct heal cast = 1.5 + 2.0 x ramp(power, 80, 400).
- AoE heal cast = 1.5 x ramp(power, 150, 700); effect x lerp(0.65 instant, 0.85 at 1.5 s).
- Cast-time spells stand still (WoW) unless the row authors `castWhileMoving`; bots are exempt (unchanged rule).

### 2. Healing nerf (numbers)
- **All champion-ability healing x 0.85** (`healing.abilityHealingScale`), applied in `CireCombat::ApplyHealing` by ability name
  (`FCireAbilityDef::HealScale`), so hard-coded and DB heal paths, HoTs and %-max-health heals are all covered.
- **AoE heals additionally x 0.65 (instant) .. x 0.85 (1.5 s cast)** → net 0.55 .. 0.72 of the old value.
- Not nerfed: items (on-use heals), class traits (Support "Mending Strikes"), anything not in the Ability Database.
- Tooltips (`CireAbilityDB::Describe`) show the nerfed numbers.

### 3. Piercing projectiles
Champion ability skillshots (source is a hero and the name is an Ability Database ability) whose player/monster collision is
`stop` become `pierce`, hit limit 5, each extra target takes 15% less (floor 40%). Authored piercers (Piercing Shot) keep
their own tuning. Monster bolts, basic attacks and probes are unchanged. Opt-out list: `piercing.never`.

### 4. Placement ignores clipping (barriers, constructs, summons)
- `ACireConstruct::ValidatePlacementFor`: no more level-ground / corner-support / overlap (units, props, walls) refusals.
  The footprint snaps to the navmesh + ground (`ACireConstruct::SnapToGround`), a slightly long aim (<= 125% range) is pulled
  into range, a footprint over the realm edge is pulled back toward the caster. Still refused: outside the realm / far out
  of range, overlapping the castle goal zone.
- `ACireSummon::SpawnGroup`: each unit snaps onto the navmesh (so it can walk), no overlap refusal (only spacing between the new units).
- `CireSkillCasting::PlacementAim` (new, shared): used by the legacy wall/dome/guardian/pack casts and the kit / signature
  Construct and Wall deliveries. The client reticle (`CireTargeting::ValidateGround`) uses the same rule, so the aim stays green.

### 5. Ground shapes = Line / Barrier / Cone / Circle
- `ACireAreaEffect::NormalizeShape`: Square → Circle (half width); a Custom polygon that is not a rectangle → Circle of its
  authored radius (Blight Sigil's L-shaped polygon was the abnormal shape); a rectangle stays as a **Barrier** footprint.
- Applied in `ACireAreaEffect::Spawn` (the hit) and in `CireAbilityShapes` (`FromArea` + `Finish`, the telegraph), so the
  telegraph is always the true hit shape. `ShapeName(Custom)` now reads "barrier".
- Audit table below (generated from the native test's `CIRE_SHAPE_AUDIT` log lines).

## Tests
- New `CireCastRulesTests.cpp` → `CireSkillTuning::RunCastRulesSmoke` (in the native CombatExpansion probe): per-row windows,
  inheritance by synthetic new abilities, monotonic scaling, JSON validation, heal scale, the shape audit.
- `RunSkillshotSmoke`: champion skillshot pierces two units with falloff.
- `RunConstructSmoke` / `RunCastSmoke`: overlap and edge placements now succeed; long aims clamp into range; far-out aims still refuse.

## Shared-file edits (small, additive)
- `CireAbilityDB.h` (+4 derived fields), `CireAbilityDB.cpp` (ApplyCastRules hook, tooltip heal scale)
- `CireCombatEvents.cpp` (ApplyHealing x HealScaleFor)
- `CireTargeting.cpp` (placement branch in ValidateGround)
- `CireSignatureSkills.cpp`, `CireKitSkills.cpp` (Construct / Wall cases call `CireSkillCasting::PlacementAim`)
- `CireCombatExpansionProbe.cpp` (+1 line: RunCastRulesSmoke)
- `CireSkillRuntimeTests.cpp` (updated expectations)

## Assumptions / questions for Eric
- Self-only heals (Second Wind, Last Stand, Hibernate) are emergency buttons, not "direct heals": they keep their cast but get the 0.85 nerf.
- Melee `attack`-section AoEs (Cleaving Strike, Blade Flurry...) and persistent self auras/transforms (Glaive Storm, Colossus...) stay instant.
- Cast-time AoE damage spells root the caster while casting (WoW rule, same as heals); Starfall/Seismic keep their impact warning after the cast.
- All champion skillshots pierce (the pack VFX travel the full lane); if one should stop, add its id to `piercing.never`.
- Placement still refuses the castle goal zone (walls could otherwise seal the castle).

## Not done / next
- F8 panel for CastRules.json (JSON + `cire.ReloadCastRules` for now).

## Gate logs
(see bottom — filled in after the runs)
