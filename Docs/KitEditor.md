# Skill Assignment editor (kit editor)

Playtest 6, section I. This is a dev/editor mode inside Champion Select: **KIT EDITOR** sits in the top-right nav, beside CHAMPIONS / LOADOUTS / SETTINGS.
It's available in every non-shipping build. A shipping build needs `-CireKitEditor`.

## What it does

1. **Pick a champion.** The strip across the top lists every roster row (`Content/Data/ChampionRoster.json`), with its portrait and role ring.
   A green dot marks a champion that already has a saved template. The mouse wheel scrolls the strip.
2. **Browse the whole ability pool.** This is every row of the Ability Database (`Content/Data/Abilities.json`), grouped in the Skill Shop's
   periodic-table sections, with the same colours and order: Spell damage, Attack damage, Defensive, Crowd Control, Summons,
   Constructs, Passives, Ultimates.
   - **Search:** matches name, id, school, role types, effect tags, section, kind and learner champions. Every word must match.
   - **Filters:** kind (actives / passives / ultimates), role (DPS / TANK / HEAL), and section chips with counts. The first click isolates a section.
   - **Only this champion's list:** shows only what the champion can already buy.
   - **Tiles:** hover shows the full ability tooltip. Click adds the ability to the kit and selects it. Right-click removes it.
3. **Assign the base kit.** The kit has 6 active slots, 1 ultimate and 1 passive, the same capacity as the Skill Shop and draft.
   A full kind refuses the add with a message.
   - **START MATCHES WITH THIS KIT** (on by default): the champion is drafted with the whole kit learned at level 1.
     The skills level up in the Skill Shop, and there's no opening pick.
   - With the toggle off, the kit is only added to the champion's Skill Shop list.
   - A red **!** on a slot means this body can't use the skill (a shield skill on a shieldless champion, or a ranged-only skill on melee).
     That skill is not granted.
4. **Place the effects, per ability, for this champion.** Select a kit ability, then set:
   - **Attach to:** DEFAULT, FEET, PELVIS, CHEST, HEAD, R HAND, L HAND, R FOOT. Each anchor resolves on this body's skeleton
     (UE5 / Paragon, Mixamo-style Tripo and Biped names). Anchors the body lacks are greyed.
   - **BONE < >:** steps through every bone and socket of the body.
   - **Forward / right / up offset:** ±150 cm, in champion space.
   - **Size:** 0.2–3×.
   - **Tint:** 10 swatches or none, plus a strength slider.

   Right-click a slider to reset it; the mouse wheel nudges it.

   The preview loops the ability's cast effect live at the placement. **CAST PREVIEW** plays the champion's real cast clip and releases
   the effect on the clip's contact frame. Drag the model to turn it, or use `<` / `>`.
5. **SAVE TEMPLATE** writes `Content/Data/ChampionKitTemplates.json` and reloads the Ability Database, so the change is live at once.
   - **REVERT** returns to the saved copy.
   - **CLEAR KIT** plus save removes the kit, and the champion goes back to the default opening pick and Skill Shop list.
   - Unsaved changes ask for a second click before you switch champion or leave.

While the host (standalone or listen server) is in the editor, the pick timer is held so it can't auto-lock a champion. A client's timer keeps running, and the header says so.

## Data: `Content/Data/ChampionKitTemplates.json`

```json
{
  "schemaVersion": 1,
  "champions": {
    "knight": {
      "baseKit": ["shield_slam", "iron_guard", "war_cry", "cleaving_strike", "second_wind", "protection_dome", "bastion_of_dawn", "stone_skin"],
      "grantOnDraft": true,
      "updated": "2026-09-27T12:00:00.000Z",
      "effects": {
        "shield_slam": { "attach": "hand_r", "offset": [20, 0, 10], "scale": 1.3, "tint": [0.18, 0.92, 0.88, 1], "tintStrength": 0.8 }
      }
    }
  }
}
```

- **baseKit:** ability ids. When the file loads, unknown ids, duplicates and over-capacity entries are dropped. The order is actives → ultimate → passive.
- **attach:** an anchor key (`root`, `pelvis`, `chest`, `head`, `hand_r`, `hand_l`, `foot_r`, `foot_l`) or a literal bone or socket name.
- **offset:** cm, champion space (+X forward, +Y right, +Z up).
- **scale:** multiplies the effect's normal size.
- **tint:** RGBA; A > 0 recolours the effect through `CireFabVFX::Recolor`.

Values are clamped when the file is read. A malformed file keeps the previous templates and logs `CIRE_KIT_TEMPLATES_REJECTED`.

## How the game uses a template

| Where | What |
|---|---|
| `CireAbilityDB::Reload` → `CireKitEditor::MergeIntoKits` | Template skills join the champion's `purchasable` / `purchasableImplemented` lists, so the Skill Shop sells and levels them. A champion without an Ability DB kit, such as a new roster row, gets one from the template. `signature` is untouched. |
| `ACireHero::DraftProfile` → `CireKitEditor::GrantOnDraft` | Runs on the server when `grantOnDraft` is set. It learns the kit (Skills, cooldowns, progression, Skill Shop level 1) and clears the opening offer. |
| `ACireSpellVisual::UpdateFabVFX` → `CireKitEditor::SpawnPlacedCast` | Runs on the client. The caster-attached **cast** overlay of an ability is spawned on the casting champion's body at the saved socket, offset, scale and tint. This covers the Fab "cast" role: caster flares, self shocks and channels. Projectiles, impacts and ground zones keep their normal placement. |

## Data-driven

The pool is `CireAbilityDB::All()`, and the champions are `CireChampionRoster::All()`. New abilities (feat/ability-expansion) and new champions
(feat/paragon-champions) show up with no code change. An unknown `section` value gets its own block after the eight known ones.

## Code

| File | Contents |
|---|---|
| `Source/CiresTeamSurvival/CireKitEditor.h/.cpp` | Data, rules, game integration, placement, pool browser |
| `Source/CiresTeamSurvival/CireKitEditorUI.cpp` | The screen |
| `Source/CiresTeamSurvival/CireKitEditorTests.cpp` | Native suite (`CIRE_KIT_EDITOR_TESTS_PASS`), run by the combat expansion probe (`RunExpansionChecks.py --only native`) |

- **Screenshot capture:** `-CireKitEditorGallery[=<champion>]` opens the editor with a sample kit and a placed effect, writes `Saved/KitEditorGallery/kit_editor_*.png`, then quits.
