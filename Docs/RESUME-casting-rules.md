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

## Gate logs (2026-09-28, after merging main a5e640e1) — all PASS
- Build: Result: Succeeded
- Native: PASS — `Saved/ExpansionChecks/20260928T052438980796Z/report.json` (CIRE_CAST_RULES_PASS, SKILLSHOT/CONSTRUCT/SUMMON/SKILL_CAST, ABILITY_VFX, TARGETING)
- Network: CIRE_NETWORK_SMOKE_PASS — `Saved/NetworkSmoke/20260928T052906513122Z/report.json`
- Interface: CIRE_INTERFACE_SMOKE_PASS — `Saved/InterfaceSmoke/20260928T053048050922Z/report.json`

Existing tests updated for the new rules: CrowdControl (ruled heal cast), RollSkills (heal nerf), Targeting descriptor/runtime
(normalized library shapes, clip-free placement, venom_ground cast completes), AbilityVFX (casts complete before radius checks,
normalized authored area), SkillRuntime construct/cast expectations.

## Cast-rule table (live from the native run)

| Ability | Rule | Cast (was) | Impact / power | Heal scale |
|---|---|---|---|---|
| golem_ether_anchor | aoeDamage | 0.5 s (0.0 s) | 60 | 1.00 |
| golem_granite_fist | aoeDamage | 0.6 s (0.0 s) | 69 | 1.00 |
| bear_roar | aoeDamage | 0.7 s (0.0 s) | 81 | 1.00 |
| behemoth_totem_sweep | aoeDamage | 0.7 s (0.0 s) | 88 | 1.00 |
| drakish_wing_rebuke | aoeDamage | 0.7 s (0.0 s) | 84 | 1.00 |
| venom_ground | aoeDamage | 0.8 s (0.0 s) | 101 | 1.00 |
| ashen_square | aoeDamage | 0.8 s (0.0 s) | 101 | 1.00 |
| blight_sigil | aoeDamage | 0.8 s (0.0 s) | 101 | 1.00 |
| repulsor_pulse | aoeDamage | 1.0 s (0.0 s) | 125 | 1.00 |
| dryad_root_snare | aoeDamage | 1.0 s (0.0 s) | 122 | 1.00 |
| paladin_holy_flail | aoeDamage | 1.2 s (0.0 s) | 141 | 1.00 |
| grave_line | aoeDamage | 1.3 s (0.0 s) | 157 | 1.00 |
| powder_flask | aoeDamage | 1.3 s (0.0 s) | 160 | 1.00 |
| cinder_cone | aoeDamage | 1.6 s (0.0 s) | 198 | 1.00 |
| arcane_blunderbuss | aoeDamage | 2.4 s (0.0 s) | 285 | 1.00 |
| cataclysm | aoeDamage | 2.8 s (0.0 s) | 333 | 1.00 |
| starfall | aoeDamage | 3.1 s (0.0 s) | 377 | 1.00 |
| seismic_reprisal | aoeDamage | 3.5 s (0.0 s) | 416 | 1.00 |
| hexbane_judgment | aoeDamage | 3.5 s (0.0 s) | 478 | 1.00 |
| miner_mountain | aoeDamage | 3.5 s (0.0 s) | 484 | 1.00 |
| sanctuary | aoeHeal | 0.0 s (2.0 s) | 132 | 0.55 |
| renewal | aoeHeal | 0.5 s (2.5 s) | 320 | 0.61 |
| bastion_of_dawn | aoeHeal | 0.8 s (1.0 s) | 450 | 0.64 |
| golem_moss_bloom | aoeHeal | 1.0 s (1.5 s) | 522 | 0.67 |
| dryad_grove_renewal | aoeHeal | 1.0 s (2.0 s) | 520 | 0.67 |
| whisp_fey_trail | aoeHeal | 1.1 s (0.8 s) | 552 | 0.68 |
| centaur_spring_march | aoeHeal | 1.1 s (1.0 s) | 552 | 0.68 |
| whisp_constellation | aoeHeal | 1.5 s (1.5 s) | 816 | 0.72 |
| keeper_sunrise | aoeHeal | 1.5 s (2.0 s) | 804 | 0.72 |
| purify | directHeal | 1.5 s (1.0 s) | 88 | 0.85 |
| paladin_pilgrim_light | directHeal | 1.7 s (1.0 s) | 114 | 0.85 |
| restoring_light | directHeal | 2.0 s (1.5 s) | 162 | 0.85 |
| aether_mend | directHeal | 2.0 s (1.0 s) | 157 | 0.85 |
| whisp_guiding_mote | directHeal | 2.2 s (1.0 s) | 192 | 0.85 |
| dryad_seed_mend | directHeal | 2.5 s (1.0 s) | 236 | 0.85 |
| keeper_dawn_beam | directHeal | 2.9 s (1.2 s) | 298 | 0.85 |
| whisp_spirit_tether | directHeal | 3.2 s (0.5 s) | 348 | 0.85 |
| wellspring | directHeal | 3.4 s (2.0 s) | 380 | 0.85 |

