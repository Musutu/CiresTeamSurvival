# Monster rig (feat/monster-rig)

Every Tripo race/NPC body is a Tripo **Humanoid** rig on the UE5 Mannequin preset (61 bones, UE5 names). The gap was
never the skeleton: each body had only 6-9 generic Tripo clips. This branch gives each body a **weapon-matched Fab set**
and moves the parts Tripo gave no bones to (tentacles, vines) **in the skin material**.

## Pipeline

1. **Audit** - `UnrealEditor-Cmd <uproject> -run=pythonscript -script=<abs>/Tools/AuditMonsterRig.py -EnablePlugins=GeometryScripting -nullrhi [-CireRigAuditOnly=A+B] [-CireRigAuditPoints]`
   writes `Saved/MonsterRigAudit.json` (and, with `-CireRigAuditPoints`, sampled bind-pose points per mesh in
   `Saved/MonsterRigPoints/`). Measured in the bind pose against the reference skeleton (GeometryScript bone info):
   islands, *leg-bound high* (leg-bone vertices above the pelvis: a fused weapon on the wrong limb), *arm-bound low*
   (arm/hand vertices below the knee), *far* (vertices > 35% of body height from their dominant bone: stretch risk).
2. **Weapon-matched sets** - `Art/Fab/FabAnimMap.json` `monsterSets` (`assign`: body -> set; `roles`: set -> role ->
   Fab clip). `Tools/RetargetFabAnimations.py -CireFabAnimMonsterSets -CireFabAnimOnly=A+B` retargets idle, walk
   f/b/l/r, run, attack/attackAlt/attack3, heavy/heavy2, cast, shout, hit, death onto each body
   (`/Game/FabDerived/Anim/<Variant>Set`, local only: Fab-licensed) and writes `Content/Data/MonsterFabClips.json`
   `variants.<Variant>.{set, roles, walkSpeedCm, runSpeedCm}`. At runtime the set roles replace the Tripo roles;
   a missing package (clean clone, `-CireNoFab`) keeps the Tripo library.
3. **Runtime** (`CireMonsterArt`): basic swings rotate attack -> attackAlt -> attack3 by the replicated swing serial;
   cone -> heavy, charge/pull -> heavy2, self circle -> ground_slam|heavy2, rally/enrage/provoke -> shout, other
   casts -> cast; rank-scaled readable wind-ups (`cire.Monsters.SwingReadability`, normal .3-.5 s, elite .38-.6 s,
   boss .55-.85 s); directional gait layer (strafe L/R, back-pedal, side steps; only the fallback when
   movement-feel (cire.Locomotion) is off, since its travel warp and reversed gait own that); feet floor
   clamp (the pelvis lifts so a retargeted lunge or low gait never sinks the lowest foot below the idle stance).
4. **Skin sway** - `M_CireMonsterSkin` (`Tools/BuildRaceSkinMaterial.py`, patched in place when the asset exists)
   has a world-position-offset sway on the *pre-skinned* position: up to two ellipsoid regions per body
   (`SwayCenterA/B`, `SwayRadiiA/B`, `SwayBandA/B` = rootZ, tipZ, amount cm) plus `SwaySpeed`/`SwayWave`, all
   0 by default. Data: `Content/Data/MonsterArt.json` `bodies.<Variant>.sway` (`regions[]`: `center`, `radii`,
   `rootZ`, `tipZ` (above root for upright tentacles), `amount`), applied by `CireRaces::ApplySkin`.
   Tuning: `python Tools/RunMonsterGallery.py --cvars cire.Monsters.SwayDebug=1 --only face_<unit>+...` paints the
   mask green (`face_` = upper-body close-up, each unit facing the lens and turned 75 degrees).
   Why in-engine: Tripo Studio's "Other" rig re-detected a biped on Mind Leech (20 credits, no tentacle chains).

## Status (2026-09-26)

| Race | Sets | Sway |
|---|---|---|
| drowned_deep | all 8 bodies (dual, two_hand x2, sword_shield, spell x4) | tentacle beards on Abyssal Stalker, Deepspawn Thrall, Coralshell Guardian, Tidecaller, Barbspitter, Drowned Prophet; Mind Leech skirt; Prophet skirt; Maw's upright maw tentacles |
| blightwood | all 8 bodies (one_hand, two_hand, sword_shield, spell x3, unarmed x2) | Vinelasher whips (both arms), Elder Oakheart moss beard |
| ironhide | IronhideGrunt one_hand, RedmoonRavager two_hand, IronhideBulwark sword_shield, BloodHexer spell, RedmoonAxethrower one_hand, IronhideDrummerHQ dual, IronhideWarchief two_hand, IronhideJuggernaut two_hand | 8/8 retargeted, 15 roles each. Gallery: weapon poses read (axes, sword+shield, dual, two-hand club, spell), grounded. No hanging parts to sway; no audit flags. |
| other races | assigned in `monsterSets.assign`, not yet retargeted | - |

