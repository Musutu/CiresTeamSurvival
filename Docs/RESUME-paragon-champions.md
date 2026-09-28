# RESUME: feat/paragon-champions

Worktree `F:\CiresTeamSurvival-agents\cts-paragon-champions`, ports 17600-17609. Spec: PLAYTEST-6 section K. Design: [ParagonChampions.md](ParagonChampions.md).

## Done
- `Tools/InspectParagon.py`: a UE commandlet that inventories all 39 packs (meshes, skins, anim BPs, anims with skeleton and length, montages with slots, Cascade/Niagara per ability folder, sound cues). Output: `Saved/ParagonInspect.json`.
- `Tools/AuthorParagonChampions.py`: the kit table for all 38 heroes (roles, primary stat, style/range, RMB/Q/E/R own abilities with clips + FX groups, passive). It generates `Content/Data/ParagonChampions.json` and the table in the doc.
- `CireParagonChampions.*`:
  - registration: roster append, Ability DB and kit merge, pool additions
  - 16 delivery recipes, 5 buffs, 5 passive kinds
  - Paragon FX playback, portraits, voice, draft background
- `CireParagonChampionsTests.cpp`: `RunSmoke` in the native expansion probe.
- `CireParagonGallery.*` + `Tools/RunParagonGallery.py`: a close-up and one shot per ability, for every hero.
- `Tools/RunParagonPortraits.py`: local portraits in `/Game/ParagonDerived/Portraits`.

- **Skins (Eric's 2026-09-28 direction):** 158 skin meshes classified. The table is in ParagonChampions.md.
  - **93 reskins:** selectable in champion select (arrows or `[` / `]`). The pick replicates as `ACireHero::ChampionSkin` via `ServerSetChampionSkin`, and the binding row is `<profile>@<skin>`.
  - **24 new champions:** own id, name, role, primary stat and passive. Their kits reuse the parent's clips and FX, recoloured by school.
  - **30 monster variants:** 17 skins plus 13 ParagonMinions bodies, merged into race units by `CireMonsterArt`.
  - **11 not usable:** parts, or a skeleton with no clips.

## Not done / next
- **Monster variants are opt-in (`-CireParagonMonsters`).** With the flag on, 30 bodies fail the monster-body checks (stride speeds 0, archetype props on Paragon weapons, knockback hit clips not upright, Khaimera head bone). Next: measure walk/run speeds, set `dropPropBones`, pick calmer hit clips, fix the Khaimera head, then turn them on.
- Run the gates, the gallery and the portraits, then review the captures (see the status below).
- Painted icons for the ~190 Paragon abilities (ChatGPT). Until then the procedural sigil fallback is used.
- Dedicated draft backgrounds. Until then each hero uses its closest existing painting.
- Skins are inventoried but can't be selected yet.
- ParagonMinions as monster variants (inventoried in `creeps`).

## Assumptions / questions for Eric
- Profile ids are `pg_<hero>` and ability ids are `pg_<hero>_<rmb|q|e|r|passive>`. The heroes are listed after the authored roster.
- The game drives the heroes' clips through its native layer, not through the Paragon anim blueprints. This keeps the server-synced cast frames, leg IK and reactions. See the doc.
- Only the own **actives** (RMB/Q/E) join the other champions' Skill Shop pools, by type. Ultimates and passives stay signature-only. With 38 heroes that is about 114 extra pool actives. If the shop feels diluted, trim `poolAdditions`.
- Heights: each hero is scaled toward 180 cm, keeping half of its size difference (clamped to 165–250 cm).
- Paragon packs ship voice lines only (no ability SFX). Ability sounds come from the game's school sounds; the ultimate plays the hero's own voice line.

## Shared-file edits (all small and additive, marked `// paragon-champions`)
- `CireSignatureSkills.cpp`: routes Knows/Handles/IsPassive/Cast/DescribeShape/damage/OnAbilityHit/speed hooks to CireParagonChampions.
- `CireChampionRoster.cpp`: `Reload` appends the Paragon profiles.
- `CireAbilityDB.cpp`: `Reload` merges the overlay.
- `CireChampionArt.cpp`: `FabProfileArt` also reads the `ParagonChampions.json` bindings.
- `CireSpellPresentation.cpp`: `Play` calls `PlayFX`.
- `CireRosterHUD.cpp`: portrait fallback and draft background fallback.
- `CireMatch.cpp`: gallery `Initialize`/`Tick`.
- `CireCombatExpansionProbe.cpp`: `RunSmoke`.
- `CireGame.h`: `ACireHero::ChampionSkin` (replicated) and `ACireController::ServerSetChampionSkin`.
- `CireHero.cpp`: `DOREPLIFETIME(ChampionSkin)`.
- `CireDraftStage.*`: `SetPreviewSkin`.
- `CireRosterHUD.cpp`: the skin selector above LOCK IN.
- `CireChampionArt.cpp`: `ArtFor` uses the `<profile>@<skin>` row; the art attempt key includes the skin.
- `CireMonsterArt.cpp`: reads `Content/Data/RaceMeshes.paragon.json` (generated) and merges it as extra variants.
- `CireSoundEvents.cpp`: reads ParagonChampions.json `audio` rows.
- `CireWeaponPresentation.cpp` and `CireChampionActions.cpp`: skip Paragon profiles (their weapons are in the mesh; they use their own clips).
- `Content/Data/BuffVisuals.json` and `BuffModifiers.json`: five `pg_*` rows.
- `Content/UI/Draft/Portraits/Exposure.json`: RunDraftPortraits merges exposure trims for the `pg_*` ids.

## Gate logs (2026-09-28, branch up to date with main 25ad73dc)
- **Native, packs installed:** PASS (`CIRE_PARAGON_SMOKE_PASS checks=591`: 38 registered, 5-hero body/clip/cast sample). Log: `Saved/ExpansionChecks/20260928T050918765882Z/native.log`.
- **Native, clean clone (junctions removed):** PASS (`installed=0`, nothing registers). Log: `Saved/ExpansionChecks/20260928T052449864900Z/native.log`.
- **Network:** PASS. Log: `Saved/NetworkSmoke/20260928T052038615392Z/report.json`.
- **Interface:** PASS. Log: `Saved/InterfaceSmoke/20260928T052206257886Z/report.json`.

The network and interface smokes were run with the Paragon junctions temporarily removed (clean-clone mode), then the junctions were restored. **Environmental finding:** with the 39 packs junctioned in, a cold `-game` start spends over 60 s gathering the asset registry (about 83 GB of headers). The network probe's client then times out at the 90 s cap (twice). This is not caused by the code: without the junctions, both smokes pass. The same slowdown will hit any worktree or main that has `Content/Paragon*` mounted. Coordinator options: raise the probe cap, or keep the packs out of the smoke worktrees.

For the full 38-hero cast/clip sweep, run the probe with `-CireParagonFullSmoke`, or run `Tools/RunParagonGallery.py`.
