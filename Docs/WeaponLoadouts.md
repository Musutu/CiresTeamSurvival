# Prototype armory

`Content/Data/WeaponLoadouts.json` maps every champion profile to a visual preset. The default Ranger retains its bow. Bear, Whisp and all Ether Golems use natural attacks and cannot receive humanoid props through this file. A body without both hand bones also rejects equipment, including a Footman's non-humanoid form. Centaur's lance requires an actual compatible hand rig.

| Profiles | Default props |
| --- | --- |
| Iron Warden, Drakish Footman | Sword and shield |
| Ash Ranger | Bow, arrow and hip dagger |
| Veil Scholar, Wizard, Rift Summoner | Distinct staffs; Summoner also carries a ritual dagger |
| Lancer, compatible Centaur | Lance |
| Holy / Righteous Paladin | Spiked flail and shield |
| Dwarf Miner | Pick-hammer |
| Orc Chieftain | War axe |
| Totemic Behemoth | Carved stone totem |
| Melee / Ranged Troll | Paired axes / paired throwing axes |
| Dryad, Keeper | Branch staff / lantern staff |

The models are original local prototype geometry. The flail chain is modeled rigidly, and the crossbow uses the current ranged pose family; bespoke flexible-chain physics, finger curls, dual-wield animation and crossbow reload clips remain future art work. This pass does not change combat rules or regenerate animations. Existing imported characters, skeletons, animation clips, and the five legacy weapon meshes are protected from modification.

## Editing and preview

Each part has an asset token, `bone`, optional `offsetCm`, optional `rotation` in `[pitch,yaw,roll]`, and optional uniform `scale` (.35–2). The origin is the palm grip. Offsets use real world centimeters; attachment code compensates for the imported root scale of 100 and champion body scale. The grip is centered between wrist and middle knuckle when that reference bone exists. All props have collision and navigation disabled and follow champion visibility/death state.

`role: "primary"` identifies one held weapon; bow/crossbow presets additionally need one `ammunition` part. `hideOnRelease` hides thrown props from .25 to .53 of the normalized .65-second attack; ammunition hides after release until the next idle. Preset `motion` changes only presentation. A crossbow selection still uses the Ranger's existing ranged basic-attack rules. Never use these visual presets to imply changed attack range, damage, or skills.

`previewOptions` lists approved local visual variants, with the default first. `CireWeapons::CyclePreview(Hero, Message)` and `ResetPreview(Hero, Message)` are non-shipping APIs for the development HUD. The preview is local, does not persist, and resets when changing profile. `CireWeapons::Reload(Error)` / `cire.Weapons.Reload` transactionally reload the JSON; an invalid file leaves the last valid database intact. A successful reload updates existing components on their next presentation tick. Components expose `GetEquippedLoadout()` and `GetPartCount()` for galleries.

## Build and validation

Ordinary Python `Tools/BuildArmoryProps.py` validates source geometry and loadout coverage without file mutation or Unreal. `--sources` writes the fourteen OBJ meshes, shared MTL and dimensional manifest under `Art/Weapons/ArmoryPrototype01`. `Tools/PreviewArmoryProps.py` uses Pillow to create a source-only silhouette atlas. This atlas does not establish in-game acceptance.

After coordinating with all other asset writers, run Unreal Python commandlets sequentially with `-run=pythonscript -script="<absolute project>/Tools/BuildArmoryProps.py" -unattended -nullrhi -nosound -NoLiveCoding`:

1. `-CireArmoryBuild` creates fourteen meshes and nine materials in the fresh `/Game/Art/Weapons/ArmoryPrototype01` directory. It refuses an existing output directory. It hashes every preexisting Unreal package, imports only the new props, checks exact centimeter bounds and material assignments, and rechecks protected hashes.
2. A separate process with `-CireArmoryVerify` reloads the saved assets, repeats dimensions/material checks and verifies all protected hashes. Reports are written to `Saved/ArmoryPrototype01/build.json` and `verify.json`.
3. Build the C++ module through the coordinated root workflow. Call `CireWeapons::RunValidationSmoke(true)` from a native probe; it checks every roster profile, natural-attack exclusions, all nineteen prop meshes/materials and parser rejection cases.
4. Run the all-profile art gallery and inspect idle, movement, windup and release. Check hands, scales, shield positions, staff/ground intersections, dual-axe release, and privacy/death hiding. Cycle Ranger to crossbow and Warden/Paladin to hammer/shield in the development UI for an additional close view. Source preflight and asset verification do not replace these rendered checks.

