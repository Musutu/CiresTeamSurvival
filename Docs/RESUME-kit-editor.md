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
- Follow-up 1 (fb51a8e7): per-champion PROJECTILE MUZZLE (Eric: yes). Hero Creator > EFFECT PLACEMENT > PROJECTILE MUZZLE page:
  ALL PROJECTILES / ONLY <spell> scope, attach chips + bone picker + offset sliders, cyan muzzle marker + looping test projectile
  (spell's projectile art, else an orange test ball). Data `muzzles` (champion -> "*" | ability id -> attach/offset/baked point).
  Runtime (server): `CireKitEditor::ProjectileStart` in ACireSkillshot::Spawn and ACireTargetProjectile::Launch; unset -> old point.
- Follow-up 2 (9d7703ea): EDIT badge -> Ability Tuner (CireTunerLink): pencil badge top-right of the hovered Hero Creator card /
  loadout button and Skill Shop card, tooltip "Edit in Ability Tuner (<F7 key>)"; action bar: Alt+click a slot (hint in the
  tooltip footer). Only when CireAbilityTuner::CanTune. CireShopUI::Pointer ignores the pointer inside the open Tuner window so
  the shop / Hero Creator underneath no longer eat its clicks.
- Merged main aed99919 (bosses-spacing, bonus-loot, shop-anywhere, ...) cleanly — 236bcb9a.
- CireKitEditorTests.cpp (258 checks incl. muzzle + CireTunerLink 6), Docs/KitEditor.md rewritten (profiles, presets, muzzle, EDIT).

## Not done / next
- Nothing blocking. Not eyeballed in a rendered run: the muzzle page / test projectile and the EDIT badge (native tests run
  -nullrhi; no gallery shot taken to save disk). Paragon packs are absent in this worktree, so Paragon bodies were not checked.

## Assumptions / questions for Eric
- "Base kit" = the skills the champion STARTS the match with (grantOnDraft default ON, toggle per champion). This overrides the
  "1 skill point / opening role pick" rule for templated champions only; turn the toggle off to only add the kit to the Skill Shop list.
- Effect placement applies to the caster-attached CAST effect; projectile start points use the MUZZLE page (Eric said yes);
  impacts and ground zones keep their normal placement.
- Skills whose requirement the body fails (shield / ranged-only) are shown with a red "!" and not granted.
- The editor is on in all dev/editor builds; shipping needs -CireKitEditor.
- While the host edits, the pick timer is held (standalone / listen server). Clients see a warning instead.
- Muzzle is BAKED into champion space from the editor's posed preview body at SAVE (a dedicated server does not animate, so a
  live socket there would be the ref pose). A skillshot aims from the muzzle unless the aim point is beside/behind it (dot < .5).
  Muzzle rejected (old point used) when a world-static wall is between body centre and muzzle or it leaves the realm.
  Points clamped to +-200 cm horizontally, -90..+160 cm vertically so sweeps still hit capsules.
- Action-bar modifier is Alt+click, not Ctrl+click: Ctrl is bound to Dodge roll by default.
- EDIT badge shows only on hover (keeps cards clean); on a loadout button it temporarily hides the "!" kind warning in that corner.
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
- CireSkillshot.cpp: include + origin = `CireKitEditor::ProjectileStart(...)` (muzzle) in Spawn.
- CireAttackSystem.cpp: include + Start = `CireKitEditor::ProjectileStart(...)` in ACireTargetProjectile::Launch.
- CireShopUI.cpp: includes, EDIT badge on the hovered Skill Shop card, Pointer() masks the open Ability Tuner window.
- CireHUDActionBars.cpp: include, Alt+click slot -> tuner, tooltip footer hint.

## Gate logs (2026-09-28 08:3x, after merging main aed99919; build Result: Succeeded)
- native PASS (CIRE_KIT_EDITOR_TESTS_PASS checks=258, CIRE_TUNER_LINK_TESTS_PASS checks=6, CIRE_COMBAT_EXPANSION_PASS):
  Saved/ExpansionChecks/20260928T082855895399Z/report.json
- network PASS: Saved/NetworkSmoke/20260928T083125032478Z/report.json
- interface PASS: Saved/InterfaceSmoke/20260928T083217139551Z/report.json
- Probe note: the network / interface smokes do not exercise champion projectiles with a muzzle (only the expansion net probe
  spawns skillshots, from heroes without muzzle data), so no probe check was added; native covers the server spawn position.
