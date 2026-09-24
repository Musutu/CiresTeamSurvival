# Options and combat interface

Open **F9** for Controls, Interface, Video, Audio and System. **F10** edits HUD
positions and sizes; each panel has its own lock. These are local preferences,
independent of authoritative ability tuning and match rules.

The layout follows the MMO pattern of persistent player/party/target/focus
frames, a separate action area, and individually movable panels. Blizzard's
[HUD revamp description](https://news.blizzard.com/en-gb/article/23841481/world-of-warcraft-dragonflight-hud-and-ui-revamp)
describes editable HUD components and saved layouts. The split between character
controls/interface preferences and display/audio settings is also documented in
the [FFXIV configuration manual](https://eu.finalfantasyxiv.com/game_manual/config/).
The implementation and all artwork here are original; these references inform
organization and usability.

| Page | Working controls |
| --- | --- |
| Controls | Independent horizontal/vertical RMB sensitivity, invert Y, third-person distance, FOV, current key reference |
| Interface / Combat | Floating numbers, personal SCT, damage/healing/incoming/outgoing filters, critical markers, text sizes, combat log and meters |
| Interface / Tooltips | Enable tooltips, cursor/fixed/radial position, offset angle and distance with a lock, status filters and remaining duration |
| Interface / Chat | Player-chat visibility, font size/color, entry to panel layout editing |
| Video | Resolution, fullscreen/borderless/windowed, Unreal scalability preset, VSync, frame cap, bloom and motion blur |
| Audio | Master, combat SFX, interface feedback, mute, real UI sound test |
| System | FPS/frame time, PlayerState latency, local profile path, local-default reset |
| Developer (development standalone) | Validated live match/combat overrides, balance lab, local replay recording/browser; see `DeveloperTools.md` |

Display changes apply as a preview. Resolution, window mode, all scalability
levels, VSync and frame cap are restored after 15 seconds without confirmation,
or immediately when closing Options. The preview uses Unreal's resolution and
non-resolution application methods without saving; **Keep changes** confirms
and saves. It never asks a user to approve a permanent unreviewed mode change.

Combat audio components use Master × SFX; interface cues use Master × UI. Live
sliders affect active presentation voices. Their shared concurrency and distance
falloff are described in `SpellPresentation.md`. Epic's
[Sound Classes documentation](https://dev.epicgames.com/documentation/unreal-engine/sound-classes-in-unreal-engine)
provides the native grouping model for future soundtrack/ambience expansion.
The present implementation scales its existing components directly; it does not
pretend an unimplemented music channel is working.

Statuses are attached to the player, party, selected target and focus frames.
The display can show all statuses, buffs or debuffs, with a dispellable-only
filter. Icons have remaining seconds/minutes, a small dispellable mark and
explanations. Ground poison uses the remaining area lifetime and explicitly
requires leaving the area; overlapping areas remain independent. Permanent
learned passives have no countdown. Icon overflow uses a detailed +N tooltip.

Critical hits use a larger orange number, CRIT label and original starburst.
Normal outgoing damage is gold, incoming damage red, healing green, and avoided
attacks muted. Normal and critical amounts never aggregate into the same SCT
entry. Effective damage/healing and zero-damage avoidance still come from server
combat events. The target/focus frame shows NPC cast progress and its actual
victim; the interface does not invent client-side threat percentages.

Hover explanations cover player resources and stats, party targeting/focus,
unit health, statuses, abilities/empty slots, town purchases, match phase/lives,
minimap visibility, chat, meters, options and summon commands. Fixed tooltips use
the movable Tooltip panel; radial tooltips retain their saved angle/distance and
are clamped inside the viewport. Commandable summons have Follow, Move, Attack
and Hold controls. Move selects a subsequent ground click; autonomous summons
are shown without enabling unsupported commands.

`Saved/Config/CireUI.ini` schema 3 accepts versions 1 and 2, retains their saved
layout/preferences and supplies defaults for new controls. Nonfinite/out-of-range
profile values are sanitized. `CireOptions::RunSettingsSmoke` validates an isolated
profile's migration, roundtrip, locking and bounds. `-CireOptionsGallery` renders
thirteen deterministic menu/HUD/developer views to `Saved/OptionsGallery`, including
durations, an enemy cast, critical/miss/healing feedback and developer/lab/replay
pages, without saving to the player's profile. The gallery also exercises actual
developer Apply/Restore and isolated profile persistence. Key remapping, gamepad
navigation and named multi-profile sharing are
not implemented in this pass.
