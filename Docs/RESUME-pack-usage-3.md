# RESUME: pack-usage pass #3 (feat/pack-usage-3)

Worktree `F:\CiresTeamSurvival-agents\cts-pack-usage-3`, branch `feat/pack-usage-3` from main 25ad73dc. Ports 17500-17509.
Brief: Playtest 6 section A (Docs/EricFeedback/2026-09-27/PLAYTEST-6.md) + the pack-usage-2 "Not done" / "Next queue".

## Done

- **Ratings (committed first, 104b4612, for feat/ability-expansion):** `Art/Fab/FabVFXInventory.json` now carries quality
  A/B/C + a one-line `look` for all 523 catalogued systems (sheets: `cts-pack-usage-2/Saved/FabVFXCatalog/20260927T042848Z/sheets`).
  Convention: a system that draws nothing on the static stage at both frames (projectiles, dashes, beams, splines) is left
  unrated with tag `stage-blank` (not a C); circular teal / green swirls carry tag `swirl`. `InventoryFabVFX.py --ratings FILE
  --catalog DIR` folds a review in. Counts: A 105, B 176, C 105, unrated 152 (137 stage-blank + 15 `_Base` systems not catalogued).
- **C picks demoted:** `FabMonsterVFXTable.DEMOTED` (C stem -> A/B replacement of the same theme, race / CC tint still wins),
  applied to monster pools / overrides and, in `MapFabVFX.entry()`, to champion / hit / kill tables; school and buff lists in
  `MapFabVFX.py` edited directly. Projectiles, orbs and muzzles stay (small by design). The three Forest explosions that
  render as a red / black missing-material blob (Explosion_Nature, Explosion_Cast_Nature, Explosion_Grenade_Nature) are gone.
- **The green-blue on-hit swirl:** it was `NS_Air_Magic_Hit3` (green crescent swirl) on `hit.armor` = every steel hit on any
  plate / mail target, plus `NS_Shadow_Magic_Hit2` (teal ribbon swirl) first in the Hollow hit pool under the grave-green tint.
  Armour hits are now warm steel sparks that differ per weapon family (blades Light_Magic_Hit1, axes / claws
  Lightning_Magic_Projectile2, blunt State_VFX_Shock1, piercing Ice_Magic_Hit steel-tinted, guns Light_Magic_Hit2, default
  Light_Magic_Hit3; crits Lightning_Magic_Blink2 / blunt Fire_Magic_Explosion). The swirls stay only where they suit:
  whisps (`hit.none`), the wind arrow `piercing_shot`, `bear_roar`, `polymorph`, `shadow_step` and two Voidborn abilities.
  Native test: no swirl on hit.flesh* / hit.armor*, >= 4 distinct armour hit systems.
- **Sizes:** `VFXTuning.json` spellEffectScale 1.3 -> 1.17, auraLayerScale 1.1 -> .99, handGlowScale 1.2 -> 1.08 (-10%);
  new `hitEffectScale` .89 multiplies every impact / critical visual (procedural + Fab) on top: .89 x 1.17 = .8 x the old 1.3
  (-20%). Code: `CireAbilityVFX::DesignHitEffectScale`, `CireSpellPresentation.cpp` (FxScale on Impact / Critical cues). Tests updated.
- **Unused systems wired (quality first):** wrapped race pools take the unused A/B systems of their theme before any recolour
  repeat (`POOL_ADDITIONS`), plus `PACK_USAGE_3_OVERRIDES` for systems with one obvious owner (frostfang / lich ice set,
  aether thunder storm + P_ky lightning, void dark storm / crystal wall / shield break, stoneborn pillar ring, ...);
  kill.boss = BloodBurst_Extreme; pistol hits BulletHit_Sample.
- **Arena portals:** ring, base pool (60%), open and swallow bursts now take the real `CireFabVFX::Recolor` to the arena's
  hue (default on; `"ring": {"tint": false}` in Arenas.json opts out). ApplyTint remains only as the no-colour-parameter fallback.

## Not done / next

- Blood decals (Splatter_*, SphericalDecalSplatter_*) and drips stay
  unused: one system per kill key, and decals need a surface-projection spawn path (code work, not data).
- Remaining unused Big Pack / Shadow / Earth systems are almost all `stage-blank` projectiles / lines (need a live motion
  review before rating) or C-rated.

## Galleries (2026-09-28, all captures PASS; raw PNG frames deleted to save disk, sheets + comparisons kept)

- After: `Saved/AbilityVFX/after-packusage3-{champion,voidborn,stoneborn,hollow}-*/sheets`.
- Before/after (before = `cts-pack-usage-2/Saved/AbilityVFX/before-packusage2-*`): champion `compare-20260928T055052Z`,
  voidborn `compare-20260928T062412Z`, stoneborn `compare-20260928T062547Z`.
- Eye-check: hits read smaller and none of the champion basic attacks shows the green swirl any more (basic_sword's cyan rune
  ring in the gallery is the lingering mass_aegis / wellspring zone from the previous casts, not a hit). New Voidborn dark
  storm (gravity well) and crystal-wall picks read well. Tinted kits: the Hollow grave-green tint is subtle on Shadow Magic
  (dark, low-saturation systems keep most of their ink look), which is fine for undead; Stoneborn cyan rune variants read
  as intended. No broken / missing-material systems seen.

## Assumptions / questions for Eric

- "Hit effects -20%" = every impact / critical visual ends at 80% of its size before this pass (not 80% on top of the
  general -10%). Tunable live: `hitEffectScale` in `Content/Data/VFXTuning.json`.
- "Skill effects -10% across the board" includes aura layers and hand glows (all three design scales x .9); aoeRadiusScale
  (gameplay) untouched.
- Stage-blank systems are unrated rather than C (they need motion or a target to show anything).
- CC colours kept as defaults (root green, silence magenta, stun gold, slow ice-blue, charm teal, blind navy).

## Shared-file edits

- `Content/Data/VFXTuning.json`: three values + new `hitEffectScale` + note.
- `Source/CiresTeamSurvival/CireArenaPortal.cpp/.h`: portal layers use Recolor (Layer lambda gains a strength arg; bTintRing
  defaults true; open / swallow bursts recoloured).
- `Source/CiresTeamSurvival/CireAbilityVFXTests.cpp`: expected design scales.

## Commands

```
Data:    python Tools/InventoryFabVFX.py --ratings <review.txt> --catalog <catalogue dir> ; python Tools/MapFabVFX.py
Audit:   python Tools/AuditPackUsage.py --before main
Gallery: python Tools/RunAbilityVFXGallery.py --set champion --tag after-packusage3 ; --compare BEFORE AFTER
(python = F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe)
```

## Gate logs

- Space freed by the coordinator; main a5e640e1 merged in (52498b62). Build: Succeeded.
- native: PASS `Saved/ExpansionChecks/20260928T052554538942Z/report.json` (the vfx-scale test now expects EffectScale
  1.3 x hitEffectScale on hits; small splashes grow 20-35% because of a fixed rim).
- network: PASS `Saved/NetworkSmoke/20260928T053050130657Z/report.json`
- interface: PASS `Saved/InterfaceSmoke/20260928T053225230633Z/report.json`
