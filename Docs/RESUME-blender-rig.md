# RESUME: feat/blender-rig (Blender wave 2)

Worktree `F:\CiresTeamSurvival-agents\cts-blender-rig`, test ports 17610-17619 (wave 2026-09-27; earlier runs used 17440-17449).

**Branch state (2026-09-28)**
* Latest main merged (clean, no conflicts); build `Saved/AgentLogs/build-17.log` right after the merge.
* Items 2-6 below are done. Gate logs: see "Gates" at the end.

**Local-only change.** `Content/FabDerived` in this worktree is a real local copy (853 MB, gitignored), not the junction
into main, so retargets never write into main.

## Item status

1. **Ranger straighten: DONE (4229671).** Re-bound in Blender and re-imported in place; 0 backwards elbows in 15 clips.
2. **High Inquisitor: DONE.**
   * The mesh is re-bound (canonical straight pose, `--frame feet`) and re-imported in place (committed earlier).
   * Its `A_FallenHighInquisitorSet_*` clips were re-retargeted in this worktree's local FabDerived: no backwards elbows.
   * **war_cry / ground_slam fix.** The Gun & Sword coverage clips (`A_FallenHighInquisitor_monster_war_cry`,
     `_ground_slam`) bend the staff arm backwards however they are retargeted. `MonsterFabClips.json` now aliases them to
     the body's own set clips: `"war_cry": "role:shout"`, `"ground_slam": "role:heavy2"` (spell_shout / spell_skill2).
     `CireMonsterArt` resolves `role:<role>` values. The body's own Tripo `war_cry` clip (audit-clean) still wins for
     `war_cry` (named Tripo clips always did); `ground_slam` (self circles) now plays spell_skill2.
   * `RetargetFabAnimations.py -CireFabAnimMonsters` no longer rewrites the file down to the retargeted bodies: it merges
     per variant, keeps `set`/`roles`/speeds and `role:` aliases, and keeps the set clip windows.
   * Test: `CireMonsterArtTests` checks the Inquisitor's war_cry/ground_slam never resolve to a `_monster_` coverage clip.
   * **Main must still re-run (not done by me; I never touched F:\CiresTeamSurvival):**
     `RetargetFabAnimations.py -CireFabAnimMonsterSets -CireFabAnimOnly=FallenHighInquisitor`. main's FabDerived holds the
     set clips from the old bind; they look the same as before (skinning is invariant under the re-bind) but are not fixed.
     The war_cry/ground_slam fix needs no retarget (data alias).
3. **Monster weapons 2x: DONE.**
   * `CireWeapons::HeldSizeClass` applies `WeaponLoadouts.json` `sizeClasses` to monster/creature held props: a one-hander
     (grip data, no second grip, not shield/ammo/carry/two-hand) is `mace` (Mace/Hammer/Flail/Club by mesh name) or
     `one_hand`: 2x long, girth .8 (handle stretch about the handle point), capped at 3/4 of the body height, never below
     the authored size. A prop that reaches .65 of the body is carried upright at rest like the champions'.
   * `CireMonsterArt.cpp` (prop loop) and `CireCreatureArt.cpp` (`AttachProps`, riders). Kill switch:
     `cire.Monsters.PropSizeClass 0`. Tags on the part: `CireSizeClass_<class>`, `CireSizeBase_<base*1000>`.
   * Tests: `CireGripTests` monster section (grew >1.15x, capped near 3/4 body, at least one sized prop);
     `CireMonsterArtTests` prop scale check accepts sized props (authored..2x).
4. **drowned_deep tentacle pilot: DONE (Tidecaller).**
   * New `Tools/Blender/add_chain.py`: adds `tentacle_<chain>_<bone>` chains under `head`, weights the region onto them
     only (existing weights scaled, never redistributed). The UE raw region is mapped into Blender by a least-squares fit
     of the ref bind points (fit error 7e-5 on 60 bones). `--box` selects a column (a beard hanging past the sway region).
   * Tidecaller: 3 chains x 4 bones, `--region 1,6,79,7,7,7,80,59 --box=-5.5,7.5,1.5,12`, 2,773 verts weighted.
     QA: `Saved/Blender/qa/tidecaller_tentacles` (bend_face/bend_side at 12 deg/bone, 4x the in-game amplitude: clean;
     a few necklace beads in front of the beard follow slightly).
   * Imported in place with `-CireImportUpdateRef` (73 bones). `ImportFromBlender.py` now also saves the Skeleton asset
     (merged bones) and logs `CIRE_IMPORT_BONES`.
   * Runtime: `CireMonsterAnim` waves the tentacle bones (travelling wave, chains out of phase) from the body's first
     sway region (speed/wave from MonsterArt.json; tip travel ~ amount), and that region leaves the skin-material sway.
     Kill switch `cire.Monsters.TentacleBones 0`. Fab clips don't key the new bones (they stay at ref + wave).
   * Tests: `CireMonsterArtTests` (12 tentacle bones, wave 0.5-15 deg/bone, region removed from skin sway, tip finite).
   * Next bodies: MindLeech skirt / MawOfTheDeep ring need per-tentacle chains (connected-component detection; the
     Tripo meshes split at UV seams, weld first). Command pattern is in add_chain.py's docstring.
