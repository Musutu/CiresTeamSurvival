# RESUME: feat/ability-expansion (Playtest 6, section J)

Worktree `F:\CiresTeamSurvival-agents\cts-ability-expansion`, ports 17590-17599.

## What this branch adds
- **111 new abilities** (`Content/Data/AbilitiesExpansion.json`, written by `Tools/BuildAbilityExpansion.py`):
  61 pack-signature actives, 9 heal / defence / buff actives, 8 summons, 9 constructs, 14 passives, 12 ultimates.
  Merged additively into the Ability Database by `CireAbilityDB::Reload` (new rows only; each listed champion's purchasable
  list grows; buff-modifier rows join). Each champion gains 15-31 skills (cap 30-31 so the Skill Shop stays readable).
- **Gameplay**: `Source/CiresTeamSurvival/CireAbilityExpansion.{h,cpp}`: 20 generic, data-driven delivery recipes
  (bolt, pierce, line, cone, circle, zone, nova, chain, strike, leap, dash, heal, healZone, barrier, selfBuff, partyBuff,
  summon, construct, wall, barrage) plus passive hooks (executioner, opportunist, firstStrike, thickSkin, lastBastion,
  bond, echo, bleedEdge, vampiric, fleet, trance, manaFont, soulHarvest, frostbite). Riders: root, taunt, weaken, mark
  (exposed), knockback, pull, bleed/burn DoT, lifesteal. Routed from `CireSignatureSkills` like `CireKitSkills`.
- **Every ability** has role types, a Skill Shop section + group, PRIMARY-stat scaling (damage / heal / barrier / summon and
  construct hits: base + coef x Primary; buffs and passives: potency), a level-15 bonus (actives) or team aura (passives),
  an ultimate upgrade (ultimates), a true telegraph (`DescribeShape`), a sound row, an icon row and (actives) a Fab signature.
- **Pack VFX**: every previously unused A/B-rated (pack-usage-3 rating, commit 104b4612) or unrated moving (projectile /
  line) Niagara system from the purchased packs is the signature of one active (61 systems). Secondary roles use A/B,
  non-swirl systems of the same school, recoloured to the school hue when reused. `Content/Data/FabVFX.expansion.json`.
- **Icons**: procedural path (`Tools/BuildAbilityIcons.py` now also paints `AbilityIcons.expansion.json` rows; every
  expansion icon has a distinct glyph+palette). PNGs in `Saved/AbilityIcons/expansion`; import into `/Game/UI/Abilities/T_<id>`.
- **Buffs**: `xp_empowered`, `xp_fortified`, `xp_hastened`, `xp_exposed`, `xp_bleeding` (visuals in
  `Content/Data/BuffVisuals.expansion.json`, modifier rows in `AbilitiesExpansion.json`). Reused: `npc_rooted`, `bear_cowed`, `taunted`.
- **Summon bodies**: `Content/Data/SummonArt.expansion.json` reuses the three HQ summon bodies (guardian / spectral / mech).
- **Tests**: `CireAbilityExpansion::RunSmoke` (in `CireAbilityExpansionTests.cpp`, run by the native gate) + an expansion
  merge check in `CireAbilityDB::RunSmoke`.

## Regenerate
```
F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/BuildAbilityExpansion.py --inventory <rated FabVFXInventory.json>
```
(default inventory = `Art/Fab/FabVFXInventory.json`, which carries the ratings once feat/pack-usage-3 is merged.)
Icons: `python Tools/BuildAbilityIcons.py --pylib F:/CiresTeamSurvival-agents/pylib --ids <expansion ids> --out Saved/AbilityIcons/expansion`,
then import with `Tools/ImportDraftPortraits.py` (env `CIRE_DRAFT_PORTRAIT_DIR`, `CIRE_UI_TEXTURE_DEST=/Game/UI/Abilities`, `CIRE_UI_TEXTURE_PREFIX=T_`).
Pillow lives in `F:/CiresTeamSurvival-agents/pylib` (installed for this branch).

