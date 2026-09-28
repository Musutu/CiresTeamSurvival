# RESUME: feat/ability-tuner (Playtest 6 section M)

Worktree `F:\CiresTeamSurvival-agents\cts-ability-tuner`, branch `feat/ability-tuner` (from main + feat/casting-rules +
feat/ability-expansion). Ports 17630-17639. User doc: `Docs/AbilityTuner.md`.

## Done
- Override layer (`CireAbilityTuner.{h,cpp}`): per-ability overrides on Abilities.json + AbilitiesExpansion.json +
  CastRules, applied at DB load (startup profile, re-applied after every `CireAbilityDB::Reload`) and live. Fields:
  name, tooltip text, enabled, VFX scale / tint, and **every numeric leaf of the row** by JSON path (damage / heal /
  potency / scaling, durations of CC / DoTs / buffs, cooldown, costs, cast time, range, radius, level curve, void
  zones, Apotheosis upgrade, expansion delivery recipes), plus new per-ability `level15.scale` / `durationScale`.
  Rows are rebuilt through `CireAbilityDB::ParseJson` (validation, AoE scale, CastRules) and swapped in place; a cast
  time override beats CastRules. Reset per field and per ability; old names still resolve.
- Replication: `ACireAbilityTunerState` (spawned by `ACireGameMode::BeginPlay`) carries the set as JSON; clients
  rebuild rows in OnRep (tooltips, action bars, Skill Shop). Host / single-player only; "Allow ability tuning" option.
- In-game Ability Tuner window (`CireAbilityTunerUI.{h,cpp}`, F7 "ToggleAbilityTuner"): search / filter, grouped
  generic field editor (slider + typed value + RESET), rename / tooltip text / enable, live WoW tooltip preview at
  Lv 1/5/10/15, VFX scale + hue strip, CAST IT NOW, TARGET DUMMY, profiles (SAVE / LOAD / STARTUP / EXPORT / IMPORT /
  RESET ALL). Read-only view for non-hosts.
- Profiles: editor -> `Content/Data/AbilityOverrides.json` (new, empty), packaged -> `<Saved>/Tuning/AbilityOverrides.json`;
  export / import `<Saved>/Tuning/Exports|Imports/*.json`; `-CireTuningProfile=`, `-CireAllowTuning`, `-CireNoTuning`.
- Coordination: `CireAbilityTuner::Version()` + `CireAbilityTuner::OnChanged()` for the Hero Creator (feat/kit-editor)
  and any card UI.
- Tests: `CireAbilityTunerTests.cpp` (native, in the combat-expansion probe): fields, apply, per-field / per-ability
  reset, validation, CC duration, CastRules precedence, expansion recipe, disabled -> shops, VFX by id / name, reload
  persistence, JSON round trip, profile save / load / startup / delete, export / import, shipping-safe defaults and
  paths, authoritative publish, presets. Network probe: the dedicated server pushes an override (`ember_lance`
  renamed + cooldown 3.5) and the remote client verifies name, cooldown, old-name alias and that it cannot tune
  (`CIRE_NET_CLIENT_TUNER PASS`).

## Not done / next
- Hero Creator (feat/kit-editor) should subscribe to `CireAbilityTuner::OnChanged()` / poll `Version()` to refresh its cards.
- No clipboard import / export (would need the ApplicationCore module); files only.

## Wiring for feat/waves-modes (`tuningProfile`) — DONE after merging main
`CireAbilityTuner::ApplyWavePreset(World, PresetId)` reads the preset's raw row in `WavePresets.json` (no change to
`FCireWavePreset`, so it cannot conflict with kitProfile work). Called from `CireWaveDirector::SelectPreset` and the
match-init preset pick in `CireWaves.cpp`. Keys: `"tuningProfile": "<profile name>"` ("" = startup set) and
`"allowTuning": true|false` (false also locks the option). No shipped preset sets them yet: Eric decides which game
types are ranked (suggest `"allowTuning": false` on standard / ranked presets).

## Assumptions / questions for Eric
1. There is no ranked / custom game distinction yet. Default: **on** in editor and development builds, **off** in a
   shipping build unless `-CireAllowTuning`, a preset, or the host flips it before wave 1. OK?
2. The **startup profile** (if Eric sets one) applies in every match, ranked included: it is treated as shipped
   balance data like Abilities.json. Hand edits in a match are dropped when the match ends.
3. Level-15 bonuses use shared global numbers per bonus kind; per ability the tuner exposes a magnitude and a
   duration multiplier (`level15.scale`, `level15.durationScale`) instead of absolute values.
4. Numbers that live in C++ kit code (e.g. cone angles in CireKitSkills, legacy starter skill specs) are not tunable
   yet; everything the game reads from the ability rows is.
5. CAST IT NOW grants an unknown skill to the host's bar for the test (not removed afterwards) and refunds cost /
   cooldown; it needs a combat phase.
6. F7 is the default key (free). Radius values are shown as authored (the global AoE scale still grows them).
7. Remote clients see a read-only tuner (what is tuned and the live tooltip) rather than nothing.

## Shared-file edits (small, additive)
- `CireAbilityDB.h/.cpp`: `ReplaceRow`, reload hook, `level15.scale/durationScale` parse, disabled filter in
  `PurchasableSkills` / `CanLearn`, two `FCireAbilityDef` fields.
- `CireAbilityExpansion.h/.cpp`: recipe parse extracted to `ParseRecipe` (no behaviour change), `SetRecipeOverride`,
  `FTable::Authored`.
- `CireScalingKits.cpp` (`CireKits::ApplyLevel15`): per-ability level-15 scales.
- `CireSignatureSkills.cpp`: disabled abilities refuse to cast.
- `CireFabVFX.cpp`: tuned VFX scale / tint in `FindAbility` / `FindFor`.
- `CireHUD.cpp`: draw call, pointer-over-interface, Escape.
- `CireController.cpp`: F7 toggle, keyboard ownership, network client probe check.
- `CireChat.cpp` (viewport `InputChar`): typed characters to the tuner.
- `CireKeybindings.cpp`: `ToggleAbilityTuner` (F7) + action-count test `+1`.
- `CireMatch.cpp`: `CireAbilityTuner::InitializeServer(this)` in BeginPlay.
- `CireCombatExpansionProbe.cpp`: `CireAbilityTuner::RunSmoke(Mode)`.
- `CireWaves.cpp`: two `CireAbilityTuner::ApplyWavePreset` calls (SelectPreset, match-init pick).

## Gate logs (after merging main e5d261a7, 2026-09-28)
- Native PASS (`CIRE_ABILITY_TUNER_PASS checks=39`, keybindings 83 actions): `Saved/ExpansionChecks/20260928T065836921128Z/report.json`
- Network PASS (`CIRE_NET_CLIENT_TUNER PASS ... name=Tuned Ember Lance cooldown=3.50 client_denied`): `Saved/NetworkSmoke/20260928T070141688917Z/report.json`
- Interface PASS: `Saved/InterfaceSmoke/20260928T070307224355Z/report.json`
