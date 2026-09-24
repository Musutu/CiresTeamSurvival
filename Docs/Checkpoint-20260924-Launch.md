# September24,2026 — current testable prototype

## Latest change — adjustable tooltips, 01:16 UTC

User reported oversized tooltips and requested adjustable sizing. Implemented **F9 → Interface → Tooltips / status → Tooltip size (%)**: 60–140%, 5% steps, default 80%. Text, frame and spacing scale together; height fits the wrapped description and all three positioning modes clamp to the viewport. Fixed mode preserves its saved position/base width and uses content-driven height. Long unbroken words wrap. TooltipScale is an optional schema3 setting; existing preferences/layout are preserved, changes save once per step, and Reset restores 80%.

Normal editor build PASS, no suffixed module needed: the user's prior game had exited before compilation. Tooltip rendering PASS49 checks/six screenshots: `Saved/TooltipGalleryChecks/20260924T011504131173Z/report.json`. All six screenshots in `Saved/TooltipGallery/20260924-011515-741D37` visually reviewed by root and ground_effects: complete descriptions, no clipping at cursor/fixed/radial edge anchors. 60% is intentionally very compact. Options PASS14 captures with settings PASS33 and developer PASS15: `Saved/OptionsGalleryChecks/20260924T011504328275Z/report.json`; new slider visually reviewed in `Saved/OptionsGallery/20260924-011516/03_tooltips_status.png`. Test profiles were isolated from user preferences. All test children exited.

Tooltip gallery source: CireTooltipGallery.h/.cpp and Tools/RunTooltipGallery.py, development-only explicit CLI fixture. Existing renderer lives in CireOptions.cpp; preference storage in CireUISettings.h/.cpp. No paid generation or content imports during this fix.

Updated game reopened and verified at 01:17:08 UTC: visible responsive development window, Game Engine Initialized and CIRE MATCH READY, no Error/Fatal/Assertion/Ensure matches in startup log `Saved/Logs/CireTooltipPreview-20260924.log`. Uses ordinary `UnrealEditor-CiresTeamSurvival.dll`, Citadel map, Tripo champions, 1600x900 window. Leave this user session running. Inspect fresh process command lines before any future process action; never reuse saved process IDs.

## Previous environment and model pass

Read this before older checkpoints. Environment and compatible-model integration pass is tested and ready for the user-requested launch. It is a development prototype; the entire requested AA/AAA game is not complete.

## Saved work

- Expanded winding private PvE lanes with textured masonry/slate/timber buildings, world-space road/soil textures, visible town-defense landmark and actual-route minimap.
- Editable routes and Armored Escort rounds implemented and tested. Escorts ignore players, march to town and attack destructible obstructions. Route settings are in Content/Data/BattlefieldRoutes.json; server `cire.Routes reload` applies validated live waypoint/tuning edits.
- All18 new Tripo models imported; fourteen humanoid-compatible bodies integrated into sixteen champion profiles. Bear/Whisp/Centaur bodies and the future dragon form require custom motion. All original Warden/Ranger/Scholar packages preserved.
- Batch01 adds238 locomotion clips,14 BlendSpaces,14 attacks and14 copied material repairs. Separate reload verification passed;492 preexisting package hashes unchanged. Content/Data/ChampionArtBindings.json contains16 ready structural bindings.
- Weapon props follow profile attack style, so golems no longer inherit swords and Trolls no longer inherit bows. Missing specialty weapons are left absent pending authored props.
- F9→Developer→Effects has Load champion test kit for the five core champions, after enabling and applying overrides. Normal drafts still start empty. Six actives, one passive, one ultimate; passive-only final offer has four choices.
-22 profile roster, skills/effects/telegraphs, crit/miss/combat text, elite NPC threat/mechanics, summoned units/walls, Options, balance lab and native replay systems are present at prototype scope.

## Latest checks

- Final editor build PASS after weapon-style and developer-panel edits.
- Native expansion PASS: Saved/ExpansionChecks/20260924T001058916948Z/report.json. Includes206profile and31route assertions plus threat/projectile/construct/summon/NPC/settings/developer/replay API checks.
- Batch01 preflight/build/separate reload PASS: Saved/TripoBatchIntegration/Batch01.json. Offline pipeline13testsPASS, including Windows long-path protection and hyphenated vendor folders.
- All-profile rendered gallery PASS323checks/16profiles/40captures: Saved/BatchArtGalleryChecks/20260924T002337053778Z/report.json. Frames Saved/BatchArtGallery/20260924-002349-0F755D reviewed by root and two agents.
- Environment PASS42checks/four captures: Saved/EnvironmentGalleryChecks/20260924T001058662733Z/report.json. Town sign and soil visually reviewed.
- Options PASS14captures/settings and developer fixtures: Saved/OptionsGalleryChecks/20260924T002337280251Z/report.json. Test-kit button reviewed, no overlap.
- Earlier real replay15assertions and dedicated server/two-client network pass remain at Saved/ExpansionChecks/20260923T233047761054Z/report.json (whole older reportfalse due old lanefixture only; laternative reruns pass). Native draft rules209029 assertionsPASS.

## Important limits and next work

See Docs/Batch01-VisualReview.md for exact reviewed artifacts. Do not claim final art approval. Both Trolls have stray source-mesh geometry; Orc still looks too human; Footman/Dwarf have weapon/hand clipping. Specialty weapons, finger grips/IK, cloth/death/cast motion, body-sized collision, LODs and performance need production work. Large creatures still share prototype gameplay capsules. Four custom forms have no runtime art binding. The future dragon is not a separate current roster profile.

Only the first five champions have complete implemented thematic test kits; additional classes use the common draft pool and have planned bespoke skills. Existing effects are modeled prototype families, not final Niagara production work. Multiplayer has separate-server/two-client evidence, not ten-human matchmaking certification. Armored Escort and long-term scaling need broader playtesting. Exact Enfo fork/parity audit remains open.

Tripo receipts: Art/TripoImportBindings.json, exact UUID/folders,25 receipts including retained duplicates and one failed duplicate Bear transfer whose canonical import is saved. Unknown extra eldritch asset is retained unbound. User exported all models; root exported missing Summoner through actual Send To Unreal hover-menu destination. No regeneration or additional credits spent on imports. Manifests preserve prompts and recorded costs (2355 total across original3 and new18); stale account balance fields are historical.

All import/build/test helpers have exited. Before acting on any future process, inspect current command lines; never reuse a saved PID. Game launcher is PlayTripoPreview.cmd. The game runs through installed UE5.8.3 at F:/UE_5.8; workspace is this CiresTeamSurvival folder. No Git repository, original Documents project untouched, sources/ read-only.

## Launch status

Visible Unreal game process launched with /Game/Maps/Citadel, -game, -windowed,1600x900 and -CireTripoChampions. Log: Saved/Logs/CireUserPreview-20260924.log. Startup verification follows below; never use a stored PID for future process actions.

Verified01:00:39UTC: responsive visible window titled CiresTeamSurvival (64-bit Development PCD3D_SM6); Game Engine Initialized, Citadel loaded and CIRE MATCH READY logged with5v5/3clearedwaves/60prep/90arena/15recovery. No startup Error/Fatal/Assertion match. Leave this user play session running; no automatic test/cleanup process owns it.