Segmentation: none needed for drowned/blightwood - every fused weapon is weighted to the hand that holds it
(remaining flags are staffs/claws/whips on their own hand or fingers). Candidates elsewhere, from the audit:
TuskedBehemothHQ (leg-bound high 4%), GravemawPackLeader / DrakkariAshwing / EmberWhelp / FeralMammoth (far > 7%),
DeepforgeRunesmith (arm-bound low 10%: hammer head on ring_03_r).

## Audit (Saved/MonsterRigAudit.json, 82 bodies)

| Body | File | Islands | Assigned set | Applied | Sway regions | Flags |
|---|---|---|---|---|---|---|
| AbyssalStalker | RaceMeshes | 1 | dual | **dual** | 1 |  |
| AetheriBulwark | RaceMeshes | 1 | sword_shield | Tripo only |  |  |
| AetheriColossusB | RaceMeshes | 2 | unarmed | Tripo only |  |  |
| AetheriEngineer | RaceMeshes | 2 | spell | Tripo only |  |  |
| AetheriHierarch | RaceMeshes | 1 | spell | Tripo only |  |  |
| AetheriLancer | RaceMeshes | 1 | gun | Tripo only |  |  |
| AetheriPhaseblade | RaceMeshes | 2 | dual | Tripo only |  |  |
| AetheriWarframe | RaceMeshes | 1 | unarmed | Tripo only |  |  |
| BarbedHunter | NPCMeshes | 2 | bow | Tripo only |  |  |
| BarbedHunterB | NPCMeshes | 6 | crossbow | Tripo only |  |  |
| BarbspitterHQ | RaceMeshes | 2 | spell | **spell** | 1 |  |
| BarkhideWarden | RaceMeshes | 1 | sword_shield | **sword_shield** |  |  |
| BastionGolem | RaceMeshes | 3 | sword_shield | Tripo only |  |  |
| BlightCaster | NPCMeshes | 1 | spell | Tripo only |  | arm-bound low 1.4% (thumb_03_l) |
| BlightCasterB | NPCMeshes | 3 | spell | Tripo only |  |  |
| BlightedChaplain | RaceMeshes | 2 | spell | Tripo only |  |  |
| BloodHexer | RaceMeshes | 1 | spell | Tripo only |  |  |
| CoralshellGuardian | RaceMeshes | 1 | sword_shield | **sword_shield** | 1 |  |
| CrystalBallista | RaceMeshes | 1 | - | Tripo only |  |  |
| DeepforgeRunesmith | RaceMeshes | 1 | spell | Tripo only |  | arm-bound low 10.2% (ring_03_r) |
| DeepspawnThrall | RaceMeshes | 1 | two_hand | **two_hand** | 1 |  |
| DrakkariAshwing | RaceMeshes | 1 | two_hand | Tripo only |  | far 13.6% (upperarm_r) |
| DrakkariBroodmother | RaceMeshes | 2 | spell | Tripo only |  | far 7.5% (clavicle_r) |
| DrakkariFlamecallerHQ | RaceMeshes | 1 | spell | Tripo only |  | arm-bound low 1.4% (pinky_03_l); far 2.4% (pinky_03_l) |
| DrakkariScalebreaker | RaceMeshes | 1 | two_hand | Tripo only |  |  |
| DrakkariScaleguard | RaceMeshes | 1 | sword_shield | Tripo only |  | far 3.1% (thigh_r) |
| DrakkariWhelpguard | RaceMeshes | 1 | one_hand | Tripo only |  |  |
| DrakkariWingshot | RaceMeshes | 2 | bow | Tripo only |  |  |
| DreadKnight | RaceMeshes | 1 | two_hand | Tripo only |  | far 5.2% (spine_02) |
| DrownedProphet | RaceMeshes | 3 | spell | **spell** | 2 | leg-bound high 2.1% (thigh_r); arm-bound low 1.2% (hand_r) |
| ElderOakheart | RaceMeshes | 1 | unarmed | **unarmed** | 1 |  |
| EmberWhelp | RaceMeshes | 1 | unarmed | Tripo only |  | far 11.8% (thigh_l) |
| EtherMote | RaceMeshes | 2 | unarmed | Tripo only |  |  |
| FallenCrusader | RaceMeshes | 3 | two_hand | Tripo only |  | far 4.2% (clavicle_l) |
| FallenHighInquisitor | RaceMeshes | 3 | spell | Tripo only |  |  |
| FallenInquisitorCrossbow | RaceMeshes | 1 | crossbow | Tripo only |  |  |
| FallenSquire | RaceMeshes | 1 | one_hand | Tripo only |  |  |
| FeralMammoth | RaceMeshes | 1 | - | Tripo only |  | far 7.2% (neck_01) |
| FeralShaman | RaceMeshes | 3 | spell | Tripo only |  |  |
| FeralUrsoth | RaceMeshes | 1 | - | Tripo only |  |  |
| Flagellant | RaceMeshes | 1 | dual | Tripo only |  |  |
| GraniteCrusherHQ | RaceMeshes | 1 | unarmed | Tripo only |  |  |
| GravemawPackLeader | NPCMeshes | 2 | two_hand | Tripo only |  | far 12.4% (spine_02) |
| HollowInfantry | NPCMeshes | 1 | - | Tripo only |  |  |
| HollowInfantryB | NPCMeshes | 1 | - | Tripo only |  |  |
| HollowShieldbearer | NPCMeshes | 5 | sword_shield | Tripo only |  | arm-bound low 1.8% (thumb_03_l); far 8.0% (thumb_03_l) |
| HollowShieldbearerV2 | NPCMeshes | 10 | sword_shield | Tripo only |  |  |
| HollowSiegebreaker | NPCMeshes | 2 | two_hand | Tripo only |  | arm-bound low 3.0% (pinky_01_r) |
| IronboundBruiser | NPCMeshes | 2 | two_hand | Tripo only |  | far 4.8% (thigh_l) |
| IronboundBruiserV2 | NPCMeshes | 1 | two_hand | Tripo only |  |  |
| IronhideBulwark | RaceMeshes | 2 | sword_shield | Tripo only |  |  |
| IronhideDrummerHQ | RaceMeshes | 1 | dual | Tripo only |  |  |
| IronhideGrunt | RaceMeshes | 1 | one_hand | Tripo only |  |  |
| IronhideJuggernaut | RaceMeshes | 2 | two_hand | Tripo only |  |  |
| IronhideWarchief | RaceMeshes | 1 | two_hand | Tripo only |  |  |
| MawOfTheDeep | RaceMeshes | 3 | two_hand | **two_hand** | 1 | arm-bound low 1.4% (index_03_r) |
| MindLeech | RaceMeshes | 2 | spell | **spell** | 1 |  |
| NullWardenHQ | RaceMeshes | 1 | unarmed | Tripo only |  |  |
| OathbreakerTemplar | RaceMeshes | 78 | sword_shield | Tripo only |  |  |
| RedmoonAxethrower | RaceMeshes | 2 | one_hand | Tripo only |  |  |
| RedmoonRavager | RaceMeshes | 1 | two_hand | Tripo only |  |  |
| RiftGazer | RaceMeshes | 1 | spell | Tripo only |  | arm-bound low 1.9% (index_02_r) |
| RiftStalkerHQ | RaceMeshes | 1 | dual | Tripo only |  |  |
| RiftWeaverHQ | RaceMeshes | 1 | spell | Tripo only |  |  |
| RotbloomShamanHQ | RaceMeshes | 1 | spell | **spell** |  | arm-bound low 1.4% (thumb_03_r) |
| RuneSentinelHQ | RaceMeshes | 2 | one_hand | Tripo only |  |  |
| SaplingBrute | RaceMeshes | 2 | two_hand | **two_hand** |  |  |
| SkitterDrone | RaceMeshes | 1 | unarmed | Tripo only |  |  |
| Sporeling | RaceMeshes | 1 | unarmed | **unarmed** |  |  |
| StonebornColossus | RaceMeshes | 1 | unarmed | Tripo only |  |  |
| StonebornForgelord | RaceMeshes | 2 | two_hand | Tripo only |  |  |
| Thornspitter | RaceMeshes | 1 | spell | **spell** |  |  |
| Tidecaller | RaceMeshes | 1 | spell | **spell** | 1 |  |
| TuskedBehemothHQ | RaceMeshes | 2 | sword_shield | Tripo only |  | leg-bound high 4.0% (thigh_l) |
| Vinelasher | RaceMeshes | 1 | one_hand | **one_hand** | 2 | far 2.3% (middle_03_r) |
| VoidRavager | RaceMeshes | 1 | unarmed | Tripo only |  |  |
| VoidbornDevourer | RaceMeshes | 1 | unarmed | Tripo only |  |  |
| VoidbornHerald | RaceMeshes | 5 | spell | Tripo only |  |  |
| Voidling | RaceMeshes | 3 | unarmed | Tripo only |  |  |
| WerebearMaulerHQ | RaceMeshes | 1 | unarmed | Tripo only |  |  |
| WildOutrider | RaceMeshes | 6 | - | Tripo only |  |  |
| WitheredMatron | RaceMeshes | 1 | spell | **spell** |  |  |
