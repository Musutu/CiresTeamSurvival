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

## Not done / next
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
- `Content/Data/BuffVisuals.json` and `BuffModifiers.json`: five `pg_*` rows.
- `Content/UI/Draft/Portraits/Exposure.json`: RunDraftPortraits merges exposure trims for the `pg_*` ids.

## Gate logs
(filled in when run)
