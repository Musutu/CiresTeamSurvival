# Validation record — September 23, 2026

## Current combat and Astra result — September 23, 21:47 UTC

- **Final checkpoint passed:** `Saved/Logs/CombatFinalBuild.log` records the successful final build. The presentation-only follow-up passed `Saved/CombatChecks/20260923T214627772253Z/report.json`; all nine1920×1080 captures at `Saved/CombatArtPreview/20260923-214639` were visually reviewed. Palm attachment offsets, release visibility and review lighting were improved. Bodies remain at human scale, equipment follows the hands, distinct attack phases return to idle, and all five labeled shapes render. Finger grips, idle carrying poses, some body/weapon overlap and cinematic-quality animation remain polish work. No Unreal test processes remain running.

- Unreal Editor build succeeded after the new combat, area and Astra loader integration. A vendor Tripo Bridge dependency warning and a deprecated projectile frequency setter warning are nonblocking.
- `Saved/CombatChecks/20260923T214104982273Z/report.json`: all three executed fixtures PASS. Features:36 checks, plus19 geometry checks and18 lifecycle checks; existing telemetry:56 assertions. Coverage includes exact probability thresholds/distributions, runtime feet heights, Lancer primary stats, miss/dodge meter behavior, moving-target projectile arrival, phase cancellation, cross-realm launch rejection and a real authored poison cast with mana/cooldown/damage/exit verification.
- Initial nine1920×1080 captures passed at `Saved/CombatArtPreview/20260923-214143`; their review prompted the bounded presentation follow-up above. These captures verify prototype presentation, not finished art acceptance.
- `Saved/InterfaceSmoke/20260923T214105276915Z/report.json`: dedicated server and both real remote clients PASS, all three exited0,42.8seconds. Realm privacy, selection, PBR models, chat, arena damage/healing and recovery pass. This existing network suite covers the three original bodies; it does not independently exercise every new projectile/area path under latency or packet loss.
- Astra full Node suite103/103 PASS; game importer4/4 PASS. The later material pin correction passed the focused5-test export suite. Browser generation flow was verified on an isolated local server, which was then stopped. The live47261 session/library were left intact; restarting it after saving is still needed to load updated backend imports.
- Five Astra recipes load in Unreal, and native ground material generation passed. Full browser cosmetic timelines remain exported data, not an installed Niagara or cosmetic playback system. Original Tripo/locomotion assets remain protected (127 hashes checked by the attack generator). No new paid model generation occurred.

See `CombatAbilities.md` for current behavior and limitations. The eight-learned-skill progression policy, packaged build, full ten-human match, threat/navigation, production VFX and exact Enfo parity remain open.

## Earlier result — Tripo playable preview, September 23, 20:47 UTC

- Editor build succeeded with CireChampionArt and CireArtPreview; launcher **PlayTripoPreview.cmd** enables `-CireTripoChampions`. Regular Play.cmd still uses mannequin art.
- Three original skeletal meshes and PBR materials are saved. BaseColorTex repair verified the actual saved assignments. Intended heights184/178/176cm and bone-derived facing are applied locally without changing gameplay capsules or replication.
- Initial Preview01 animations were rejected by rendered review: imported root scale100 was lost during retargeting; idle collapsed and locked locomotion moved far above the floor. **Preview02** repairs51 copied clips and3 copied BlendSpaces. Original mesh/skeleton/Preview01 hashes are unchanged. Independent reload verified1,224 raw/compressed, retarget on/off, unlocked/runtime-root-lock pose cases; maximum pelvis error<0.01cm. Evidence `Saved/TripoAnimationScaleVerification.json`.
- **Rendered preview PASS:** `Saved/ArtChecks/20260923T204554254422Z/report.json`. All five1920x1080 captures were visually inspected: front/back reference, idle, forward walk and forward jog. Characters remain visible at plausible scale/floor height with readable materials. Evaluated bone/bounds, floor/screen checks and resolution checks passed. Images: `Saved/ArtPreview/20260923-204604-39C9B9E3`.
- **Tripo multiplayer probe PASS:** `Saved/InterfaceSmoke/20260923T204554256467Z/report.json`; server and both clients exited0. All five allied heroes on each client cover all three models with matching animation skeletons, capsule/feet setup and original PBR materials. Selection overlays restore correctly; opposing PvE heroes remain absent, arena damage/healing and recovery privacy still pass.
- The first art network probe was a readiness race before newly replicated allies' first visual tick. The fixture now has a bounded8-second readiness wait and retains all checks. A first negative animation diagnostic correctly recorded FAIL even though Unreal exited0; `Tools/RunArtPreview.py` now requires the explicit PASS marker plus five valid full-resolution PNGs.
- The exact three overlapping Bridge origin actors are retained as editor-only, hidden-in-game, noncolliding references. Original map backup: `Art/Imports/BridgeOriginalMap/Citadel.umap`; report `Saved/TripoMapStaging.json`.
- This certifies a **prototype locomotion preview**, not production art: attack/cast/death animations, full moving-camera/foot-slide/cloth/twist review, LODs, packaged builds and ten-human load testing remain.

