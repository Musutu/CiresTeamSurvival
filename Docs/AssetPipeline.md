# Tripo → Unreal asset pipeline

## Latest acceptance — prototype preview

Run **PlayTripoPreview.cmd** for Iron Warden, Ash Ranger and Veil Scholar using corrected **Preview02** locomotion. All five front/back reference and idle/walk/jog1080p captures were reviewed, and both remote clients passed the art/selection/privacy probe. Evidence: `Saved/ArtChecks/20260923T204554254422Z/report.json`, `Saved/InterfaceSmoke/20260923T204554256467Z/report.json`. Original meshes/skeletons remain unchanged. This is prototype acceptance only; combat animation, cloth/weight polish, foot planting, LODs and load testing remain.

The original Preview01 retarget lost imported root scale100 and is rejected. `Tools/RepairTripoAnimationScale.py` duplicates its animations, restores reference root scale, divides the direct-child pelvis translation consistently, and preserves root position/rotation. Independent saved-asset reload passed1,224 pose checks, including the runtime root-lock/extraction path. Repaired BlendSpaces live at `/Game/Art/Characters/TripoRetarget/Preview02/{Warden,Ranger,Scholar}/Animations/BS_Idle_Walk_Run_{name}`. Keep the earlier rigs/retargeters in Preview01 for provenance, but use Preview02 clips.

Bridge origin instances are now editor-only references; gameplay creates the actual champion actors. The regular Play.cmd still uses the existing mannequin assets. No new Tripo generation was needed for this repair.


Art target: original realistic human dark fantasy for a third-person team-survival game. Use credible anatomy, armor construction, roughness, wear, and restrained color. Heroes should read as tank, agile damage, or spell/support silhouettes at gameplay distance. The game's visual reference is dark fantasy realism; the output must have its own identity.

## Current Tripo checkpoint — 2026-09-23

