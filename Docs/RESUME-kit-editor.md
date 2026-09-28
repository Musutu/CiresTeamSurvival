# RESUME — feat/kit-editor (Skill Assignment editor / Hero Creator)

Spec: Docs/EricFeedback/2026-09-27/PLAYTEST-6.md section I + the Hero Creator half of section M.
Wave rules: F:\CiresTeamSurvival-agents\WAVE-2026-09-27.md. Ports 17800-17849 (earlier runs used 17580-17589).
Design + data format: Docs/KitEditor.md.

## Done
- CireKitEditor.h/.cpp: template data (Content/Data/ChampionKitTemplates.json, schema 2 with profiles + named loadout presets,
  schema 1 migration, transactional parse, clamped values, pretty save), kit rules (6 actives / 1 ult / 1 passive, Normalize,
  AddBlocker), game integration (MergeIntoKits, GrantOnDraft, SpawnPlacedCast), anchor/bone resolution, placement spawn/apply,
  data-driven pool browser (Skill Shop sections, search, filters).
- Hero Creator (section M, d6778848): Skill-Shop scroll cards for ALL abilities (class list chip off by default), drag/click onto
  action-bar buttons with real keybind labels, multiple named presets per champion (save / save as / new / rename / delete / default),
  kit PROFILES spanning all champions (new / copy / rename / delete), WavePresets `kitProfile` hard-wires a profile to a game mode.
- Layout polish recovered after the VS Code crash (ded89afc: bar height, card height from the scroll art's natural aspect,
  panel title insets, gallery wait) — reviewed, sane, builds.
- Ability Tuner integration (bb199bb1): the Hero Creator subscribes to `CireAbilityTuner::OnChanged()` and polls `Version()`;
  on a change it respawns the looping preview effect (tuned VFX scale / tint; the live key includes the version), clears hover lift,
  drops a selection/drag of an ability that no longer exists, and shows a status line (warns when buttons of the loadout are
  disabled). Tuner-disabled cards are dimmed with a red "DISABLED BY TUNER" caption. `CireKitEditor::TunerStamp()` exposes the
  subscription count. Native test: rename via tuner -> OnChanged heard + card found by tuned name; disabled ability stays listed;
  tuner set restored.
- Merged main 8b323888 (ability-tuner, initiation, pack-usage-3, paragon-champions) — a97af919; 3 conflicts, all additive
  (includes in CireAbilityDB.cpp, probe calls in CireCombatExpansionProbe.cpp, accessors in CireDraftStage.h; both sides kept).
- CireKitEditorTests.cpp (242 checks), Docs/KitEditor.md.

## Not done / next
- Nothing blocking. Possible follow-ups: per-champion projectile muzzle sockets (see assumptions); Paragon champions were
  not installed in this worktree (packs absent), so the Hero Creator strip was not eyeballed with Paragon bodies.

## Assumptions / questions for Eric
- "Base kit" = the skills the champion STARTS the match with (grantOnDraft default ON, toggle per champion). This overrides the
  "1 skill point / opening role pick" rule for templated champions only; turn the toggle off to only add the kit to the Skill Shop list.
- Effect placement applies to the caster-attached CAST effect. Projectiles, impacts and ground zones keep their normal placement.
  Ask Eric if he wants per-champion projectile muzzle sockets too.
- Skills whose requirement the body fails (shield / ranged-only) are shown with a red "!" and not granted.
- The editor is on in all dev/editor builds; shipping needs -CireKitEditor.
- While the host edits, the pick timer is held (standalone / listen server). Clients see a warning instead.
- Tuner-disabled abilities remain assignable in the Hero Creator (templates are saved across matches; the tuner is per match),
  but are flagged on the card and in the status line and cannot be cast while disabled.

## Shared-file edits (small, additive)
- CireAbilityDB.cpp: include + `CireKitEditor::MergeIntoKits(GKits)` in Reload (after the Paragon merge, before the tuner re-apply).
- CireChampionProfiles.cpp: include + `CireKitEditor::GrantOnDraft(this)` in DraftProfile before RefreshOffer.
- CireSpellPresentation.cpp: include + UpdateFabVFX tries `CireKitEditor::SpawnPlacedCast` first for the cast role.
- CireCombatExpansionProbe.cpp: include + `CireKitEditor::RunTests(Mode)`.
- CireDraftStage.h: `GetPreviewHero()` accessor.
- CireRosterHUD.cpp: include, HERO CREATOR nav item, early-out that draws the editor while open.
- CireShopUI.cpp/.h: shared skill-card / section helpers used by the Hero Creator (DrawSkillCard, SkillSectionInfo, TipSkill, Pointer).
- CireWaveData.cpp / CireWaves.h / Content/Data/WavePresets.json: `kitProfile` on wave presets.

## Gate logs (2026-09-28, after merging main 8b323888; build Result: Succeeded)
- native PASS (CIRE_KIT_EDITOR_TESTS_PASS checks=242, CIRE_COMBAT_EXPANSION_PASS):
  Saved/ExpansionChecks/20260928T074752875630Z/report.json
- network PASS: Saved/NetworkSmoke/20260928T075105199237Z/report.json
- interface PASS: Saved/InterfaceSmoke/20260928T075236990047Z/report.json