## Combat feedback result — September 23

- **Latest Unreal Editor build succeeded**, including compilation of the **Tripo Bridge 1.0.5** plugin source. That earlier build established plugin compatibility; the newer art review above records successful imports and prototype validation.
- **Combat telemetry: 56 assertions passed.** `Saved/Logs/CombatFeedbackTelemetry.log` records `CIRE_TELEMETRY_PASS assertions=56 damage=149 healing=65`. Coverage includes effective damage/healing, mitigation, overkill/overheal exclusion, four ultimate casts and their costs/cooldowns, phase/range restrictions, active/ultimate slot mapping, lethal-event identity and impact position, recipient privacy, and bounded event retention that protects personal hits from teammate traffic.
- **UI persistence: Success.** `Saved/Logs/CombatFeedbackSettings.log` records a successful `Cire.UI.Settings.Persistence` automation test.
- **Final dedicated-server/two-client probe passed in 42.39 seconds**, with server and both clients exiting **0**. Exact evidence: `Saved/InterfaceSmoke/20260923T195842450035Z/report.json`. Verified own-team five-hero replication with no opposing PvE heroes/monsters; private Party chat and shared All chat; self/ally selection, invalid-null preservation, explicit ground clearing, and arena enemy targeting; local-only collision-free selection rings and overlay restoration; the separate selection trace channel without changing combat visibility; actual arena damage/healing events and authoritative meter totals; opponent removal and target rejection during recovery; and privacy on the next survival cycle.
- The same network probe verified an **actual 13-damage killing blow on a monster destroyed before its first replication frame**. Its own team received stable names/IDs, personal attribution, sequence, and the head-position snapshot with no actor references. The opposing PvE team did not receive it. No error, fatal, assertion, or ensure messages appeared in the final three probe logs. The harness used loopback and all of its child processes ended.
- **Rendered feedback preview passed 22 checks and saved six screenshots** under `Saved/FeedbackPreview/20260923-195448-B177EB4E/`. Four screenshots were visually reviewed: three individual **70** hits with an anchored **210 / 3 targets** summary; **150 healing** in both displays; **45 incoming damage** in both displays; and **137 lethal damage** remaining visible after despawn. The other two screenshots were captured but are not claimed as visually reviewed.
- An earlier network failure was a **fixture error**: it assumed the requested spawn height instead of Unreal's floor-adjusted position. Diagnostics showed the server and client agreed on head Z **200.150**, while the fixture expected **218**. The fixture now independently replicates and compares the actual pre-impact expectation; the final run above passed without changing the event-delivery backend to accommodate the test.

**Still unresolved:** the full eight-learned-skill draft policy and the native rules regression from the earlier work. The historical 46,472-assertion result below predates those rule changes and does not validate the current draft policy. Current combat and interface checks do not certify full ten-client gameplay, production art, or Enfo feature parity.

The following sections preserve earlier evidence. In particular, the original timed-survival and six-skill descriptions are historical; current phase testing follows **Survival → Prep → Arena → Recovery → Survival**.

## Historical — initial prototype

