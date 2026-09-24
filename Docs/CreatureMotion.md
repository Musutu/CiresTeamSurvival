# Custom creature presentation

Bear, Whisp and Evergrove Centaur now have explicit `custom_ready` entries in
`Content/Data/ChampionArtBindings.json`. The custom adapter never substitutes a
humanoid body for these profiles if loading fails. Their gameplay capsules,
movement, targeting and replication remain owned by the hero. Presentation is
local, collision-free, and inherits the existing realm visibility parent.

* Bear uses the Tripo quadruped, scaled to 148 cm (x1.15 in game as a Tank). Since
  September 24 (camera-movement pass) it binds `/Game/Art/Characters/Motion03/SK_BearMotion`,
  a 49-bone copy whose right rear leg has a real knee/hock/paw chain (see below).
  Native local-pose evaluation drives a diagonal trot, head movement and a front-paw swipe.
* Whisp uses the actual lantern-spirit mesh at 85 cm plus a 65 cm hover offset,
  with floating motion, travel lean and a cast surge.
* Centaur uses a generated CPU-readable copy of the original static model,
  scaled to 236 cm. A bounded 30 Hz spatial deformation moves four leg regions
  independently, relaxes the original T-pose arms, and extends the casting arm.
  Original UVs, materials and topology are retained. This is prototype motion;
  it needs a dedicated skeleton and authored gait for final animation quality.

`Tools/InspectCreatureMotion.py` reads staged sources in the isolated
`Tools/CreatureBuilder` project. `Tools/BuildCreatureMotion.py` creates only
`/Game/Art/Characters/CreatureMotion01/SM_CentaurMotion`; original packages are
hash-checked and unchanged. The generated mesh contains 7,056 section vertices
and 5,016 triangles. No downloaded motion or paid generation is used.

Humanoid locomotion also receives a local airborne limb pose and a pelvis-centered
tumble from replicated `UCireMobility` state. Neither changes the gameplay capsule
nor the mesh offset cached for network smoothing.

Validation: asset generation, file preflight and Unreal 5.8.3 compilation passed.
Run `Tools/RunBatchArtGallery.py --profiles
bear,whisp,evergrove_centaur` after the coordinated build. This uses real profile
drafting, confirms exact custom bodies, checks finite geometry and sampled movement,
and captures ten images across idle, walk, attack and recovery states. Image review
is still required; structural checks do not establish finished animation quality.

Add `--mobility` to capture walking speed, an airborne pose and the middle of a
dodge. For example, `--profiles lancer,summoner,bear,whisp,evergrove_centaur
--mobility` produces 24 captures across three pages. The fixture sets replicated
mobility presentation state on isolated heroes; movement authority, cooldown and
invulnerability remain covered separately by the gameplay smoke fixture. Mid-roll
limb samples are constrained above the presentation floor before visual review.

The September 24 gallery caught missing imported `BaseColorTex` assignments on
all three custom bodies. `Tools/RepairCreatureSurfaces.py` creates three copied
bodies and three copied material instances under `/Game/Art/Characters/CreatureSurfaces02`,
assigning each import's verified sRGB color texture. The final bindings use those
copies; the Centaur copy remains CPU-readable. All 1,101 existing main-project
packages were hash-checked unchanged, and the six published packages were verified
byte-identical to their isolated-builder outputs. Original imports remain intact.

The combined five-profile mobility fixture passed 175 checks and 24 captures.
After the material correction, the three-creature repeat passed 117 checks and
16 captures (`Saved/BatchArtGalleryChecks/20260924T054036214600Z/report.json`).
Reviewed idle, gait, attack, airborne and dodge samples show the correct bodies,
materials and finite grounded geometry. Bear has its textured brown fur/armor;
Centaur has its textured armor/body; Whisp retains its pale green source appearance.
Humanoid weapon grips, Bear's incomplete rear-leg rig and Centaur's spatial skin
remain prototype limitations. This is suitable for the current playtest, not final
animation acceptance. Detailed evidence is in `Docs/WorldCreatureVisualReview-20260924.md`.

## Bear gait pass (Motion03, September 24)

The user reported that the Bear "moves incorrectly". Findings and fixes:

* **Rigid right rear leg.** The import only had `1_Right_Limb_0` on that leg. `Tools/BuildBearMotion03.py`
  (run in an isolated CreatureBuilder copy with GeometryScripting + MeshModelingToolset) duplicates
  SK_Bear and its skeleton into `Content/Art/Characters/Motion03/`, mirrors `1_Left_Limb_1..4` onto the
  right hip and copies the mirrored left-rear skin weights to 1,226 right-rear vertices (mean mirror
  distance 0.7 units, max 2.4). Originals are untouched; `ChampionArtBindings.json` now points at the copy.
  The animation code only rotates bones that exist, so the old body still works.
* **Foot sliding.** The old phase advanced one cycle per 175 cm with a +/-15 degree sinusoidal swing, so a
  planted paw covered roughly half the body travel. The phase rate is now derived from the leg length
  (reference skeleton hip-to-paw height x mesh scale x actor scale) and the swing amplitude: one cycle
  covers `4 * L * sin(a)`. Stance is a linear back-sweep, swing an eased return with knee/hock and
  elbow/wrist fold, and the body is lowered by `L(1-cos a)` so planted paws stay on the floor.
* **Idle/walk/run blend.** Amplitude scales from 15 degrees (walk) to 30 degrees (run) with speed; idle keeps
  a breathing/head sway that fades out while moving. Backpedalling runs the cycle in reverse and
  turning in place steps the legs.
* **Facing** was verified correct: the skeleton's head is +Y, the mesh is yawed -90 so it faces actor +X.

Evidence: the native movement suite now includes `UCireCreatureArt::RunGaitSmoke`, which walks the real
Motion03 rig at 240 and 520 cm/s and measures planted-paw ground speed as a fraction of body speed.
Result (`Saved/ExpansionChecks/20260924T084431773010Z`): front paws 0.18/0.22 at walk and 0.14/0.15 at run,
rear paws 0.17/0.17 at walk and 0.02/0.02 at run (1.0 = paw dragged with the body; the old stride math
was roughly 0.5); both rear knees now bend through 28 degrees (right rear was 0). Rendered captures:
`Saved/BatchArtGallery/20260924-084521-FE24EA` (idle, walk, attack, airborne, roll; Bear, Centaur, Whisp).
Limits: still procedural animation on an auto-generated rig; no authored gallop, no IK on slopes, and
lateral strafing slides the paws.
