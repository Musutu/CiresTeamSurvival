# RESUME — feat/kit-editor (Skill Assignment editor)

Spec: Docs/EricFeedback/2026-09-27/PLAYTEST-6.md section I. Wave rules: F:\CiresTeamSurvival-agents\WAVE-2026-09-27.md. Ports 17580-17589.
Design + data format: Docs/KitEditor.md.

## Done
- CireKitEditor.h/.cpp: template data (Content/Data/ChampionKitTemplates.json, transactional parse, clamped values, pretty save),
  kit rules (6 actives / 1 ult / 1 passive, Normalize, AddBlocker), game integration (MergeIntoKits, GrantOnDraft, SpawnPlacedCast),
  anchor/bone resolution across skeleton naming schemes, placement spawn/apply, data-driven pool browser (sections, search, filters).
- CireKitEditorUI.cpp: full-screen editor from Champion Select > KIT EDITOR (champion strip, periodic-table pool with search/filters,
  live 3D preview on the draft stage with looping placed effect + CAST PREVIEW + drag-rotate, kit slots, start-with-kit toggle,
  save/revert/clear with unsaved-change guard, placement controls). Gallery capture: -CireKitEditorGallery[=<champion>].
- CireKitEditorTests.cpp: native suite, hooked into the combat expansion probe.
- Docs/KitEditor.md.

## Not done / next
- (see gate status below)

## Assumptions / questions for Eric
- "Base kit" = the skills the champion STARTS the match with (grantOnDraft default ON, toggle per champion). This overrides the
  "1 skill point / opening role pick" rule for templated champions only; turn the toggle off to only add the kit to the Skill Shop list.
- Effect placement applies to the caster-attached CAST effect (Fab cast role: caster flare / self shock / channel). Projectiles,
  impacts and ground zones keep their normal placement (their position is the gameplay hit point). Ask Eric if he wants per-champion
  projectile muzzle sockets too.
- Skills whose requirement the body fails (shield / ranged-only) are shown with a red "!" and not granted.
- The editor is on in all dev/editor builds; shipping needs -CireKitEditor.
- While the host edits, the pick timer is held (standalone / listen server). Clients see a warning instead.

## Shared-file edits (small, additive)
- CireAbilityDB.cpp: include + `CireKitEditor::MergeIntoKits(GKits)` at the end of Reload.
- CireChampionProfiles.cpp: include + `CireKitEditor::GrantOnDraft(this)` in DraftProfile before RefreshOffer.
- CireSpellPresentation.cpp: include + UpdateFabVFX tries `CireKitEditor::SpawnPlacedCast` first for the cast role (else the old spawn + entry tint).
- CireCombatExpansionProbe.cpp: include + `CireKitEditor::RunTests(Mode)`.
- CireDraftStage.h: `GetPreviewHero()` accessor.
- CireRosterHUD.cpp: include, KIT EDITOR nav item, early-out that draws the editor while open.

## Gate logs
- (pending)