The isolated `Tools/CreatureBuilder/CreatureBuilder.uproject` has its own copied Content tree. When used, the separate verify pass copies only the exact fourteen meshes and nine materials into a fresh real Content namespace and byte-checks every transfer. It never replaces differing destination content. `-CireArmoryRecoverValidation` may accompany verify to fully revalidate saved output after a build-time validation failure; it does not regenerate assets or bypass any dimensions, materials, triangle counts, or protection checks. The initial run needed this because the deprecated editor-subsystem LOD query was unavailable in a commandlet; the script now checks the mesh's actual render LOD and triangle data directly.

## Grip model (weapon-grips)

**Root cause.** Props used to be placed from each body's bind-pose hand (`CireGrip::Place`, `WeaponGrips.json` tilt).
The Fab clips (GDH bundle, Gun & Sword, Crossbow) were authored on the UE5 mannequin with the weapon on a socket of
its hand, and the retargeted bodies' hands sit in that frame, not in the bind frame. So bind-pose swords, maces and
axes pointed about 135-150 degrees backwards in every Fab clip (`bindTipDev` below).

**Fix.**
* `Tools/MeasureMannySockets.py` (commandlet) reads each set's demo socket and demo weapon mesh and writes
  `Content/Data/WeaponSockets.json`. For each set and hand it records the grip frame in mannequin bone space: grip
  point, tip (handle to business end) and edge. Shields record strap, up and face-out instead. Two-handed sets also
  record the second hand's bone relative to the main hand.
  * **Gun & Sword** (gunblade) holds its sword and pistol on the helper bones `Sword_Weapon_R` / `Gun_Weapon_L`.
    They are fixed children of the hands in every clip (spread 0.0 cm / 0.0 deg over 24 samples) and are
    re-expressed in the hand.
  * The pistol grip (`kind: pistol`) comes from the demo hand: tip = up the grip (pinky to index knuckle),
    edge = muzzle.
  * **Crossbow** rides the animated `w_crossbow` bone. The left hand holds its fore-end rigidly through the attack
    clips (spread 0.7 cm / 2.6 deg), so the authored grip is that stock hold (`grip: "stock"`): tip = muzzle along
    the left knuckle line, edge = up. The tool checks that the limbs sit at the muzzle end.
  * At runtime the prop is gripped at its `WeaponGrips.json` `offHand` / `offAxis` fore-grip
    (`CireWeaponSockets::PropGrip`). `swapHandPresets` is skipped for a hand the set authors.
* `CireWeaponSockets::Calibration` measures each body's retarget rotation per hand bone (`Target = Source * Q`) from
  one of its retargeted Fab clips against the mannequin source. `Intended` then maps the mannequin frame onto the
  body, and `UCireWeaponPresentation::PlaceAuthored` puts the prop there. The fingers still curl around the handle
  (`CireGrip::BuildHandPoseAlong`), and two-handed sets IK the second hand onto the handle line.
* Bodies without Fab clips, sets without an authored frame, and `-CireLegacyGrips` / `cire.Grips.Legacy 1` keep the
  bind grip (`mode=bind`).
* Also on this branch: the GDH AxeV1 set for the miner and chieftain, the Spear set for the Aetheri Warden, the
  upright Idle2 sword-and-shield idle (knight, paladins, footman), and staffs carried low at the side in one hand
  (`WeaponSockets.json` `carry`).

**Grip table.** Idle state of the after-gallery, `Saved/GripGallery/20260926-071117-after/metrics.txt`
(`python Tools/RunGripGallery.py --pylib <Pillow dir>`; `--legacy` renders the before set):
* `bindTipDev` / `bindEdgeDev`: how far the old bind-pose grip was from the clip's grip.
* spread: the calibration's frame-to-frame deviation.
* off-hand: second-hand distance from its IK target at idle.