- **Iron Warden:** [Persistent rigged asset](https://studio.tripo3d.ai/workspace/rigging/352ec075-2a66-48b2-883a-1d5249935799), ID `352ec075-2a66-48b2-883a-1d5249935799`. Generated, retopologized to 10,423 polygons / 9,601 vertices and auto-rigged after selecting the UE5 Mannequin preset; 115-credit usage. The existing asset was reused and successfully imported as a SkeletalMesh through the official Unreal Bridge.
- **Ash Ranger:** [Persistent rigged asset](https://studio.tripo3d.ai/workspace/rigging/c80ee66a-668c-461c-b680-cacc4d16516c), ID `c80ee66a-668c-461c-b680-cacc4d16516c`. Realistic adult human female archer in fitted leather/steel. Generation verified complete at 1,957,526 source faces. Quad Smart Mesh P1.0 retopology produced 10,552 polygons / 9,807 vertices. UE5 Mannequin preset was selected and auto-rig completion verified. Total 115 credits: 55 generation, 40 retopology and 20 rigging. Successfully imported as a SkeletalMesh through the official Unreal Bridge.
- **Veil Scholar:** [Persistent rigged asset](https://studio.tripo3d.ai/workspace/rigging/5364420b-e866-4afe-9dc1-acf89186aef9), ID `5364420b-e866-4afe-9dc1-acf89186aef9`. Realistic adult human male support/mage in layered dark leather/cloth with brass and teal accents. Generation completed at 1,942,750 source faces. Quad Smart Mesh P1.0 retopology produced 9,980 polygons / 9,381 vertices. UE5 Mannequin preset was selected and auto-rig completion verified. Total 115 credits: 55 generation, 40 retopology and 20 rigging. Successfully imported as a SkeletalMesh through the official Unreal Bridge.

### Verified official Bridge imports

The user dispatched all three existing models through the official Tripo Bridge asset picker. The website connection showed **Connected**, and Unreal's `Saved/Logs/TripoBridgeTransfer.log` records successful FBX SkeletalMesh imports followed by the completed model imports below. All three imports are saved. Skeleton inspection, a missing base-color binding repair, and a first locomotion retarget build are complete; rendered materials, deformation, facing, and live champion integration still require visual review.

| Hero | Unreal content folder | Import completed (UTC, 2026-09-23) | Log line |
|---|---|---|---|
| Iron Warden | `/Game/TripoModels/medieval_knight_armor_3d_model` | 20:02:31.968 | 2554 |
| Ash Ranger | `/Game/TripoModels/armored_archer_3d_model` | 20:02:19.137 | 2385 |
| Veil Scholar | `/Game/TripoModels/battlefield_healer_3d_model` | 20:02:06.289 | 2155 |

The Bridge received FBX source files under `Intermediate/Tripo3D/<asset_id>/`; the manifest records each actual filename. The previous manually named `Cire_*_UE5_Rig.zip` download attempts are historical and are not the names used by these successful imports. The official connection workflow is documented in [Tripo's Unreal Bridge guide](https://www.tripo3d.ai/blog/tripo-dcc-bridge-for-ue).

Work resumed on the subsequent user-authorized turn. All three cloud assets should be reused; do not generate duplicates. The manifest records prompts, actual processing stages and credit usage.

The resumed work used 175 existing credits (Ash Ranger retopology/rigging and the full Veil Scholar workflow). The last visible account balance was 11,025. Recorded cumulative usage across all three heroes is 345 credits. No credits were purchased. Tripo resets its visible preset dropdown to Mixamo after returning to a model page; the recorded UE5 Mannequin value is the preset selected before each rig submission. Local inspection established that the actual exported skeletons use the UE4-style hierarchy described below; the requested preset is not proof of Manny animation compatibility.

### Saved material repair and animation build

`Saved/TripoBridgeInspection.json` reports three SkeletalMeshes, three separate Skeleton assets, three material instances, and 16 textures. All three meshes have 61 bones: every one of Unreal's 53 UE4 humanoid-template bones and parent relationships matches, plus eight twist bones. Manny has 89 bones in this project and a five-bone spine; the imported characters have three spine bones. The imported skeletons were retained rather than reassigned to Manny.

The Bridge material instances initially inherited a default white texture for `BaseColorTex`, despite the corresponding 4K color maps being present. `Tools/FixTripoMaterials.py` bound each existing color map to its character's material instance. Only the three material packages were saved; originals are preserved in `Art/Imports/BridgeOriginalMaterials`. `Saved/TripoMaterialRepair.json` records each previous binding, replacement texture, and backup. The subsequent inspection confirms the saved `BaseColorTex` overrides. This is a verified binding repair, not visual material acceptance.

`Tools/RetargetTripo.py` used the installed UE 5.8 IKRig editor APIs to create automatic source/target characterizations, FBIK solvers, exact chain mappings, and target poses aligned with `ChainToChain`. The new assets live under `/Game/Art/Characters/TripoRetarget/Preview01`. The original Manny animations, imported meshes, and imported skeletons were not modified by this build.

| Character | Imported height | Runtime review height | Uniform mesh scale | New locomotion BlendSpace |
|---|---:|---:|---:|---|
| Warden | 97.6819 cm | 184 cm | 1.8837 | `/Game/Art/Characters/TripoRetarget/Preview01/Warden/Animations/BS_Idle_Walk_Run_Warden` |
| Ranger | 97.7457 cm | 178 cm | 1.8211 | `/Game/Art/Characters/TripoRetarget/Preview01/Ranger/Animations/BS_Idle_Walk_Run_Ranger` |
| Scholar | 97.7823 cm | 176 cm | 1.7999 | `/Game/Art/Characters/TripoRetarget/Preview01/Scholar/Animations/BS_Idle_Walk_Run_Scholar` |

The build saved 61 new assets (about 19.8 MB): one source IK Rig, three target IK Rigs, three retargeters, 51 animation sequences, and three 2D BlendSpaces. Each BlendSpace has 27 samples backed by 17 unique clips: idle and eight directions of walking/jogging. Every sample references a generated clip with the exact corresponding target skeleton; all sequences have nonzero durations (1.5–7.5667 seconds). Input is `Vector(direction_degrees, speed_cm_per_second, 0)`, with direction −180..180, walking at 300, and jogging at 600. The runtime review helper uses these assets under `-CireTripoChampions`; it keeps the original mannequin when validation fails and never assigns Manny's animation blueprint to a Tripo skeleton.

`Saved/TripoRetargetBuild.json` and `Saved/Logs/TripoRetarget-Build.log` record the successful commandlet (exit 0, zero errors). Reported warnings concerned absent source animation curves, imported animation dependency loading, and the existing drive-journal warning. Automatic rig matching and successful export do not establish good deformation or visual quality.

### Scale, orientation, and deformation review status

The current scale approach is consistent with the engine implementation: `PelvisMotionOp` normalizes source movement using the ratio of target/source pelvis heights, and `RootMotionGeneratorOp` applies the same ratio to root translation. Uniform component scale then restores the imported body to the declared runtime height. This avoids applying Manny-sized translations directly to the smaller imported skeleton. The small differences in final body/leg proportions still require stride-speed review.

The reported bind-pose mesh bottoms are effectively Z=0. The runtime helper places this bottom at the capsule bottom and retains the original mesh-component rotation. The root-motion retarget operation uses its default `CopyFromSourceRoot` and `CopyHeightFromSource` settings; pelvis floor-constraint weight is zero, and there is no explicit `SnapToGround` operation in this build. Grounded bind bounds therefore do not prove that animated feet stay planted. Runtime animation uses `IgnoreRootMotion` so Character Movement retains capsule authority.

Facing remains unverified: bounds show the expected wide horizontal arm span but cannot distinguish forward from backward. Review front/back and forward/side/backward locomotion before changing component yaw. The eight imported twist bones are outside the generated retarget chains; their inherited transforms and automatic skin weights need wrist, forearm, shoulder, hip, and knee inspection. The generated spine mapping explicitly spans source `spine_01..spine_05` to target `spine_01..spine_03`. It interpolates chain rotations rather than assigning the incompatible source hierarchy directly.

Required next visual checks are feet at rest and during gait, stride sliding at actual game speed, facing during turns and strafing, shoulder/forearm twisting, bent knees, and the Scholar's coat clipping. The imported physics assets and close-view anatomy also remain unaccepted. Attack, cast, hit, death, and other combat animation coverage are separate follow-up work; the current retarget set is locomotion only.

### Historical ordinary browser download blocker

The normal Ash Ranger export navigated to `tripo-data.rg1.data.tripo3d.com`. Chrome displayed **"This page has been blocked by Chrome"** and **`ERR_BLOCKED_BY_CLIENT`**. The observed ZIP path was `/tripo-studio/20260924/feeff4e1-eb0c-4102-bfb8-9428912de7c0/tripo_convert_feeff4e1-eb0c-4102-bfb8-9428912de7c0.zip`, with filename `Cire_AshRanger_UE5_Rig.zip`. Expiring signed authorization query values are intentionally omitted; this path is diagnostic information, not a reusable download link.

The Veil Scholar normal export reached the same blocked host and exact Chrome error. Its observed ZIP path was `/tripo-studio/20260924/50d9ddf9-4a35-4379-8c57-10dbe1f70441/tripo_convert_50d9ddf9-4a35-4379-8c57-10dbe1f70441.zip`, with filename `Cire_VeilScholar_UE5_Rig.zip`; signed query values are likewise omitted. Both model pages were left available for handoff. All three assets remain in the signed-in Tripo account.

The specific blocking extension, filter or policy has **not** been identified. This affected the ordinary browser ZIP download route; the official Unreal Bridge subsequently transferred and imported all three models successfully. No Chrome internal pages, extension settings, security controls or alternate manual HTTP download routes were used. Ordinary ZIP downloads remain unverified, while the Bridge imports above are complete. Animation and gameplay acceptance are separate remaining checks.

The signed-in Tripo account is the requested generation source. These prompts and checks are a production brief, not proof that a model was generated, exported, rigged, or imported. Record those stages separately per asset. Generate one hero through the complete workflow before commissioning a large batch.

## First three human briefs

| ID | Working identity | Silhouette and function |
|---|---|---|
| CH_Human_Bastion_01 | Bastion; STR tank affinity | Broad, grounded silhouette; practical segmented steel plate over padded cloth; uncovered joints; short cloth panels. |
| CH_Human_Stalker_01 | Veilwalker; AGI damage affinity | Lean athletic silhouette; layered dark leather and fitted brigandine; visible hands and ankles; light equipment. |
| CH_Human_Cantor_01 | Ember Cantor; INT support affinity | Medium build; fitted linen and leather under narrow ceremonial armor; split knee-length coat; simple bronze focal ornament. |

Working names are original placeholders and can change. Role affinity does not grant innate skills; actual abilities follow the draft rules in `Design.md`.

### Shared prompt foundation

> One original adult human playable character for a realistic dark fantasy game. Full body, neutral symmetrical A-pose, arms separated from the torso, fingers clearly separated, feet shoulder-width apart. Believable human anatomy and proportions, physically plausible worn medieval equipment, carefully defined material differences between metal, cloth, leather and skin. Practical armor articulation at shoulders, elbows, hips and knees. Simple readable silhouette. Neutral expression, closed mouth. Even neutral lighting, plain background. No base, no scenery, no text, no logo, no floating effects, no extra limbs. Empty hands; weapons will be separate assets. No oversized cartoon proportions or chibi features.

Append exactly one role block:

**Bastion:** “An adult human frontline guardian approximately 185 cm tall, athletic sturdy build, original angular breastplate, rounded articulated shoulder guards, dark charcoal padded underlayer, muted oxblood cloth accents, weathered steel with restrained edge wear, closed practical boots, face visible, short hair. Minimal dangling straps and no long cape.”

**Veilwalker:** “An adult human skirmisher approximately 178 cm tall, lean athletic build, realistic fitted charcoal leather armor with small riveted steel plates, desaturated moss cloth accents, compact forearm bracers, fitted boots, visible face with short tied hair. No cloak, no hood hiding the entire face, no weapons fused to hands.”

**Ember Cantor:** “An adult human battlefield healer approximately 175 cm tall, natural adult proportions, fitted ash-colored layered garments, split knee-length coat allowing leg movement, narrow antique bronze armor segments, muted warm ivory and dark rust accents, small original geometric breast ornament, visible face, hair secured close to the head. No luminous magical effect, no oversized sleeves, no long train.”

These are modeling prompts, not guarantees of topology, texture quality, exact triangle counts, clean fingers, or working rigs. Judge the result visually and in motion.

## Generation and handoff sequence

1. Generate the human body/clothing concept; inspect front, back, sides, hands, feet, joint clearance, and symmetry. Reject fused legs, unstable anatomy, duplicated gear, or unreadable silhouettes early.
2. Finish mesh edits, segmentation, and retopology before final rigging. Tripo's [rigging documentation](https://docs.tripo3d.ai/animation/rig-v1-0-20240301.html) explicitly warns that relevant mesh-processing steps require rigging/retargeting afterward. Its documented rig task supports biped and FBX/GLB output; available browser controls may differ.
3. Produce a manageable deformation mesh with clean elbow, knee, shoulder, hip, wrist, and finger loops. Retain the high-detail source for baking. Check UVs and bake material maps after topology stabilizes. Use manual DCC cleanup where automated output fails.
4. Run the human/biped rig option exposed by the account. Preview walking, running, turning, arm raise, crouch, attack, and casting. Fix skin weights before accepting the mesh. A successful auto-rig job is not animation acceptance.
5. Export a skeletal FBX with textures and a recorded bind pose; preserve a source GLB if useful. Verify export contents rather than assuming the extension includes a skeleton. Tripo's [Studio tutorial](https://www.tripo3d.ai/blog/tripo-studio-tutorial-english) describes generation, editing, retopology, rigging, and engine export.
6. Import into UE 5.8.3 in a dedicated test folder first. Epic's [FBX skeletal mesh pipeline](https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-skeletal-mesh-pipeline-in-unreal-engine) specifies FBX 2020.2 for its pipeline and supports skeleton-associated meshes and LODs. If the exporter uses another version, verify compatibility or convert deliberately.
7. Create source/target IK Rigs and a retargeter rather than assigning an incompatible skeleton by name. Epic's [IK retargeting documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/ik-rig-animation-retargeting-in-unreal-engine) describes mapping chains and pelvis; differing hierarchies can require retargeting work. Use animation assets with documented rights.
8. Only promote to the game content directory after the checks below pass. Keep raw exports outside cooked content and preserve the accepted source separately.

## Provisional technical budgets

These are starting targets for testing, not Tripo capability promises or final performance requirements.

| Item | Starting target |
|---|---|
| Hero mesh | 45–65k rendered triangles at LOD0; then roughly 50%, 25%, and 10% of LOD0. Validate silhouette and deformation per LOD. |
| Common enemy | 12–25k at LOD0; aggressive distance LODs, tested at representative wave counts. |
| Textures | 2K shared/body sets for first integration; 4K only where close-up benefit and memory budget justify it. |
| Materials | Aim for 2–4 slots per hero; avoid an independent material for each armor plate. |
| Scale/transform | Unreal centimeter scale; declared character height; identity import transform after validated conversion; feet sit on ground. |
| Rig | Single stable hierarchy; root/pelvis behavior documented; weapon sockets separate; collision driven by a tuned capsule/physics asset. |
| Movement | Start with in-place locomotion and Character Movement authority; introduce root-motion actions only with deliberate network handling. |

Minimal animation coverage: idle, walk/run forward and backward, strafe, turn, start/stop, jump/land if supported, attack, cast/channel, interrupt, hit reaction, death, revive, and a readable defensive action. Skills can share appropriate animation families initially. LOD reduction must preserve joints, important sockets, and silhouette.

## Import acceptance checklist

- Human anatomy and fingers pass close inspection; no fused limbs or accidental equipment duplicates.
- Intended height matches a reference capsule; forward/up axes and ground contact are correct; no 100× scale error.
- Mesh normals/tangents and UVs render correctly; no unexpected transparency; base-color and non-color maps use appropriate texture settings.
- Material response holds up under neutral lighting and the intended battlefield lighting; team accents remain distinguishable.
- Skin does not collapse at elbows/knees/shoulders; feet do not visibly slide or penetrate during locomotion; weapon grip/socket alignment is credible.
- Collision permits navigation and does not expose gameplay hit logic to decorative spikes or cloth.
- Animation state changes do not freeze or pop; root motion does not fight movement replication.
- All LODs, shadow behavior, texture streaming, and representative ten-player plus wave load are profiled in the actual scene.
- Asset manifest records prompt/reference, Tripo job/model ID, export date, source file, imported asset path, cleanup tool, skeleton, texture sets, rights/provenance, and QA result.

“Generated,” “downloaded,” “imported,” “animated,” and “accepted for production” are separate states. Report the highest state actually verified.