## Status
- Done: data builder, module, routing, loaders, tests, docs appendix (Docs/Abilities.md "Expansion pool"), icons painted.
- Icons imported to `/Game/UI/Abilities/T_<id>` (111, committed via LFS).
- Next (after feat/casting-rules merges): switch the expansion Construct / Wall / Summon placement from its own
  ground + sight check to `CireSkillCasting::PlacementAim` (clip-free placement, section G) - a 3-line change in
  `CireAbilityExpansion::Cast` (`NeedGround`). Optional polish: ChatGPT-painted icons for the 12 ultimates.

## Assumptions / questions for Eric
- Cast times: no expansion row authors a cast time or `castWhileMoving`. feat/casting-rules applies its data rules
  (`CastRules.json`, `CireSkillTuning::ApplyCastRules`) inside `CireAbilityDB::ParseJson`, which also parses
  `AbilitiesExpansion.json`, so every expansion row inherits AoE-damage / direct-heal / AoE-heal casts and the heal nerf.
- C-rated systems are skipped (coordinator ruling). 14 unused systems that render nothing on the catalogue stage and are
  not projectiles / lines (Earth_Magic_Spike1/2/5, Earth/Ice/Blood_Magic_Target, Ice_Magic_Snowstorm2, Blood_Magic_Crystal3/6,
  Posion_Magic_Spike1/2, the two blank blood decals) are left unused: they likely need a target / spline parameter.
- Blood (RealisticBlood gore) systems sign the physical "bleed" family (Hemorrhage, Lacerate, Arterial Slash, Flay...).
- Summons reuse the three HQ summon bodies until bespoke bodies exist.
- Each champion's pool is capped at ~30 expansion skills, chosen by role and by the schools its own kit already uses.

## Shared-file edits (small, additive; for the merge)
- `CireSignatureSkills.cpp`: route Knows / Handles / IsPassive / Cast / DescribeShape and the damage / hit / kill / speed hooks.
- `CireAbilityDB.cpp`: merge `AbilitiesExpansion.json` in `Reload`; merge check in `RunSmoke`.
- `CireTechConstructs.cpp`: `AppendConstructRecipes` in `BuildRecipes`.
- `CireHero.cpp`: bot `BotWantsCast` line.
- `CireBuffs.cpp`: `KnownIds` includes the expansion buff ids.
- `CireCombatExpansionProbe.cpp`: runs `CireAbilityExpansion::RunSmoke`.
- `CireFabVFX.cpp`, `CireSoundEvents.cpp`, `CireAbilityIcons.cpp`, `CireChampionArt.cpp`, `CireAuraVisuals.cpp`, `CireEffects.cpp`:
  each also reads its `*.expansion.json` sibling (base file wins on a clash).
- `Tools/BuildAbilityIcons.py`: paints the expansion icon rows.
- `Docs/Abilities.md`: AUTO:expansion appendix.

## Gate logs (after Paragon was unlinked, 2026-09-28)
- Build: Succeeded after merging main (a5e640e1).
- Network smoke: PASS - `Saved/NetworkSmoke/20260928T051510227981Z/report.json`.
- Interface smoke: PASS - `Saved/InterfaceSmoke/20260928T051646704002Z/report.json`.
- Native: PASS (`CIRE_ABILITY_EXPANSION_PASS checks=2364`, `CIRE_COMBAT_EXPANSION_PASS`) - `Saved/ExpansionChecks/20260928T052753560677Z/report.json`
  (passed=true, `--timeout 600`). With `--timeout 240` all probes pass (~3 min under load) but the editor's shutdown
  asset-registry scan (Polyphoria) pushes the exit past 240 s, so the runner reports a timeout (`20260928T052347934998Z`).
- PlacementAim follow-up: deferred until feat/casting-rules merges (coordinator).