## Shape audit (telegraph = hit shape; every row passed the native parity checks)

| Ability | Shape | Radius | Length | Width | Angle | Projectile | Ground aim |
|---|---|---|---|---|---|---|---|
| iron_guard | self | 0 | 0 | 0 | 0 |  |  |
| shield_slam | unit | 0 | 0 | 0 | 0 |  |  |
| war_cry | circle | 1105 | 0 | 0 | 0 |  |  |
| chain_spark | chain | 650 | 0 | 0 | 0 |  |  |
| frost_bind | line | 0 | 1200 | 88 | 0 | yes | yes |
| cleaving_strike | circle | 416 | 0 | 0 | 0 |  |  |
| shadow_step | unit | 0 | 0 | 0 | 0 |  |  |
| restoring_light | unit | 0 | 0 | 0 | 0 |  |  |
| sanctuary | circle | 780 | 0 | 0 | 0 |  |  |
| purify | unit | 0 | 0 | 0 | 0 |  |  |
| summoned_wall | barrier | 0 | 0 | 0 | 0 |  | yes |
| protection_dome | barrier | 0 | 0 | 0 | 0 |  | yes |
| oathbound_guardian | circle | 45 | 0 | 0 | 0 |  | yes |
| spectral_pack | circle | 230 | 0 | 0 | 0 |  | yes |
| polymorph | none | 0 | 0 | 0 | 0 |  |  |
| second_wind | self | 0 | 0 | 0 | 0 |  |  |
| decimating_strike | none | 0 | 0 | 0 | 0 |  |  |
| bastion_of_dawn | circle | 845 | 0 | 0 | 0 |  |  |
| cataclysm | circle | 715 | 0 | 0 | 0 |  |  |
| executioners_verdict | unit | 0 | 0 | 0 | 0 |  |  |
| renewal | circle | 1300 | 0 | 0 | 0 |  |  |
| last_stand | self | 0 | 0 | 0 | 0 |  |  |
| challenge_of_iron | circle | 1105 | 0 | 0 | 0 |  |  |
| seismic_reprisal | circle | 585 | 0 | 0 | 0 |  |  |
| starfall | circle | 650 | 0 | 0 | 0 |  | yes |
| spectral_hunt | unit | 0 | 0 | 0 | 0 |  |  |
| mass_aegis | circle | 1170 | 0 | 0 | 0 |  |  |
| wellspring | unit | 0 | 0 | 0 | 0 |  |  |
| venom_ground | circle | 364 | 650 | 286 | 75 |  | yes |
| cinder_cone | cone | 780 | 650 | 286 | 70 |  | yes |
| grave_line | line | 364 | 900 | 208 | 75 |  | yes |
| ashen_square | circle | 364 | 650 | 650 | 75 |  | yes |
| blight_sigil | circle | 364 | 650 | 286 | 75 |  | yes |
| piercing_shot | line | 0 | 1500 | 47 | 0 | yes | yes |
| ember_lance | line | 0 | 1200 | 78 | 0 | yes | yes |
| shield_bash | none | 0 | 0 | 0 | 0 |  |  |
| shield_toss | none | 0 | 0 | 0 | 0 |  |  |
| shield_wall | none | 0 | 0 | 0 | 0 |  |  |
| pavise | none | 0 | 0 | 0 | 0 |  |  |
| mechanical_tank | none | 0 | 0 | 0 | 0 |  |  |
| artillery | none | 0 | 0 | 0 | 0 |  |  |
| eagle_eye | none | 0 | 0 | 0 | 0 |  |  |
| longshot | none | 0 | 0 | 0 | 0 |  |  |
| silver_shot | line | 0 | 1300 | 73 | 0 | yes | yes |
| hex_mark | unit | 0 | 0 | 0 | 0 |  |  |
| powder_flask | circle | 338 | 0 | 0 | 0 |  | yes |
| blade_flurry | circle | 546 | 0 | 0 | 0 |  |  |
| hunters_stride | circle | 70 | 0 | 0 | 0 |  | yes |
| warding_talisman | self | 0 | 0 | 0 | 0 |  |  |
| collect_the_bounty | unit | 0 | 0 | 0 | 0 |  |  |
| arcane_blunderbuss | cone | 715 | 0 | 0 | 60 |  | yes |
| spirit_lantern | circle | 338 | 0 | 0 | 0 |  | yes |
| purge | unit | 0 | 0 | 0 | 0 |  |  |
| banishment | unit | 0 | 0 | 0 | 0 |  |  |
| witchfinders_mark | unit | 0 | 0 | 0 | 0 |  |  |
| spectral_blade | unit | 0 | 0 | 0 | 0 |  |  |
| hexbane_judgment | circle | 585 | 0 | 0 | 0 |  | yes |
| bouncing_glaive | chain | 650 | 0 | 0 | 0 |  |  |
| sabercat_pounce | unit | 0 | 0 | 0 | 0 |  |  |
| owl_scout | circle | 585 | 0 | 0 | 0 |  | yes |
| moonlit_sprint | self | 0 | 0 | 0 | 0 |  |  |
| crescent_volley | line | 0 | 1400 | 104 | 0 | yes | yes |
| sabercat_maul | unit | 0 | 0 | 0 | 0 |  |  |
| sabercat_roar | self | 0 | 0 | 0 | 0 |  |  |
| glaive_storm | circle | 624 | 0 | 0 | 0 |  |  |
| photon_turret | circle | 950 | 0 | 0 | 0 |  | yes |
| skitter_swarm | circle | 180 | 0 | 0 | 0 |  | yes |
| arc_mine | circle | 312 | 0 | 0 | 0 |  | yes |
| disruption_pylon | circle | 585 | 0 | 0 | 0 |  | yes |
| phase_lance | line | 0 | 1300 | 78 | 0 | yes | yes |
| overcharge | self | 0 | 0 | 0 | 0 |  |  |
| warp_obelisk | circle | 1300 | 0 | 0 | 0 |  | yes |
| aegis_pylon | circle | 585 | 0 | 0 | 0 |  | yes |
| haste_pylon | circle | 585 | 0 | 0 | 0 |  | yes |
| gravity_pylon | circle | 585 | 0 | 0 | 0 |  | yes |
| stasis_snare | circle | 195 | 0 | 0 | 0 |  | yes |
| aether_mend | unit | 0 | 0 | 0 | 0 |  |  |
| repulsor_pulse | circle | 455 | 0 | 0 | 0 |  |  |
| aether_nexus | circle | 845 | 0 | 0 | 0 |  | yes |
| bear_maul | unit | 0 | 0 | 0 | 0 |  |  |
| bear_roar | cone | 715 | 0 | 0 | 90 |  | yes |
| bear_charge | line | 0 | 750 | 234 | 0 |  | yes |
| bear_hibernate | self | 0 | 0 | 0 | 0 |  |  |
| bear_colossus | circle | 780 | 0 | 0 | 0 |  |  |
| paladin_righteous_flail | cone | 546 | 0 | 0 | 100 |  | yes |
| paladin_relic_vow | unit | 0 | 0 | 0 | 0 |  |  |
| paladin_holy_flail | unit | 0 | 0 | 0 | 0 |  |  |
| paladin_pilgrim_light | line | 0 | 1100 | 156 | 0 |  | yes |
| miner_pickfall | unit | 0 | 0 | 0 | 0 |  |  |
| miner_faultline | line | 0 | 900 | 286 | 0 |  | yes |
| miner_lantern | circle | 585 | 0 | 0 | 0 |  | yes |
| miner_mountain | circle | 650 | 0 | 0 | 0 |  | yes |
| golem_granite_fist | cone | 585 | 0 | 0 | 70 |  | yes |
| golem_ether_anchor | circle | 325 | 0 | 0 | 0 |  | yes |
| golem_worldstone | circle | 780 | 0 | 0 | 0 |  |  |
| golem_moss_bloom | circle | 455 | 0 | 0 | 0 |  | yes |
| golem_living_granite | unit | 0 | 0 | 0 | 0 |  |  |
| golem_fel_fist | cone | 520 | 0 | 0 | 60 |  | yes |
| golem_ether_furnace | self | 0 | 0 | 0 | 0 |  |  |
| chieftain_axe_hook | line | 0 | 900 | 130 | 0 | yes | yes |
| chieftain_banner | circle | 650 | 0 | 0 | 0 |  | yes |
| chieftain_earthshout | circle | 910 | 0 | 0 | 0 |  |  |
| behemoth_totem_sweep | cone | 585 | 0 | 0 | 120 |  | yes |
| behemoth_tusk_line | line | 0 | 800 | 260 | 0 |  | yes |
| behemoth_totem_bulwark | barrier | 0 | 0 | 0 | 0 |  | yes |
| behemoth_stampede | line | 0 | 1200 | 520 | 0 |  | yes |
| drakish_dragon_oath | self | 0 | 0 | 0 | 0 |  |  |
| drakish_scale_guard | self | 0 | 0 | 0 | 0 |  |  |
| drakish_wing_rebuke | cone | 520 | 0 | 0 | 120 |  | yes |
| drakish_ancient_pact | circle | 650 | 0 | 0 | 0 |  |  |
| troll_axe_frenzy | unit | 0 | 0 | 0 | 0 |  |  |
| troll_blood_leap | circle | 390 | 0 | 0 | 0 |  | yes |
| troll_red_moon | self | 0 | 0 | 0 | 0 |  |  |
| troll_twin_throw | line | 0 | 1300 | 214 | 0 | yes | yes |
| troll_returning_axes | line | 0 | 1100 | 182 | 0 |  | yes |
| dryad_root_snare | circle | 325 | 0 | 0 | 0 |  | yes |
| dryad_seed_mend | unit | 0 | 0 | 0 | 0 |  |  |
| dryad_thorn_line | line | 0 | 900 | 234 | 0 |  | yes |
| dryad_grove_renewal | circle | 780 | 0 | 0 | 0 |  | yes |
| whisp_guiding_mote | line | 0 | 1200 | 156 | 0 |  | yes |
| whisp_spirit_tether | unit | 0 | 0 | 0 | 0 |  |  |
| whisp_fey_trail | line | 0 | 800 | 312 | 0 |  | yes |
| whisp_constellation | circle | 1170 | 0 | 0 | 0 |  |  |
| centaur_grove_javelin | line | 0 | 1300 | 78 | 0 | yes | yes |
| centaur_trailblaze | line | 0 | 900 | 364 | 0 |  | yes |
| centaur_herd_call | circle | 60 | 0 | 0 | 0 |  | yes |
| centaur_spring_march | circle | 780 | 0 | 0 | 0 |  |  |
| keeper_dawn_beam | line | 0 | 1000 | 260 | 0 |  | yes |
| keeper_lantern_ward | circle | 585 | 0 | 0 | 0 |  | yes |
| keeper_beacon | circle | 520 | 0 | 0 | 0 |  | yes |
| keeper_sunrise | circle | 1170 | 0 | 0 | 0 |  |  |
| tumble_strike | none | 0 | 0 | 0 | 0 |  |  |
| mine_layer | none | 0 | 0 | 0 | 0 |  |  |
| taunting_tumble | none | 0 | 0 | 0 | 0 |  |  |
| shield_tumble | none | 0 | 0 | 0 | 0 |  |  |
| venom_tumble | none | 0 | 0 | 0 | 0 |  |  |
| shadow_dance | none | 0 | 0 | 0 | 0 |  |  |
| evasive_stance | none | 0 | 0 | 0 | 0 |  |  |

Shapes in the pool: circle / cone / line / barrier (rectangle construct footprints) plus the non-ground unit / self / chain / none deliveries. No square or free polygon remains (Blight Sigil's L-shaped polygon and the old 'Ashen Square' now hit and draw as circles).
