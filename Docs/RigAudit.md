# Rig audit: backwards elbows, reversed hands, weapon direction and size (feat/blender-rig)

Eric's playtest complaints (2026-09-26):
* "the models' joints being backwards, hands being backwards"
* "they bend backwards at the elbow"
* "sometimes the weapon isn't the right direction"
* "weapons are so tiny in the hands of some of the models"
* "weapon sizes increased 2x for 1-handed weapons and maces"

## How it is measured

**`CireRigAudit`** (`Source/CiresTeamSurvival/CireRigAudit.*`) measures each arm of an evaluated pose.
* **Elbow:** the signed flexion of the forearm in the upper arm's frame. The upper arm's anatomical front comes from its
  bind pose:
  * a bent bind arm is its own hinge (Tripo monsters are modelled holding weapons);
  * a straight bind arm uses the body's front (foot to ball).
  * Positive values bend forward, which is natural. Below -12 degrees the elbow bends backwards, and that is a defect.
* **Twist:** the hand's rotation about its own axis relative to the upper arm, measured from the bind pose. It is
  reported, not failed, because pronation reaches ±180 in sound clips.

Where it runs:
* **`Tools/RunGripGallery.py --all`** poses every champion in 6 states: idle, run, windup, contact, cast, roll.
  * It captures wide, hand and a new **arms** view (upper body from the front).
  * The metric lines carry `elbowL/R`, `twistL/R`, `thumbFwdL/R`, `armDefects`, the weapon's tip direction
    (`tipF/tipR/tipU`), `size/base/girth/class` and `handleGap`.
  * The metrics include our procedural layers: grip IK, carry IK and second-hand IK.
* **`Tools/RunRigAudit.py`** (`-CireRigAudit`) sweeps every skeletal body referenced by `Content/Data/*.json`.
  * It covers 143 bodies: 47 champion, 15 NPC/vendor and 81 monster.
  * Each body is swept over every clip on its skeleton, 12 samples per clip.
  * Output goes to `Saved/RigAudit/<stamp>.json` plus a summary per category.
  * This is the raw clip only, without the procedural layers.

## Diagnosis (body → defect → cause → fix)

