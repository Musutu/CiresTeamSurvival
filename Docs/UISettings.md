# Local interface preferences

The interface layout and presentation settings live in `Saved/Config/CireUI.ini` beneath this project. They belong to the local player and do not modify server combat rules. A missing, partially written, or unsupported-version profile falls back to defaults for the relevant values.

The reference layout is based on the user's September 23 interface image: player and party on the left, match status in the upper middle, minimap in the upper right, chat in the lower left, a central skill deck, and meters in the lower right. The optional combat log starts hidden so scrolling combat text is the default combat feedback. The reference canvas is 1280×720 logical pixels; saved panel positions and sizes are normalized to the current logical viewport.

All eleven panel IDs have independent geometry and an optional individual lock: `Player`, `Party`, `Match`, `Target`, `Focus`, `Minimap`, `Chat`, `Skills`, `Meter`, `CombatLog`, and `CombatText`. A separate global layout lock disables all movement and resizing. The initial state is globally locked, with all individual panels unlocked. Geometry is clamped to the visible viewport, including after a resolution change; minimum readable dimensions are 65% of the reference panel dimensions, reduced if the viewport itself is smaller. Reset restores the reference layout and all presentation preferences.

Preferences include chat visibility, font size and RGBA color; combat-log and meter visibility; damage or effective-healing meter mode; and two independent combat displays. Floating damage/healing numbers appear over the affected target, while anchored scrolling combat text appears in the movable `CombatText` panel. **Both displays are enabled by default and can appear simultaneously.** `bShowFloatingNumbers` and `bShowScrollingText` toggle them independently; `bCombatTextEnabled` is a master switch. Both displays share damage/healing and incoming/outgoing filters.

Floating numbers use `WorldNumberFontSize`, default 26 logical pixels and clamped to 12–48. Anchored text uses `CombatTextFontSize`, default 24 and clamped to 12–42. The two font preferences persist independently. Fonts and color components are sanitized on load and save. Chat alpha has a minimum of 0.35 so a malformed profile cannot make chat completely transparent.

Profile version 2 migrates version 1's exclusive `bCombatTextWorldSpace` setting to both display toggles enabled. It preserves the player's master enabled/disabled choice, filters, anchored font size, panel positions, and other existing preferences. It does not turn combat text back on when the player explicitly disabled the master switch. Saving writes version 2 and omits the obsolete exclusive-mode key. The legacy C++ field remains temporarily for source compatibility; rendering must use the two new independent toggles.

## Implementation contract

`FCireUISettings` is a plain C++ class; it does not require a UObject or a reflected save-game type. The HUD owns one instance, calls `Load()` once, uses `GetRect(PanelId, LogicalViewport)` to draw, and passes drag/resize results to `SetRect`. `SetRect` rejects edits while the layout or the individual panel is locked. `IsPanelLocked` returns the individual lock only, allowing the HUD to show both lock states separately. Save after releasing a drag or committing a setting rather than every frame. Check the `Save()` return value before reporting that preferences were saved. `Reset()` resets memory; call `Save()` to persist a reset.

The settings file uses Unreal's `FConfigFile` INI serializer independently of the global config cache, so loading a profile always reflects its current disk contents. Save first writes the complete profile to a temporary sibling file, then replaces the target. `Load(Filename)` optionally selects an isolated profile, used by the automated test.

## Verification

The development automation test `Cire.UI.Settings.Persistence` exercises actual file write/reload, global and individual locks, rectangle preservation, color/font/filter preservation, damaged-file clamping, three viewport sizes, and reset behavior. It verifies all four combinations of the independent combat display toggles, distinct font sizes, and migration from either old exclusive mode with the master switch both enabled and disabled. It writes only a uniquely named test profile under `Saved/Automation` and removes that profile afterward. The presence of the test is not a record that it has passed; see the current validation record or build output for execution results.
