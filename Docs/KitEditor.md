# Hero Creator (kit editor)

Playtest 6, sections I and M. The Hero Creator is a dev/editor mode inside Champion Select. To open it, use **HERO CREATOR** in the top-right nav, beside SETTINGS / LOADOUTS / CHAMPIONS.
It's available in every non-shipping build. A shipping build needs `-CireKitEditor`.

In the Hero Creator you:

1. Pick a champion.
2. Take spells from **every** ability in the game, the same way you buy them in the Skill Shop. Any champion may take any skill.
3. Put them on the champion's real skill buttons.
4. Save them as **named loadout presets** inside a **kit profile**. Game modes choose which profile they use.

## Screen

- **Header:** the title and the **KIT PROFILE** picker (NEW, COPY, RENAME, DELETE; Standard can't be deleted), plus BACK TO CHAMPION SELECT.
- **Champion strip:** every roster row (`Content/Data/ChampionRoster.json`) with its portrait and role ring. A dot marks a champion
  that has a loadout in the current profile. The mouse wheel scrolls the strip.
- **SPELLS & BUTTONS tab**
  - **Left panel:** every row of the Ability Database, drawn as the Skill Shop's own scroll cards (golden active, plain passive,
    prismatic ultimate). The cards sit in the Skill Shop's periodic-table sections, with the same colours and order.
    - **Search:** matches name, id, school, section, kind, role types, effect tags and categories. Every word must match.
    - **Filter chips:** kind, section, role, and the champion's own class list. The role and class-list chips are off by default.
    - **Adding a card:** click a card to put it on the next free button (or the selected one), or drag it onto a button.
    - **Tooltip:** hovering a card shows the Skill Shop tooltip.
  - **Right panel, LOADOUT PRESETS:** the champion's presets in this profile. SAVE, SAVE AS, NEW, RENAME, DELETE, SET DEFAULT,
    and **START MATCHES WITH THE DEFAULT**.
- **EFFECT PLACEMENT tab:** the champion's real body on the draft stage. Drag the model to turn it, or use `<` / `>`. The page
  switches between two pages:
  - **CAST EFFECT:** the selected ability's cast effect, looping live at its placement:
    - attach point (DEFAULT, FEET, PELVIS, CHEST, HEAD, R HAND, L HAND, R FOOT), resolved on this skeleton's naming scheme
      (UE5 / Paragon, Mixamo, Biped);
    - **BONE < >** to step through every bone and socket;
    - forward / right / up offset (±150 cm);
    - size (0.2–3×);
    - tint (10 swatches, plus strength).

    **CAST PREVIEW** plays the champion's cast clip and releases the effect on its contact frame.
  - **PROJECTILE MUZZLE:** where the champion's projectiles leave from. There are two scope chips:
    - **ALL PROJECTILES:** every skillshot plus the ranged basic attack.
    - **ONLY \<spell\>:** an override for the selected ability.

    Set the attach point, bone and offset. A cyan marker sits on the muzzle, and a test projectile (the spell's own projectile art,
    or an orange test ball when no art is installed) loops forward from it. The page prints the muzzle's position in champion space.
  - **Sliders:** right-click a slider to reset it; the mouse wheel nudges it.
- **Action bar (bottom):** the champion's skill buttons with **your own keybind labels**: keys 1–6, the ultimate R, and the passive.
  - Click a button to select it (the next card replaces it).
  - Right-click it, or use its x, to clear it.
  - A kind that doesn't fit the button gets a warning "!".
- **Leaving with unsaved changes** asks for a second click.

While the host (standalone or listen server) is in the Hero Creator, the pick timer is held. A client's timer keeps running, and the header says so.

## Ability Tuner link

- **Live refresh:** cards, names, numbers and tooltips read the live (tuned) rows every frame. The Hero Creator also subscribes to
  `CireAbilityTuner::OnChanged()`, and polls `Version()`, so each change respawns the preview effect and test projectile with the tuned
  VFX scale / tint.
- **Disabled abilities:** they stay in the pool, dimmed, with a red DISABLED BY TUNER caption. They can still be assigned, because
  templates outlive one match.
- **EDIT badge:** a small bevelled pencil in the top-right corner of the hovered ability card or button. Clicking it opens the
  Ability Tuner on that ability (`CireAbilityTunerUI::Select`). Its tooltip reads "Edit in Ability Tuner (F7)", with your own key.
  - It's shared code (`CireTunerLink`), so it has the same look and placement on Hero Creator cards, Hero Creator buttons and Skill
    Shop cards.
  - **Action bar:** tooltips can't hold clicks, so you **Alt+click** a slot instead. The ability tooltip's footer says so.
  - It's shown only when `CireAbilityTuner::CanTune` passes: the host or single player, while "Allow ability tuning" is on. It's never
    shown to a remote client, or in a shipping build without the tuning flag.
- **Clicks under the Tuner window:** while the Tuner window is open, the Skill Shop and Hero Creator ignore the pointer inside it, so
  clicks there reach the Tuner.

## Data: `Content/Data/ChampionKitTemplates.json` (schemaVersion 2)