| Champion | Preset | Fab set (calibration, spread) | Prop @ hand: mode (bindTipDev / bindEdgeDev) | Off-hand |
| --- | --- | --- | --- | --- |
| Knight (Poly plate) | warden | sword_shield (0.1 deg) | sword @r authored (146 / 36); shield @l authored (9 / 17) | - |
| Paladin (righteous, holy) | paladin | sword_shield (0.1 deg) | mace @r authored (148 / 34); shield @l authored (9 / 17) | - |
| Drakish Footman | footman | sword_shield (4.1 deg) | sword @r authored (139 / 29); kite shield @l authored (36 / 42) | - |
| Orc Chieftain | chieftain | axe (5.6 deg) | war axe @r authored (136 / 20) | - |
| Dwarf Miner | miner | axe (10.2 deg) | pick-hammer @r authored (37 / 23) | - |
| Melee Troll | troll_melee | dual (11.3 deg) | throwing axes @r/@l authored (43 / 30, 33 / 22) | - |
| Ranged Troll | troll_ranged | throw (11.3 deg) | throwing axes @r/@l authored (49 / 147, 33 / 164) | - |
| Lancer | lancer | spear (2.6 deg) | spear @r authored, two-handed (30 / 169) | 0.0 cm |
| Totemic Behemoth | behemoth | two_hand (13.5 deg) | totem @r authored, two-handed (25 / 4) | 4.1 cm |
| Aetheri Warden | aetheri_warden | spear (12.7 deg) | halberd @r authored, two-handed (20 / 166) | 0.0 cm with the Warden spear clone (was 65 cm; see below) |
| Huntress | tripo_huntress | spear (1.4 deg) | glaive @r authored, two-handed (39 / 168); launcher @l bind | - |
| **Gunblade** | tripo_gunblade | **gun (1.7 deg)** | **sword @r authored (11 / 10); pistol @l authored (17 / 172)** | - |
| **Ranger (crossbow preview)** | ranger_crossbow | **crossbow** (bow_attack1 calibration, 3.1 deg) | **crossbow @l authored, stock hold (82 / 89)** | - |
| Ranger | ranger | bow | bow @l bind (no bow socket in the pack) | - |
| Scholar, Wizard, Summoner, Dryad, Keeper, Artificer | casters | spell (unarmed) | staff @r bind, side carry; Summoner dagger @l bind | - |
| Witch Slayer | tripo_witch_slayer | spell (unarmed) | blunderbuss @l bind (no gun socket in the spell set) | 15 cm |

The crossbow row comes from `python Tools/RunGripGallery.py --only ranger --preset ranger:ranger_crossbow`
(`Saved/GripGallery/20260926-080448-after`). `swapHandPresets` no longer moves a prop off a hand its Fab set
authors: the Crossbow pack holds the stock in the left hand, as the preset lists it. The grip test now fails any
held prop that stays on the bind grip while its set authors that hand.

**After the champion-hq merge (main a3b53d6 / ee29d6d).** The HQ champion bodies (`/Game/Tripo/ChampionsHQ`, folders
`HQ<Name>`) play their own Tripo clips, authored on their bind pose, so they keep the bind grip (`set=-`, `mode=bind`)
and the 145-degree Fab-clip bug does not apply to them. Only the Polyphoria plate body (knight, both paladins) plays
Fab clips on current main, and it keeps the authored sword, mace and shield grips above. Full gallery on the merged
tree: `Saved/GripGallery/20260926-084204-after`. The HQ Warden carries its halberd upright with the second hand on
the shaft (0.0 cm from target at idle and run). The table rows for the other bodies describe the Fab-clip bodies,
which are still the fallback when the HQ art is missing.

**Aetheri Warden spear locomotion (Fab-clip body).** The 65 cm second-hand gap had two causes:
* **Static locomotion.** The Warden's Tripo BlendSpace (`BS_Idle_Walk_Run_AetheriWarden`) evaluates to a static pose
  at runtime. So does every duplicate of it, including the shared `BS_Fab_Locomotion_TripoAetheriWarden`: idle and run
  rendered the same frame, and the main hand sat at the same place (130 cm from the off shoulder, 65 cm arm).
* **Wrong set.** The shared locomotion carried two-handed (greatsword) clips while the Warden attacks with the Spear
  set.

`Tools/BuildWardenSpearLocomotion.py` fixes both in a Warden-only folder. It changes nothing shared: all 1611 shared
files have the same SHA-1 and mtime before and after.
* It clones the Spear set's idle and 8-way walk/run onto `TripoAetheriWarden`
  (`/Game/FabDerived/Warden/Warden_Spear/A_Warden_Spear_loco_*`).
