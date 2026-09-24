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