5. **Fab clip hyperextension: DONE (runtime guard).**
   * `CireGrip::GuardElbows`: an elbow below -8 deg (CireRigAudit sign) is re-solved by the two-bone IK (anatomical pole)
     keeping the hand, blended in to full at -16 deg (no pop). Runs in `CireMonsterAnim` before the grip IK. Anterior per
     arm from `CireRigAudit::AnteriorLocal` at body apply. Kill switch `cire.Monsters.ElbowGuard 0`.
   * Test: `CireMonsterArtTests` checks every sampled walk/run/attack/hit/death pose of every humanoid monster body for
     elbows >= -12 deg.
   * Legacy fallback `A_<body>_Attack` clips: covered by the same runtime guard.
6. **Ranger quiver: DONE in Blender (no Tripo, 0 credits).**
   * Instead of cutting the quiver out (hole + new prop asset), new `Tools/Blender/rigid_part.py` re-weights the fused
     quiver 100% to `spine_03`, feathered into the skin, so it no longer bends/twists with the shoulder and arm.
     Selection: a capsule in the body's (lateral, height) frame plus a disc for the arrows, outer layer only (depth test).
   * Ranger: `--segment=.10,.87,.01,.675 --radius .028 --extra=.10,.95,.045`, 4,747 rigid verts. QA (painted selection +
     spine twist + raised arm): `Saved/Blender/qa/ranger_quiver` (sel_*/twist_*).
   * Imported in place onto the Ranger's skeleton (bones unchanged).

## Assumptions / questions for Eric

* Monster weapon 2x uses the same sizeClasses numbers as the champions (one knob). Mace vs one_hand is by mesh name.
* Big monster one-handers (>= .65 of the body) rest carried upright like the champions' 1.2 m blades.
* Tentacle wave amplitude follows the old skin-sway `amount` (subtle). Raise `amount` in MonsterArt.json to taste.
* The quiver is rigid on spine_03 rather than split out; if you want it removable (e.g. hidden on some skins), the Tripo
  part masks (7 + 16 on 2b9fbccc) are still the clean cut.

## Shared-file edits

* `Content/Data/MonsterFabClips.json`: FallenHighInquisitor war_cry/ground_slam -> `role:` aliases (2 lines).
* `CireMonsterArt.cpp` (role alias, prop size class, tentacle + elbow guard setup, 3 cvars), `CireCreatureArt.cpp`
  (AttachProps size class), `CireWeaponPresentation.cpp/.h` (HeldSizeClass), `CireGrip.cpp/.h` (GuardElbows),
  `CireMonsterAnim.cpp/.h` (tentacle wave, elbow guard), tests in `CireMonsterArtTests.cpp`, `CireGripTests.cpp`.
* Tools: `RetargetFabAnimations.py` (monsters mode merges), `ImportFromBlender.py` (saves skeleton),
  `Blender/qa_turnaround.py` (`--bend`), new `Blender/add_chain.py`, `Blender/rigid_part.py`.
* Assets (git, not Fab): `Content/Tripo/Races/drowned_deep/Tidecaller/CTS_Race_Tidecaller(.uasset|_Skeleton.uasset)`,
  `Content/Tripo/ChampionsHQ/Ranger/CTS_ChampHQ_Ranger(.uasset|_Skeleton.uasset)`.

## Commands and tools

Use the UE python for everything: `F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe`.

* **Export from UE** (full editor, ~3 min):
  `UnrealEditor.exe <uproject> -ExecutePythonScript=<abs>/Tools/ExportForBlender.py -RenderOffscreen -unattended -CireExport=<asset,...>`
* **Re-bind:** `Tools/RunBlender.py rebind.py -- --in X.fbx --out Y.fbx [--ref Saved/Blender/ref/<rig>.json] [--frame feet] --report r.json`
* **Tentacle chain:** `Tools/RunBlender.py add_chain.py -- --in X.fbx --out Y.fbx --ref Saved/Blender/ref/<body>.json --region cx,cy,cz,rx,ry,rz,rootZ,tipZ [--box=xmin,xmax,ymin,ymax] --chains 3 --bones 4`
  (negative first values need `--arg=` syntax).
* **Rigid prop:** `Tools/RunBlender.py rigid_part.py -- --in X.fbx --out Y.fbx --bone spine_03 --segment=l1,z1,l2,z2 --radius R [--extra=l,z,r] --qa DIR`
* **QA renders:** `Tools/RunBlender.py qa_turnaround.py -- --in X.fbx --out DIR [--bend tentacle:12]`.
* **Parts:** `Tools/RunBlender.py parts.py -- --in X.fbx --out parts.json [--split ids --split-out --body-out --decimate-tris N]`.
* **Import onto the existing skeleton** (commandlet, `UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs>/Tools/ImportFromBlender.py -unattended -nullrhi -nosound`):
  `-CireImport="<fbx>|<dest mesh>|<skeleton>" [-CireImportUpdateRef] [-CireImportMaterials=..]`. A dest under
  `/Game/_BlenderRoundTrip` is a dry run.
* **Audit:** `Tools/RunRigAudit.py [--only substr] [--meshes <paths>]`.
* **Galleries:** `Tools/RunMonsterGallery.py --only grips_|face_<unit>|close_<unit>+...`.
* **Monster Fab set:** `RetargetFabAnimations.py -CireFabAnimMonsterSets -CireFabAnimOnly=<Variant>`.
* **Gates:** `RunExpansionChecks.py --only native --timeout 240 --port 17611`;
  `RunNetworkSmoke.py --port 17613 --startup-timeout 120 --probe-timeout 90`;
  `RunInterfaceSmoke.py --port 17615 --startup-timeout 120 --probe-timeout 120`.

## Gates

(filled in below as they run)