- Installed Unreal Engine build is **5.8.3**, changelist 58210709.
- Microsoft C++ build tools and required SDKs installed after the user's Windows administrator approval. MSVC actual product version **14.44.35229**; Windows SDK **10.0.22621.0**; .NET Framework SDK **4.8**.
- Native rules suite completed **46,472 assertions, zero failures**, compiled with MSVC C++17 `/W4 /WX /permissive-`. A subsequent executable launch from the reusable runner was blocked by Windows Device Guard; no bypass was attempted. The earlier pass is recorded, and this later environment limitation remains.
- UE game target compiled successfully; UE editor target compiled successfully after gameplay, replication, camera, world-boundary, and UI corrections.
- Content commandlet generated Citadel and six materials successfully. A rendered test exposed missing InstancedStaticMeshes usage flags; all six material assets were regenerated with the flags explicitly verified.
- Headless engine smoke: **CIRE_SMOKE_PASS heroes=10 round=2 phase=0**, exit 0. This uses accelerated 6/3/5-second phases to verify transition plumbing; normal play still uses 300/60/90 seconds.
- Rendered 1280×720 playtest: champion selection worked; INT champion showed **20 INT / 600 mana**, **10 STR / 250 HP**; waves and bots fought and awarded team XP/gold; queued level choices appeared; selecting Stone Skin produced a subsequent four-active-only offer.
- Corrected a spawn-camera obstruction by the base portal, a late-wave spawn beyond the floor, remote-client hostile selection using server-only state, and disconnect handling that would have destroyed player progression.
- **Separate dedicated server / remote-client probe passed**, both exit 0 in 23.72 seconds. Verified remote movement (319.6 cm client snapshot, 332.2 cm server after deceleration), replicated INT draft and stats, hostile targeting, illegal shop/cast/draft rejection, and preserving the same champion on disconnect with ten heroes split 5/5. This is one client plus bots, not ten human clients.
- Procedural floor components now have stable network names. Unsupported component warnings were eliminated; one initial movement-base-not-yet-resolved warning remains on connection, followed by successful replicated movement.
- Final rendered material run had no material usage warning or checkerboard fallback. The solo draft now waits for the user's selection before starting the match clock.

## Historical — first UI, combat telemetry, and realm isolation update

Verified on the September 23 editor build used at **19:06 UTC**, before the final town-zone additions:

- **Two real remote clients plus a dedicated server passed**, with all three processes exiting **0** in **35.16 seconds**. Report: `Saved/InterfaceSmoke/20260923T190637572697Z/report.json`. The harness binds only to loopback and cleans up only its own child processes.
- Each opposing client received its own five heroes and **zero opposing heroes or monsters during PvE**. Party chat stayed with its team; All chat reached both clients. The server rejected opposing-team selection and damage outside the arena.
- In the arena, each client received five opponents and completed an enemy-target RPC round trip. Both received the actual **17-damage / 9-healing** events and corresponding authoritative meter totals.
- Recovery removed opposing actors from both clients and rejected attempts to select the prior enemy. Returning to Survival preserved realm privacy. The tested sequence was **Survival → Prep → Arena → Recovery → Survival**; the older timed-survival smoke above records the earlier prototype only.
- **Combat telemetry smoke passed 25 assertions**, including effective damage after mitigation, overkill exclusion, effective healing with the healing passive, overheal exclusion, phase restrictions, and private PvE event routing. `Saved/Logs/TelemetryTest.log` records `CIRE_TELEMETRY_PASS assertions=25 damage=149 healing=65`.
- No error, fatal, assertion, or ensure messages were found in the three interface-probe logs. This remains two human-equivalent client processes with bots, not a full ten-client load test; the test does not simulate packet loss.

Reproduce the interface test with `Tools/RunInterfaceSmoke.py` after building; `--arena <id>` pins the server's arena pick through the development flag `-CireArena=<id>`. The runner's probe budget starts once both clients have joined (joining is bounded by `--startup-timeout`), so slow client boots on a busy machine no longer eat into it.

The 2026-09-26 prep→arena stall (clients stuck waiting for the arena after `CHAT_PASS`) was a dropped `CIRE_PROBE_ACK_PREP`: the first prep validated the arena rotation inline (~0.8 s path-grid walk on the 2x arenas), world time advances at most 0.4 s per frame, and the chat spam guard measured 0.75 s in world time, so an ack sent 1.16 s later read as 0.74 s and was silently discarded. The rotation is now validated at match start, and the chat guard uses real time with a three-line burst allowance. The telemetry fixture runs with the development flag `-CireTelemetryProbe`. Both fixtures are disabled in shipping builds.

## Historical evidence files

- `Saved/Logs/CireSmoke.log`: phase and smoke pass record.
- `Saved/Logs/CirePlaytest2.log`: live gameplay and original material flag finding.
- `Saved/Logs/CireFinalVisual.log`: final material/UI visual run.
- `Saved/NetworkSmoke/20260923T124107686687Z/report.json`: final passing separate network probe and exact log evidence.

## Limits

This does not certify a full ten-human match, matchmaking, reconnect persistence, latency/packet-loss behavior, anti-cheat, packaged distribution, minimum-hardware performance, crash-free long sessions, or Enfo parity. Damage schools/resistances, navigation, animations, finished art, sound, and VFX need production work. Current combat effects are debug geometry.

The Tripo hero was generated and rigged in the account. Tripo Bridge now compiles with this project, but a completed Tripo asset import has not been validated. No claim is made that the generated hero's skeleton, deformation, materials, LODs, or animation work in Unreal yet.

The visual test and accelerated smoke are complementary: neither substitutes for a full normal-duration competitive playtest and measured balance work.