| Body | Defect (before) | Cause | Fix | After |
| --- | --- | --- | --- | --- |
| HQ Lancer (spear, carried) | Right elbow folded backwards at idle/roll (-153 / -135 deg) | **UE IK pole.** `CireGrip` TwoBoneIK took the bend direction from the current pose. With a near-straight arm that direction is arbitrary, so the carry/second-hand IK folded the arm the wrong way. | UE: an anatomical pole (elbow behind, below and outside the shoulder-hand line, in the chest's current frame), plus a humerus twist so the upper arm's front faces the forearm | +135 deg, natural |
| HQ Dryad, Wizard, Scholar, Summoner, Keeper, Artificer (staff carry) | Right elbow -61 deg at idle (Dryad); twist flips while running | Same IK pole | Same | All positive |
| HQ Totemic Behemoth, Aetheri Warden (two-hand second hand) | Off-hand elbow -116 / -107 deg in roll | Same IK pole | Same | Positive |
| Witch Slayer (blunderbuss carry) | Left elbow -84 / -138 deg in windup/cast | Same IK pole | Same | +86 / +134 |
| HQ Ranger, clip `attack_crossbow` (crossbow preview only) | Right elbow down to -50 deg in 13 samples | **Mesh/bind.** The Tripo rig binds the right arm bent 59 deg (left 20). The FK transfer (`RetargetChampionAttacks.py`) replays the source's deltas around that bent start. | Not fixed. An arm-chain bind alignment in the transfer was tried and made it worse (bow hand in the face, 117 samples), so it was reverted. The live bow attack is unaffected (0 samples in the gallery). | Needs Eric: re-rig the Ranger B body with straight arms (it is already queued with the quiver split) |
| HQ bodies, all others (23 in the sweep) | None in raw clips | – | – | 0 backwards samples |
| Fallback bodies (`SK_*` Batch01, `CTS_Champ_*`, TripoModels), legacy `A_<body>_Attack` clips | 1-13 samples per body (e.g. SK_orc_chieftain -156, armored_archer -175) | Old prototype attack clips, used only when the HQ art is missing | Not changed: they are not played while the HQ bodies exist | Fallback only |
| **Vendors** (Arcane, Armory, Weaponsmith) | None: worst elbow +3 deg, twist ≤ 84 deg | – | – | Clean |
| NPC Hollow Siegebreaker (boss) | -22 deg in war_cry/shout/ground_slam/two_hand_skill2 (19 samples) | Source clip hyperextension (mild) | Left as is: mild, and inside a shout pose | Report only |
| Monster Fallen High Inquisitor | Arms folded backwards in every clip (-179 deg, 127 samples) | **Bind pose.** Modelled with both arms bent across the robe (bind 48/71 deg) holding a staff. The anterior is ambiguous. In the render the arms read as a caster's cross-body staff hold, not an obvious break. | Blender candidate (straighten the bind and re-weight) or a Tripo re-rig. Flagged, not changed. | Needs Eric's eye |
| Monsters Aetheri Bulwark, Bastion Golem, Voidborn Herald, Drakkari Wingshot | -16 to -26 deg in 1-6 samples (sword_shield attack3/4, ground_slam) | Source Fab clip hyperextension at the swing's end | None (mild, a single frame at contact) | Report only |
| Monster Drakkari Flamecaller HQ | Flagged at +28/+81 deg (bind anterior ambiguity), not backwards in the render | Audit reference | – | False positive |
| Hands "backwards" | No champion hand is reversed at idle. Thumbs face forward at idle for every body (`thumbFwd` 0.4-0.8). | See Weapons: the *weapon* pointed backwards, which reads as a reversed hand | Weapons fix | – |

**Blender** (Phase 0) was used for inspection: `qa_turnaround` renders of the HQ Golem and Chieftain, where the flexed
elbows bend forward and the skin holds. Blender was not needed for any champion fix: every live champion defect was an
IK or data problem in UE. No mesh was modified, and nothing needed a Tripo regen, except the Ranger B re-rig noted above.
drowned_deep tentacle bones were not started (see "Not done").

## Weapons

**Direction ("the weapon isn't the right direction").** `WeaponGrips.fab.json` lists the Fab meshes' handle axes by their
modelling direction. On the bind grip, `SM_Sword_1` (footman), `SM_Axe_1` (chieftain), `SM_Dagger_1` and `SM_WarHammer`
came out **pommel-first**: the tip was behind the fist at idle (`tipF` -0.90 / -0.94 / -0.91).
* `CireWeapons::BusinessAxis` points the handle axis at the far end of the prop (the blade or head).
* It flips the axis when the far end along it is clearly the shorter one.
* Used by champions (`CireWeaponPresentation`), creatures (`CireCreatureArt`) and monster props (`CireMonsterArt`).
* After: footman +0.49 (upright carry), chieftain +0.75, summoner dagger +0.91.

Every weapon type in the gallery now faces the right way:
* **Authored grips** (knight sword, paladin maces, shields, crossbow stock, gunblade in the Fab set) are unchanged: they
  were already right.
* **Crossbow and gunblade on the bind grip** (the HQ bodies play their own Tripo clips): the gunblade sword and pistol
  point forward (pistol `tipF` +0.97). The crossbow is placed through the preview path, and `CireGripTests` covers it.

**Size (2x).** `WeaponLoadouts.json` has a new `sizeClasses` table, and parts carry a `sizeClass`:

| Class | Scale | Girth | Cap | Parts |
| --- | --- | --- | --- | --- |
| `one_hand` | 2.0 | 0.8 | 3/4 of the body height | swords (warden, footman), gunblade sword, falchion, war axe, throwing axes, daggers (summoner, dual) |
| `mace` | 2.0 | 0.8 | 3/4 of the body height | flail/maces (paladins), war hammer, pick-hammer |
| `pistol` | 1.5 | 1.0 | – | gunblade pistol, flintlock |

**Not scaled** (my choice): staffs, spear/lance, halberd, glaive, totem, bow, crossbow, blunderbuss and launchers. They
are 1.0-1.1x body height already, and none looked tiny in the gallery.

**Why girth and a cap.**
* **Girth.** The length doubles, but the cross-section only grows 1.6x (girth 0.8 × 2). The prop is placed at the thinner
  scale, so the fingers curl around a handle they can close on. It is then stretched along its handle axis about the
  handle point (`CireWeapons::HandleStretch`). The grip stays seated at the handle, not the weapon centre
  (`handleGap` 0.0 cm on every prop).
* **Cap.** A 2x knight sword would be 182 cm on a 192 cm body. The cap keeps any class prop within 3/4 of the bind body
  height, and never below its size before the class.
* **Long-weapon carry.** A one-hander that becomes 1.2 m or longer is carried upright at the side at rest instead of
  hanging from a straight arm (the 2x axe head went through the floor). The attack clips take it from there.

Resulting lengths (idle gallery, body height in brackets):

| Champion | Prop | Before | After |
| --- | --- | --- | --- |
| Knight (192) | sword | 91 cm | 146 cm (cap, 1.6x) |
| Righteous / Holy Paladin (192 / 167) | mace | 62 / 65 cm | 123 / 127 cm (2x / 1.94x) |
| Dwarf Miner (123) | pick-hammer | 54 cm | 92 cm (cap, 1.7x) |
| Orc Chieftain (201) | war axe | 91 cm | 149 cm (cap, 1.6x) |
| Drakish Footman (216) | sword | 85 cm | 145 cm (1.7x) |
| Melee / Ranged Troll (198) | throwing axes | 57 cm | 114 cm (2x) |
| Summoner (166) | dagger | 26 cm | 52 cm (2x) |
| Gunblade (165) | sword / pistol | 92 / 40 cm | 123 cm (cap) / 60 cm |

**Monsters and vendors.** Monster hand props share `BusinessAxis`. The 12 `grips_*` stages of
`RunMonsterGallery.py` show every monster weapon blade-forward in a closed fist after the change. Monster props were
not resized: Eric's 2x request was about the classes, meaning the champions. Vendors hold no weapons, and their arms are
clean in the sweep.

Captures:
* `Saved/AgentLogs/grip-gallery-after/` (all 27 champions, sheets and metrics)
* `Saved/AgentLogs/rig-before/` (before)
* `Saved/AgentLogs/monster-grips-after/`
* `Saved/AgentLogs/rigaudit-all-bodies.txt` (143-body sweep)

## Tests and tools

* **`CireGripTests`** (native, in RunExpansionChecks):
  * every weapon class (16 cases) has no backwards elbow at idle or attack;
  * size classes are 2x or exactly at the cap, never above it;
  * upscaled props are stretched along the handle only (cross/long ratio = girth);
  * a bind-gripped one-ended weapon keeps its business end on the thumb side;
  * the handle-radius checks use the cross-section scale.
  * Result: `CIRE_GRIP_PASS checks=912 grips=56`.
* **`Tools/RunGripGallery.py --all [--out-copy DIR]`**: adds the arms view and rig metrics, renders unarmed champions
  too, and copies the sheets.
* **`Tools/RunRigAudit.py [--only substr] [--summary json]`**: the clip sweep.
* **Blender Phase 0:**
  * `Tools/RunBlender.py <script> -- <args>` is the headless wrapper. It runs under the UE python (never Git Bash), with
    a timeout and a log in `Saved/Blender/logs`.
  * `Tools/Blender/common.py` holds the import and UE-ready FBX export (scale 1.0, Z up, no leaf bones, deform only)
    and the stats.
  * `Tools/Blender/qa_turnaround.py` renders front/back/left/right, the face, both hands and a 90-degree elbow-flex
    test pose, and writes stats.json. It takes about 5-12 s per body.
  * `Tools/ExportForBlender.py` exports from the **full editor** (`UnrealEditor -ExecutePythonScript`), about 3 min
    including the editor boot. The FBX skeletal exporter asserts in a commandlet (`MeshObject`). Output goes only to
    `Saved/Blender/export`, never a committed path.
* **`Tools/RetargetChampionAttacks.py -CireChampionAttacksOnly=<folders>`** rebuilds single HQ bodies' transferred
  clips.

## Blender round-trip (wave 2)

**The pipeline:** export from UE, re-bind in Blender, re-import onto the existing skeleton. It is proven on the HQ
Ranger.

1. **Export.** `Tools/ExportForBlender.py` must run in the full editor.
2. **Re-bind.** `Tools/Blender/rebind.py`:
   * Poses the spine, arms and legs onto straight reference directions. `--ref` takes a UE bind-point JSON, e.g. the
     transfer's source rig.
   * Twists each upper arm and thigh about its own axis so its real hinge front, read from the original bend, faces the
     body's front. A shortest-arc straighten alone keeps the captured frame's twist, and every bind-based tool then reads
     the elbow backwards.
   * Applies the armature deformation to the mesh and makes that pose the rest pose.
   * The skin is unchanged for any animated pose, so the body's own Tripo clips look identical.
   * **Handedness:** UE is left-handed and the FBX conversion mirrors one axis. The body frame is built as
     `up x left` in UE coordinates and `left x up` in Blender's.
3. **Import.** `Tools/ImportFromBlender.py` imports onto the given Skeleton, with no new skeleton, materials or
   animations. It keeps the replaced asset's slot materials, because the FBX import resets them to WorldGridMaterial.
   A dest under `/Game/_BlenderRoundTrip` is a dry run.
4. **Check.** `RunRigAudit.py --meshes <dry-run path>` sweeps the body's own clips on the candidate before it replaces
   anything.

**Verified.** An identity round-trip reproduces the bind exactly (pelvis, hands and feet within 0.01 cm; same clip
results). It also found and fixed Blender's `ignore_leaf_bones` dropping the `head` bone.

**HQ Ranger.**
* **Cause.** The Tripo bridge import kept an animation frame (mid-run) as the reference pose: right elbow bent 57 deg,
  legs mid-stride.
* **Fix.** Re-bound in Blender onto IronboundBruiserV2's straight bind directions (the source rig of the FK transfer),
  in about 5 s. The mesh was replaced in place, and its 5 ChampionAttacks02 clips were re-transferred.
* **Result.** 0 backwards-elbow samples across all 15 clips (`attack_crossbow` was -50 deg in 13 samples before).
* **Captures.**
  * `Saved/AgentLogs/ranger-blender/` (before/after renders, re-bind report)
  * `ranger-rebind-bow/`
  * `ranger-rebind-crossbow/`

**Also fixed on the way:**
* **Crossbow motion.** The crossbow preview now plays its own motion (`attack_crossbow`). It used to play the profile
  default, the bow.
* **Muzzle direction.**
  * Carried stock props (crossbow, blunderbuss, launchers) rest muzzle-forward. The crossbow was carried with its limbs
    through the torso (`offF` -0.98 → +0.98).
  * `BusinessAxis` no longer flips props with a second grip.
  * `CireGripTests` checks the muzzle.

**Ranger quiver.** It is fused into the one body island, and the UV layout has 2,756 islands, so neither loose parts nor
UV islands isolate it. A clean cut needs a mask: Tripo's detailed segmentation (parts 7 + 16 on 2b9fbccc) or a hand-drawn
selection. The 90k decimate is moot: the UE body is already 92.6k tris, and the 140k figure was the Tripo source's.
`Tools/Blender/parts.py` lists and splits parts once a mask exists.

**Skeleton asset.** The re-import updates the mesh's reference pose. The shared Skeleton asset on disk was not re-saved.
Every Ranger clip keys every bone, so nothing reads the skeleton's reference pose for this body.

## Not done / needs Eric

* **Ranger B re-rig with straight arms.** The HQ Ranger's right arm is bound bent 59 deg, which drives the crossbow
  preview's backwards elbow. It pairs with the pending quiver split.
* **Fallen High Inquisitor.** The body is modelled with arms folded across the robe. Should it get a Blender bind
  straighten or a Tripo re-rig?
* **drowned_deep tentacle bones** (Phase 1 item 2) and **Ranger quiver decimate** (Phase 1 item 1) were not started.
  The export → Blender → re-import round-trip onto the existing skeletons is not proven yet. Export and QA are proven;
  re-import is next.
