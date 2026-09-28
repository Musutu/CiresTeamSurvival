# Ability Tuner

Live ability balancing in the running match: names, tooltip text, damage / heal / potency and scaling, durations
of CC, DoTs and buffs, cooldown, costs, cast time, range, radius, level-15 bonus strength, VFX scale / tint,
and enabled / disabled. Part of the testing build **and the final release** (no `!UE_BUILD_SHIPPING` wrapper).

Eric (Playtest 6, section M): *"we need to adjust ability names and effects/duration as a feature in the testing
build and final release, allowing for skill adjustments and balancing while playing and testing."*

## Using it

- **F7** (rebindable: Options > Keybindings > Interface > "Ability Tuner") opens the window. Escape closes it.
- **Left:** search the whole pool (name, id, school, effect tag, champion) and filter: ALL, TUNED, OFF, ACTives,
  ULTimates, PASsives. A gold diamond marks tuned abilities; disabled ones are struck through.
- **Middle:** the selected ability. Rename it, rewrite its tooltip text (`{effect}` is replaced by the scaled
  number), enable / disable it. Below: **every number of the ability's data row**, grouped (Core, Scaling,
  Effects, Delivery, Void zone, Level 15 bonus, Apotheosis upgrade, Level curve). Drag the slider or click the
  value and type any number (Enter applies, Escape cancels). Tuned values are gold with the default beside them
  and a RESET button. RESET THIS ABILITY restores the whole row.
- **Right:** the live tooltip at level 1 / 5 / 10 / 15, the VFX scale slider and tint hue strip, and two test
  helpers: **CAST IT NOW** (casts at your target or ahead, full mana, no cooldown; unknown skills are added to
  your bar for the test) and **TARGET DUMMY** (an immobile, harmless 1,000,000-health monster, auto-targeted).
- **Bottom:** profiles. Type a name, then SAVE, LOAD (cycles saved profiles and applies the next), STARTUP (this
  profile loads with the ability database in every match), EXPORT, IMPORT (cycles files in the import folder),
  RESET ALL (click twice).

Every change applies immediately. Rows are rebuilt through the normal Ability DB parser, so the validation of
`Abilities.json` still holds (for example, an effect magnitude must stay 0..1); a refused value shows a red
message and nothing changes.

## Who can tune

- The **listen-server host** or a **single-player** session only. Remote clients get a read-only view (the
  status line says why) and have no path to send overrides: there is no client RPC, all edits run on the server.
- Gated by the game option **Allow ability tuning** (button in the tuner header):
  - Editor and development builds: **on**. Shipping: **off** unless `-CireAllowTuning`, a mode preset, or the
    host turns it on before the first wave (custom games).
  - `-CireNoTuning` (or a preset with `"allowTuning": false`, e.g. ranked / standard) turns it off and locks it.

## How it works

| Piece | File |
| --- | --- |
| Override layer, profiles, access, replication, helpers | `Source/CiresTeamSurvival/CireAbilityTuner.{h,cpp}` |
| Replicated carrier actor | `CireAbilityTunerState.h` (implemented in `CireAbilityTuner.cpp`) |
| In-game window | `CireAbilityTunerUI.{h,cpp}` |
| Native checks | `CireAbilityTunerTests.cpp` (in `-CireCombatExpansionProbe`) |
| Profiles (editor-authored, shipped) | `Content/Data/AbilityOverrides.json` |

- An override addresses numbers by their **JSON path in the ability row**: `base.cooldown`, `castTime`,
  `effects.0.duration`, `scaling.primary`, `void.innerRadius`, `ultimateUpgrade.effects.1.amount`,
  `recipe.buffSeconds` (expansion delivery recipes: DoTs, buffs, summons, telegraphs). The editor lists every
  numeric leaf of the row generically, plus `level15.scale` and `level15.durationScale` (new optional row keys,
  default 1, applied in `CireKits::ApplyLevel15`). Hidden: `base.castTime` (the parser reads the top-level
  `castTime`) and `recipe.construct.*` (construct tech recipes are compiled once).
- Applying an override clones the file row, patches it, and runs it through `CireAbilityDB::ParseJson` (AoE
  scaling, validation, CastRules), then swaps the row **in place** (`CireAbilityDB::ReplaceRow`) so every
  `Find()` pointer stays valid. A `castTime` override replaces the CastRules cast time (`CastRule` becomes
  `tuned`); the heal scale of the rule still applies. Expansion `recipe.*` numbers go to
  `CireAbilityExpansion::SetRecipeOverride`. Earlier names keep resolving through `FindByName`.
- Disabled abilities drop out of `PurchasableSkills` / `CanLearn` (Skill Shop, offers) and
  `CireSignatureSkills::Cast` refuses them. VFX scale / tint ride on `CireFabVFX::FindAbility` / `FindFor`.
- **Replication:** the server spawns one `ACireAbilityTunerState` per match (always relevant). It carries the whole
  set as JSON plus a revision, the option flags and the profile name. Clients rebuild the same rows in `OnRep`,
  so tooltips, action bars and the Skill Shop show the new names and numbers. Late joiners get the current set.
  When the match ends the process returns to the startup profile.
- **Change notification (Hero Creator, Skill Shop, any card UI):** `CireAbilityTuner::Version()` (bumped on every
  change) and `CireAbilityTuner::OnChanged()` (multicast, fired after the rows were rebuilt, on server and clients).

## Profiles and files

- Profiles file format:
  `{"schemaVersion":1,"profile":"CireAbilityOverrides","startupProfile":"","profiles":{"Name":{"notes":"","abilities":{"<id>":{...}}}}}`.
- One ability override:
  `{"name":"...","description":"...","enabled":false,"vfxScale":1.2,"vfxTint":[1,0.3,0.1],"fields":{"base.cooldown":8}}`.
- **Editor** (and editor `-game` sessions) save to `Content/Data/AbilityOverrides.json` (commit it to ship a
  profile). **Packaged** builds save to `<Saved>/Tuning/AbilityOverrides.json`; profiles from both files are
  listed and a Saved profile with the same name wins.
- **Startup profile:** applied at database load in every process (it is the shipped balance layer). Saved's choice
  wins over Content's. Empty by default, so nothing changes until Eric sets one.
- **Export** writes `<Saved>/Tuning/Exports/<name>.json`
  (`{"schemaVersion":1,"profile":"CireAbilityTuningProfile","name":...,"abilities":{...}}`); **Import** reads the
  same format from `<Saved>/Tuning/Imports/` (and Exports). Unknown ability ids are dropped on import.
- Command line: `-CireTuningProfile=<Name>` applies a profile at match start; `-CireAllowTuning` /
  `-CireNoTuning` set the option.

## Game-mode presets (`tuningProfile`)

`feat/waves-modes` owns `WavePresets.json`. When a preset is applied on the server, call
`CireAbilityTuner::ApplyModePresetJson(World, PresetJson)`; it reads `"tuningProfile"` (profile name, "" = the
startup set) and `"allowTuning"` (true / false; false also locks the option). See the RESUME doc for the wiring.

## Limits

- Only numbers the game reads from the ability row are live. Hard-coded kit constants (for example a kit's cone
  angle in `CireKitSkills`, legacy starter skills' `FCireSkillshotSpec` numbers) are not rows yet.
- The level-15 bonus kinds share global numbers (`Cires::Kits::L15`); per ability the tuner scales magnitude and
  duration.
- CAST IT NOW skips the cast bar (it casts through the same server path the action bar uses after the cast).