```json
{
  "schemaVersion": 2,
  "profiles": {
    "Standard": {
      "champions": {
        "knight": {
          "default": "Bulwark",
          "grantOnDraft": true,
          "updated": "2026-09-28T08:00:00.000Z",
          "presets": [
            { "name": "Bulwark", "slots": { "1": "shield_slam", "2": "iron_guard", "3": "war_cry", "R": "bastion_of_dawn", "P": "stone_skin" } }
          ]
        }
      }
    },
    "Hero TD": { "champions": { } }
  },
  "effects": {
    "knight": { "shield_slam": { "attach": "hand_r", "offset": [20, 0, 10], "scale": 1.3, "tint": [0.18, 0.92, 0.88, 1], "tintStrength": 0.8 } }
  },
  "muzzles": {
    "ranger": {
      "*":          { "attach": "hand_l", "offset": [12, 0, 4], "point": [38.5, -21, 52] },
      "piercing_arrow": { "attach": "hand_r", "point": [40, 18, 60] }
    }
  }
}
```

- **Profiles:** `Standard` always exists and comes first.
  - A game mode names its profile in `Content/Data/WavePresets.json` as `"kitProfile"`.
  - `-CireKitProfile=<name>` overrides the profile.
  - A champion missing from the active profile falls back to its Standard loadout, then to its built-in kit.
- **presets / slots:** slot keys are `"1"`–`"6"` (the key buttons), `"R"` (ultimate) and `"P"` (passive).
  - Unknown ids and duplicates are dropped.
  - The key buttons are packed from 1.
  - `default` is the preset the game uses.
- **effects:** per champion, not per profile, because they're about the body.
  - **attach:** an anchor key (`root`, `pelvis`, `chest`, `head`, `hand_r`, `hand_l`, `foot_r`, `foot_l`) or a literal bone or
    socket name.
  - **offset:** cm, in champion space (+X forward, +Y right, +Z up).
  - **scale:** multiplies the effect's normal size.
  - **tint:** RGBA; A > 0 recolours the effect.
- **muzzles:** per champion. `"*"` is every projectile; an ability id overrides `"*"` for that ability.
  - `attach` and `offset` are what you author.
  - `point` is the muzzle **baked** into champion space (cm, unscaled actor-local) from the preview body when you save.
  - Points are clamped to a box around the body: ±200 cm horizontally, −90 to +160 cm vertically.
- **Schema 1 files still load:** the old single `baseKit` per champion becomes a "Default" preset in Standard.

Values are clamped when the file is read. A malformed file keeps the previous data and logs `CIRE_KIT_TEMPLATES_REJECTED`.

## How the game uses the data

| Where | What |
|---|---|
| `CireAbilityDB::Reload` → `CireKitEditor::MergeIntoKits` | Loadout skills join the champion's purchasable Skill Shop lists. |
| `ACireHero::DraftProfile` → `CireKitEditor::GrantOnDraft` | Runs on the server. With `grantOnDraft` on, the active profile's default loadout is learned at level 1, in button order. |
| `ACireSpellVisual::UpdateFabVFX` → `CireKitEditor::SpawnPlacedCast` | Runs on the client. The caster-attached cast effect sits at the saved placement. |
| `ACireSkillshot::Spawn` and `ACireTargetProjectile::Launch` → `CireKitEditor::ProjectileStart` | Runs on the **server**. The projectile spawns at `ActorTransform × point` of the ability's muzzle (else `"*"`), so the replicated start is the same on every machine. A skillshot aims from the muzzle unless the aim point is beside or behind it. The old spawn point is kept when no muzzle is set, when the muzzle is behind a wall (world-static trace from the body), or when it's outside the realm. |

A dedicated server doesn't animate bodies, which is why the muzzle is baked from the editor's posed preview body, not read from a live
socket at runtime. A hand-edited muzzle without `point` uses the old spawn point plus its offset.

## Data-driven

The pool is `CireAbilityDB::All()`, and the champions are `CireChampionRoster::All()`. New abilities and new champions (Paragon
included) show up with no code change. An unknown `section` value gets its own block after the eight known ones.

## Code

| File | Contents |
|---|---|
| `Source/CiresTeamSurvival/CireKitEditor.h/.cpp` | Data (profiles, presets, effects, muzzles), rules, game integration, placement, muzzle hook, pool |
| `Source/CiresTeamSurvival/CireKitEditorUI.cpp` | The screen |
| `Source/CiresTeamSurvival/CireTunerLink.h/.cpp` | The EDIT badge and Alt+click link to the Ability Tuner |
| `Source/CiresTeamSurvival/CireKitEditorTests.cpp` | Native suite (`CIRE_KIT_EDITOR_TESTS_PASS`, plus `CIRE_TUNER_LINK_TESTS_PASS`), run by the combat expansion probe (`RunExpansionChecks.py --only native`) |

- **Screenshot capture:** `-CireKitEditorGallery[=<champion>]` opens the editor, writes `Saved/KitEditorGallery/kit_editor_*.png`, then quits.
