# Keybindings and action bars (core)

`Source/CiresTeamSurvival/CireKeybindings.h/.cpp` is the data-driven action -> key map. Every
gameplay key in `ACireController` and `CireCamera` is read through it; nothing else in the live
game hard-codes a key. This is the **core only**: the keybinding screen and the action bars are
drawn by the HUD (wow-ui) on top of the API below.

## Default layout (WoW)

| Category | Action (id) | Default |
|---|---|---|
| Movement | MoveForward / MoveBackward | W, Up / S, Down |
| | TurnLeft / TurnRight (strafe while right mouse is held) | A / D |
| | StrafeLeft / StrafeRight (sideways, never rotates) | Q / E |
| | Jump | Space |
| | ToggleAutoRun (W or S cancels) | Num Lock |
| | ToggleWalk | Caps Lock |
| | DodgeRoll | Left Ctrl, Right Ctrl |
| Combat | ToggleAutoAttack | T |
| Targeting | TargetNextEnemy / TargetPreviousEnemy | Tab / Shift+Tab |
| | TargetNextAlly / TargetSelf | F / F1 |
| Interface | OpenChat, ToggleShop, ToggleSkillShop, ToggleHelp, ToggleStats, ToggleLootLog | Enter, B, K, H, C, L |
| | RecallToTown (moved off R) | G |
| | ToggleOptions, ToggleLayoutEditor, ToggleDeveloperTools | F9, F10, F8 |
| | RosterPreviousPage / RosterNextPage (draft screen) | Page Up, Left / Page Down, Right |
| | ToggleSkillOffer (open / defer the level-up skill choice) | N |
| Action bar 1 | ActionBar1_Slot1..6 | 1..6 (active skills in learn order) |
| | ActionBar1_Slot7 | unbound (passive, display only) |
| | ActionBar1_Slot8 | R (ultimate) |
| | ActionBar1_Slot9..12 | 7, 8, 9, 0 |
| Action bar 2 | ActionBar2_Slot1..6 | Shift+1..6 (empty slots) |
| Action bar 3 | ActionBar3_Slot1..6 | Alt+1..6 (empty slots) |

Bar 3 uses Alt, not Ctrl: Ctrl alone is Dodge roll, so a Ctrl+N press would roll first.
The old binds have moved: Space auto-attack -> T, E jump -> Space, Q ultimate -> R, R recall -> G.
During the draft, bar 1 slots 1..6 pick roster entries; with an augment offer open, slots 1..4 learn.

**Fixed, non-rebindable keys:** Escape (cancel, menus, cancels capture), the chat text editor
(Enter, Tab and Backspace while typing), left and right mouse (camera, click-select, aim, confirm),
the mouse wheel (zoom), and Shift+left click (summon move order). The replay spectator
(`CireReplaySpectator`) keeps its own fixed playback keys.

## Matching rules

A binding is a chord: one key plus required Shift/Ctrl/Alt. `WasPressed` needs the modifiers to
match exactly, so Tab and Shift+Tab, or 1 and Shift+1, never both fire. `IsDown` on an unmodified
chord ignores extra held modifiers, so you can hold Shift and still move with W. A modifier used as
the key itself (Left Ctrl for dodge) is not treated as its own modifier.

## API for the keybinding screen and action bars

```cpp
FCireKeybindings& Keys = HUD->UISettings.Keybindings;          // saved with the UI profile
for (const FCireActionInfo& A : CireKeybindings::Actions())    // display order, Category, DisplayName
    Row(A.DisplayName, CireKeybindings::CategoryName(A.Category),
        Keys.Get(A.Id,0).LongLabel(), Keys.Get(A.Id,1).LongLabel());
Keys.BeginCapture(A.Id, /*0 primary, 1 secondary*/ 0);         // then the controller runs TickCapture:
// Esc cancels, Backspace/Delete unbinds, any key binds with held modifiers (a lone modifier binds on release).
// Gameplay input is suspended while capturing; the profile is saved on bind/unbind.
const FCireCaptureResult& R = Keys.LastCapture();              // R.Bind.ConflictAction -> show "Also removed from ..."
Keys.Bind(Action, Index, FCireKeyChord(EKeys::Q, /*Shift*/true), ECireBindPolicy::Swap /*or UnbindOther*/);
Keys.Unbind(Action, Index); Keys.ResetToDefaults(); Keys.FindConflict(Chord);
Keys.Label(Action);       // "S-1", "C-2", "A-3", "M4", "Spc", "S-Tab" (skill-bar corner label)
Keys.FullLabel(Action);   // "Shift+Tab / Mouse 4" (tooltips)
```

Action bars (`FCireKeybindings::NumBars = 3`, `SlotsPerBar = 12`, ids from `CireKeybindings::SlotAction(Bar, Slot)`):

```cpp
FString Id = CireKeybindings::SlotAbilityId(Keys, *Hero, Slot);  // icon to draw (may be unlearned)
int32 Skill = CireKeybindings::ResolveSlot(Keys, *Hero, Slot);    // index into Hero->Skills, or INDEX_NONE
Keys.AssignSlot(Hero->ChampionProfileId, Slot, AbilityId);        // drag an ability onto a slot
Keys.ClearSlot(ProfileId, Slot);                                  // explicitly empty
Keys.ResetSlot(ProfileId, Slot);                                  // back to the automatic default
```

Pressing a slot's chord casts `ResolveSlot` through the normal `RequestCast` path (ground aiming,
server validation). Placements are per champion profile id. Unplaced bar-1 slots follow the
automatic layout: actives in learn order, then passive, then ultimate. A new champion therefore
works with no setup. After changing bindings or placements, call `UISettings.Save()`. The capture
path already saves.

## Persistence and migration

The keys live in the existing `Saved/Config/CireUI.ini`, in their own section, with their own
schema version:

```ini
[CireUI.Keybindings]
KeybindingsVersion=1
StrafeLeft=Q|ThumbMouseButton
TargetPreviousEnemy=Shift+Tab|
Place.bear.ActionBar2_Slot1=maul
Place.bear.ActionBar1_Slot2=-          ; "-" = explicitly empty
```

Profiles saved before this section existed (UI schema 3 and older) keep all their preferences and
receive the WoW defaults. The UI `Version` key is deliberately left at 3 so the wow-ui branch's own
schema changes merge cleanly; keybindings migrate via `KeybindingsVersion`. On load, each action
with an unparsable or reserved chord keeps its default, unknown actions are ignored, duplicate
chords are repaired (the first action in display order keeps the chord), and a future
`KeybindingsVersion` falls back to the defaults.

## Tests

- `CireKeybindings::RunSmoke`: 34 checks covering the defaults and their uniqueness, labels,
  chord parsing, swap and unbind conflict policies, capture (Esc, Delete, Shift chord, lone
  modifier, reserved mouse button), reset, profile round-trip, placement round-trip, schema-3
  migration, damaged entries and a future schema version.
- Movement suite: action-bar slot resolution against a real hero (automatic layout, passive not
  castable, explicit placement, unlearned, reset).
- Camera runtime: a simulated E key press through PlayerInput and CharacterMovement moves the
  hero 220 cm sideways with 0 cm forward and 0 degrees yaw change.
- Network smoke: the remote client strafes; the server-reconciled result is 341.6 cm lateral,
  0 cm forward and 0 degrees yaw change.
