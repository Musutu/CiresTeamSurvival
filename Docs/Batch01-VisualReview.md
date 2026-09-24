# Tripo Batch01 — prototype review

Reviewed September24,2026UTC. Accepted for interactive prototype testing with the limitations below. This is not final production animation, rig, or identity approval.

## Evidence

- `Saved/TripoBatchIntegration/Batch01.json`: separate-process reload verification, status ready, errors empty; 14 new copied bodies,238 corrected locomotion clips,14 BlendSpaces,14 attacks and14 copied material repairs. All492 preexisting package hashes unchanged.
- `Saved/BatchArtGalleryChecks/20260924T002337053778Z/report.json`: PASS323 checks,16 profiles,40 captures,1920x1080. Exact mesh/clip/skeleton links, root scale, height, finite framed poses, movement blend inputs and attack/recovery checks passed.
- `Saved/BatchArtGallery/20260924-002349-0F755D`: root reviewed Lancer/Summoner idle,walk,windup,release; humanoid reviewer inspected all25 PNGs across pages02,03,05,06,08. Creature reviewer inspected the relevant remaining profiles. Screenshots are sampled poses, not a complete continuous-animation or foot-plant assessment.
- `Saved/EnvironmentGalleryChecks/20260924T001058662733Z/report.json`: PASS42 collision/route checks plus four1080p captures; improved town sign/framing and soil reviewed.
- `Saved/OptionsGalleryChecks/20260924T002337280251Z/report.json`: PASS14 captures and settings/developer fixtures. Root inspected the new Effects-page test-kit button; no overlap with Apply row.

## Accepted scope

New Lancer and Summoner bodies replace their temporary Ranger/Scholar art. Other new humanoid-compatible bodies map to the Paladin variants, Dwarf, Orc, Wizard, Keeper, Drakish Footman, three Golems, Behemoth, Dryad and Troll variants. Original Warden/Ranger/Scholar assets remain unchanged.

Bodies have visible colored materials and recognisable silhouettes. The reviewed poses show no collapsed rigs, detached limbs, or catastrophic skinning. Grounding and facing are suitable for the current prototype. Attack animations are generic articulated motion families, not bespoke finished class performances.

## Known limitations

- Lancer's hand is open around the lance; grip/weapon alignment needs authored fingers and IK. The lance is still a simple prototype prop.
- Footman's fingers intersect the shield face in attack poses; sword motion enters shield space. Dwarf hand overlaps beard/chest during release. These need pose/attachment refinement.
- Orc body still reads as a tan human; facial/tusk/skin identity needs correction.
- Both Troll variants have stray axe-like skinned geometry near wrists/ankles. They remain explicitly unaccepted for final art and need source-mesh cleanup. Their generic melee sweep also needs a proper ranged throwing variant.
- Behemoth trunk remains rigid; heavy-creature attacks need weight and bespoke motion. Golem material roughness/color needs further lighting review; Bruiser cracks do not yet animate as felflame.
- Coats, armor shoulders and creature proportions require continuous-motion, cloth and joint-weight review. Screenshots cannot establish good foot planting across all movement speeds.
- Paladin flails, Dwarf pick/axes, Orc/Troll axes, Keeper/Dryad staves and Behemoth totem props remain to be authored. Unsupported weapon styles intentionally do not receive a mismatched sword or bow.
- Large bodies retain the prototype hero gameplay capsule; final collision, target markers and camera composition must be tuned by body size before competitive balancing.
- Bear, Whisp and Centaur retain fallback runtime bodies pending custom motion integration. The dragon asset is retained for a future transformation form, not a separate current profile.
- All22 profiles have playable stat/role/basic-attack foundations and share the draft pool; only the five core champions currently have complete implemented thematic test kits. Additional bespoke class skills remain planned.

Use `PlayTripoPreview.cmd` for these bindings. F9→Developer→Effects→Load champion test kit is available for the five core champions after enabling and applying overrides. Normal matches still begin with no learned skills.
