# Rendered presentation validation

Initial engine render on 2026-09-23 UTC passed both bounded fixtures at
1920×1080. All fourteen images were opened and inspected. These captures show
the first procedural native VFX pass, not finished cinematic effects or final
champion art. The options/HUD fixture intentionally uses temporary characters.

Initial spell report: `Saved/SpellGalleryChecks/20260923T224520491852Z/report.json`
(34.0 seconds, 6/6 images, native 21-skill mesh/material smoke passed).

| Image | Initial capture |
| --- | --- |
| Nine primary combat cues | `Saved/SpellGallery/20260923-224531/01_modeled_skills.png` |
| Healing, ultimates, summons | `Saved/SpellGallery/20260923-224531/02_modeled_skills.png` |
| Ground and NPC cast cues | `Saved/SpellGallery/20260923-224531/03_modeled_skills.png` |
| Five actual shape telegraphs | `Saved/SpellGallery/20260923-224531/04_telegraphs.png` |
| Five active ground shapes | `Saved/SpellGallery/20260923-224531/05_ground_active.png` |
| Projectile, impact, crit, wall and protection | `Saved/SpellGallery/20260923-224531/06_modeled_skills.png` |

Initial options report: `Saved/OptionsGalleryChecks/20260923T224602118876Z/report.json`
(41.08 seconds, 8/8 images, schema-3 persistence/migration checks passed).

| Image | Initial capture |
| --- | --- |
| Camera and key reference | `Saved/OptionsGallery/20260923-224612/01_controls.png` |
| Combat text settings | `Saved/OptionsGallery/20260923-224612/02_combat_text.png` |
| Tooltip and status settings | `Saved/OptionsGallery/20260923-224612/03_tooltips_status.png` |
| Chat and HUD layout | `Saved/OptionsGallery/20260923-224612/04_chat_layout.png` |
| Video preview controls | `Saved/OptionsGallery/20260923-224612/05_video.png` |
| Audio controls | `Saved/OptionsGallery/20260923-224612/06_audio.png` |
| Diagnostics and profile | `Saved/OptionsGallery/20260923-224612/07_system.png` |
| Status, cast, aggro, critical, miss and heal | `Saved/OptionsGallery/20260923-224612/08_status_critical_hud.png` |

Review found two problems: floating numbers could cover target/match frame text,
and the ground-gallery specimens inherited the generic green color. Source fixes
reserve all visible HUD panel rectangles during floating-number placement and
assign the fixture's intended per-family palette. A subsequent rebuild and
recapture must validate those corrections. The expanded options fixture includes
five additional developer/lab/replay pages and actual developer state/profile
checks; its new expected count is thirteen.

All menu captions in the initial screenshots fit their controls. Player, party,
target and focus statuses include duration text; target cast progress and real
victim information render. Critical amounts, normal hits, missed hits and heals
appear in both floating and personal scrolling lanes. Gameplay replay fidelity,
live display-mode preview/reversion and perceptual audio quality require their
own runtime checks; screenshots alone do not establish them.
