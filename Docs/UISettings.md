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

## Schema 4 (WoW-style interface)

`LayoutVersion` is 4. New keys (all optional; older profiles keep defaults):

| Key | Default | Range / meaning |
| --- | --- | --- |
| `bAutoUIScale`, `UIScale` | true, 1.0 | Interface scale multiplier on top of the resolution fit, .64-1.15. Auto: 1.0 up to 1080p, ~0.93 at 1440p, 0.85 at 2160p. |
| `TooltipMode` | 3 | 0 cursor, 1 fixed (anchor top-left), 2 radial, **3 WoW corner anchor** (grows up/left from the Tooltip panel's lower-right). v1-v3 profiles with mode 0 (the old default) migrate to 3; radial/fixed are kept. |
| `TooltipOpacity`, `TooltipDelay`, `bTooltipAvoidCenter`, `bUnitTooltips` | .94, .12s, true, true | Background opacity .3-1, hover delay 0-1.5s, keep clear of the reticle and ground aim, hover tooltips on characters. |
| `bShowMisses`, `bCritPop`, `bSchoolColors`, `bMergeAoE`, `SCTDirection`, `SCTSpeed`, `SCTFadeSeconds` | true, true, true, true, 0, 1, 3.2 | Combat text: misses/dodges, crit enlarge, school colours, AoE merging, 0 up / 1 down / 2 fountain, speed .5-2, display time 1.5-5s. |
| `bShowThreatMeter`, `bThreatWarnings`, `bThreatSound`, `ThreatWarningPercent` | true, true, true, 90 | Threat meter, aggro alerts, alert sounds, pull-progress warning 60-100%. |
| `bLevelUpEffect`, `bShowBossFrames` | true, true | Level-up burst/banner, boss & pack-leader frames. |
| `bShowActionBar2`, `bShowActionBar3`, `bLockActionBars` | true, false, false | Extra action bars and drag lock (Shift-drag while locked). |

Panels gain `Anchor` (0-8: horizontal left/centre/right + 3 x vertical top/centre/bottom). Panel
sizes are stored in reference units, so they keep their designed size when the interface scale
changes; positions keep their distance from the anchored edge (left/top offsets, right/bottom
offsets, or stay centred). Older profiles derive the anchor from the saved rectangle, which
reproduces their 16:9 layout exactly. New panels: `Threat`, `Boss`, `Bar2`, `Bar3`. At large
scales the HUD lets chat/meter/combat log narrow beside the action bars, the focus frame
narrow between target and minimap, and boss frames show fewer rows above the threat meter
instead of overlapping (`ACireHUD::PanelRect`).

Keybindings and per-champion action-bar placements live in the same file
(`[CireUI.Keybindings]`, `[CireUI.ActionBars.<profile>]`, see `Docs/Keybindings.md`).

`CireOptions::RunSettingsSmoke` (run by `RunExpansionChecks.py --only native`) covers the
schema-4 defaults, v3 migration, roundtrip, bounds and right-anchor preservation.

## Schema 5 (WoW default layout)

`LayoutVersion` is 5. Default layout (1280x720 reference): player top-left (20,20), target to its right
(290,20, 240x140; target-of-target inside), focus left-middle under the target (290,176), party 210 wide,
compact match plate top-centre (540,16, 250x70), right column minimap / boss frames (1040,208) / threat
(1040,372) / damage meter (1040,566, 220x134), bag bar above chat (20,420), pet frame under focus
(290,306), stats window beside the right column (858,208, closed by default; C toggles). The screen centre
stays clear (checked by the WoW UI gallery, along with no overlaps between the default frames).

Profiles older than 5 adopt these defaults **only for panels still at their old default rectangle**;
panels the player moved keep their saved place. New keys: `bMeterCollapsed`, `bThreatCollapsed` (click
the header to fold these panels). The Developer Tools [F8] button is hidden in normal play until F8 is
pressed (or the game runs with `-dev`). A focus target that already has a boss frame is shown there
(tagged FOCUS) instead of a second frame. Transition banners and aggro alerts centre in the free gap
between the left frames and the right column.
