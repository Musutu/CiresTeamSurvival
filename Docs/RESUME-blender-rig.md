# RESUME: feat/blender-rig (Blender wave 2)

Worktree `F:\CiresTeamSurvival-agents\cts-blender-rig`, test ports 17440-17449.

**Branch state**
* The last build succeeded (`Saved/AgentLogs/build-16.log`).
* The gates passed at commit 4229671 (item 1): CIRE_COMBAT_EXPANSION_PASS with CIRE_GRIP_PASS checks=918,
  CIRE_NET_SERVER_PASS, and CIRE_INTERFACE_SMOKE_PASS.
* The WIP commit after it (Inquisitor re-bind, audit side/time) has **not** been gated.

**Local-only change.** `Content/FabDerived` in this worktree is now a real local copy (853 MB, gitignored), not the
junction into main. I made it so retargets never write into main.

## Item status

1. **Ranger straighten: DONE (4229671).**
   * Re-bound in Blender and re-imported in place. The ChampionAttacks02/HQRanger clips were re-transferred: 0
     backwards elbows in 15 clips.
   * The crossbow preview plays `attack_crossbow`, and carried stock props rest muzzle-forward.
   * **Quiver: NOT done.** It is fused into the body island and the UVs have 2,756 islands, so it needs a mask (Tripo
     segmentation parts 7+16, or a hand selection). With a mask, split it with `Tools/Blender/parts.py --split`.
     The 90k decimate is moot: the body is already 92.6k tris.
2. **High Inquisitor: WIP.**
   * The mesh is re-bound (canonical straight pose, `--frame feet`) and re-imported in place (committed).
   * I re-ran its Fab set retarget in this worktree's local FabDerived. The audit then shows **no backwards elbows in
     the `A_FallenHighInquisitorSet_*` clips** (121 → 25 samples).
   * Still flagged (right arm):
     * the body's own Tripo clips `hit_to_body_01` and `cast_a_spell` (not used by the game);
     * the coverage clips `A_FallenHighInquisitor_monster_war_cry` (-165) and `ground_slam` (-30), which are used.
   * Next steps:
     * Capture the Inquisitor (`RunMonsterGallery.py --only face_fallen_high_inquisitor` and `close_...`) and check
       the staff arm visually.
     * For war_cry/ground_slam, the `-CireFabAnimMonsters` retarget did not improve them. **Warning:** that mode
       rewrites `Content/Data/MonsterFabClips.json` down to one body (483 lines lost). I reverted it. Restore the file
       from git after any `-CireFabAnimMonsters -CireFabAnimOnly=` run, or fix the tool to merge.
     * **After merge, main must re-run:** `RetargetFabAnimations.py -CireFabAnimMonsterSets
       -CireFabAnimOnly=FallenHighInquisitor`. main's FabDerived still holds the clips from the old bind. They look the
       same as before (skinning is invariant under the re-bind), so they are no worse, but they are not fixed.
3. **Monster weapons 2x: NOT started.**
   * Apply the `WeaponLoadouts.json` sizeClasses rule to monster/creature props: `CireMonsterArt.cpp` (the prop loop
     around `CireGrip::Place`, which has `Size`/`Body.MeshScale`) and `CireCreatureArt.cpp`.
   * Classify props by grip data (one-hander/mace = no offHand, not a shield, not carry); scale 2x, girth .8, cap 3/4 of
     the body; stretch with `CireWeapons::HandleStretch`.
   * Verify with `RunMonsterGallery.py --only grips_` (12 stages) and add checks to `CireGripTests` (monster section).
4. **drowned_deep tentacle pilot: NOT started.**
   * Bodies are exported: `Saved/Blender/export/CTS_Race_{MindLeech,MawOfTheDeep,AbyssalStalker,Tidecaller}.fbx`.
   * The round-trip is proven, so write `Tools/Blender/add_chain.py`: add a bone chain under head/spine for the
     tentacle region (MonsterArt.json `sway.regions` give centre/radii), weight the region with automatic weights on
     the new bones only, and export.
   * Import with `ImportFromBlender.py`. New bones mean the skeleton gains bones: import onto the same Skeleton with
     `-CireImportUpdateRef` and check that the Fab clips still play.
   * Drive the new bones from the in-engine sway in `CireMonsterAnim` instead of the WPO skin sway.
5. **Fab clip hyperextension: NOT started.**
   * Mild (-13..-26 deg) in a few Fab sword_shield/spell clips: AetheriBulwark, BastionGolem, VoidbornHerald,
     DrakkariWingshot, HollowSiegebreaker.
   * Cleanest fix: a runtime elbow guard in the monster anim. If CireRigAudit elbow < -12, re-solve with
     `CireGrip` TwoBoneIK (anatomical pole) keeping the hand.
   * Legacy fallback bodies' `A_<body>_Attack` clips: skip unless cheap.

## Commands and tools

Use the UE python for everything: `F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe`.

* **Export from UE** (needs the full editor, about 3 min):
  `UnrealEditor.exe <uproject> -ExecutePythonScript=<abs>/Tools/ExportForBlender.py -RenderOffscreen -unattended -CireExport=<asset,...>`
  → `Saved/Blender/export`.
* **Re-bind:** `Tools/RunBlender.py rebind.py -- --in X.fbx --out Y.fbx [--ref Saved/Blender/ref/<rig>.json] [--frame feet] --report r.json`
  (refs are copied MonsterRigPoints JSONs).
* **QA renders:** `Tools/RunBlender.py qa_turnaround.py -- --in X.fbx --out DIR`.
* **Parts:** `Tools/RunBlender.py parts.py -- --in X.fbx --out parts.json [--split ids --split-out --body-out --decimate-tris N]`.
* **Import onto the existing skeleton** (commandlet):
  `-run=pythonscript -script=<abs>/Tools/ImportFromBlender.py -CireImport="<fbx>|<dest mesh>|<skeleton>" [-CireImportUpdateRef] [-CireImportMaterials=..]`
  * A dest under `/Game/_BlenderRoundTrip` is a dry run. Delete that folder afterwards.
* **Audit:** `Tools/RunRigAudit.py [--only substr] [--meshes <paths>]`.
* **Galleries:** `Tools/RunGripGallery.py --all --pylib Saved/pylib [--only ..] [--preset ranger:ranger_crossbow] --out-copy DIR`;
  `Tools/RunMonsterGallery.py --only grips_|face_<unit>|close_<unit>+...`.
* **Champion clip re-transfer:** `RetargetChampionAttacks.py -CireChampionAttacksOnly=<Folder>`.
* **Monster Fab set:** `RetargetFabAnimations.py -CireFabAnimMonsterSets -CireFabAnimOnly=<Variant>`.
* **Gates:**
  * `RunExpansionChecks.py --only native --timeout 240 --port 17441`
  * `RunNetworkSmoke.py --port 17443 --startup-timeout 120 --probe-timeout 90`
  * `RunInterfaceSmoke.py --port 17445 --startup-timeout 120 --probe-timeout 120` (no `--tripo-champions`)