* It builds `BS_Fab_Locomotion_Warden_Spear` on the lancer's proven BlendSpace, retargeted onto the Warden
  (the paladin-hq `make_template` route). That retarget carries the lancer's sample grid but none of its clips, so
  the `_template` scaffold has 27 empty samples. The tool deletes it once the real BlendSpace is built, and deletes a
  scaffold left by an older run as files before anything loads it. An empty-sample BlendSpace on disk fails to load
  ("sample with no/invalid animation"), which made a second run of the tool exit 1.
* `FabAnimations.json` `locomotionOverride` points only that body folder at the clone (`CireFabAnimation::Locomotion`).
* Like all of FabDerived it is derived from the Fab packs, so it stays local (gitignored). The main checkout needs one
  run of the tool.

Second hand from its grip target (`RunGripGallery.py --only aetheri_warden --hq-off`):

| State | Before | After |
| --- | --- | --- |
| Idle | 65.0 cm | 0.0 cm |
| Run | 65.0 cm | 0.0 cm |
| Attacks (hand released by design) | 17.7 / 26.6 cm | 17.7 / 26.7 cm |

Captures: `Saved/GripGallery/20260926-092053-after`.

**The other frozen Tripo fallback bodies.** Every new-champions body (`/Game/Tripo/Champions/<Name>`) had the same
frozen BlendSpace. I diffed idle against run in the `--hq-off` gallery (pixels changed by more than 24 of 255, at
1920x1080):

| Body | Before | After |
| --- | --- | --- |
| Gunblade | 396 | 115,287 |
| Witch Slayer | 250 | 96,472 |
| Huntress | 5,816 | 105,238 |
| Aetheri Artificer | 5,133 | 89,848 |
| Aetheri Warden | 333 | 163,813 |
| lancer (control, never frozen) | 110,748 | 110,331 |

All Batch01 and Polyphoria bodies moved normally (75,000-220,000). The same tool now builds each of the four other
bodies its own BlendSpace on the lancer grid. Their shared Fab clips were sound (the clips move) and are only
referenced (`/Game/FabDerived/BodyLoco/<Name>_Loco/BS_Fab_Locomotion_<Name>_Loco`). `locomotionOverride` names all
five folders. All 3054 shared FabDerived files have the same SHA-1 and mtime before and after. Captures:
`Saved/GripGallery/20260926-101427-after`.

Calibration is now per body and set (`CireWeaponSockets::Calibration(Body, Set)`, the set's own clip first), because
every Fab set is retargeted with its own retargeter. The miner and chieftain now calibrate on `axe_attack1` and the
ranged troll on `throw_attack1`; their grips were unchanged in the gallery. The live HQ Warden needs none of this: it
plays its own Tripo clips with the second hand on the shaft (0.0 cm).

**Open gaps.**
* **Crossbow at rest.** The Ranger idles and runs on the bow locomotion (the Crossbow pack ships no locomotion), so
  between shots the authored left-hand hold leaves the crossbow hanging diagonally. The old upright carry only
  existed on the bind grip. Windup and contact are shouldered and aimed correctly.
* **Staffs, bow and the Witch Slayer's blunderbuss** keep the bind grip. No owned pack authors a staff, a bow socket
  or a musket. Fab candidates (not bought): "Essential Magic Staff Animation Pack" and "Blunderbuss Musket Rifle
  Animation Pack". A mace-specific set would be "700+ Mace/Hammer Combat Animations"; today the paladin mace plays
  the sword-and-shield set.
* **Gunblade pistol at contact.** In the contact frame of the Gun & Sword attack the left hand is open and flat, and
  the pistol does not read in the capture. The before captures show the same. The pack's skeleton also has
  `Gun_Holder_R` / `Sword_Holder_L` bones, which suggests a holster swap during some clips. That is not verified and
  not modelled.

**Tests.**
* `CireGrip::RunSmoke` adds the gunblade case and runs `CireWeaponSockets::RunSmoke`.
* For every authored prop it checks that the handle runs along the authored grip line (within 6 degrees; the
  bind-pose tilt no longer applies) and that the prop's tip and edge match the Fab socket frame mapped onto the
  body (within 3 degrees).
* Authored grips get two tolerances, both checked in the gallery:
  * An authored shield may reach the wrist end of the forearm (along < 1.0). The Fab sword-and-shield clips strap
    it there, and Eric approved that shield grip on 2026-09-26.
  * The index finger may sit 1.5 cm looser, because the authored handle line is not the palm's natural axis
    (chieftain axe, behemoth totem).
* `CIRE_GRIP_PASS checks=791 grips=54`.
