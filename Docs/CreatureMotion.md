# Custom creature presentation

Bear, Whisp and Evergrove Centaur now have explicit `custom_ready` entries in
`Content/Data/ChampionArtBindings.json`. The custom adapter never substitutes a
humanoid body for these profiles if loading fails. Their gameplay capsules,
movement, targeting and replication remain owned by the hero. Presentation is
local, collision-free, and inherits the existing realm visibility parent.

* Bear uses the actual 45-bone Tripo quadruped, scaled to 148 cm. Native local-pose
  evaluation drives diagonal gait pairs, head movement and a front-paw swipe.
  The import has only a hip bone on its right rear leg, so that leg has a rigid
  swing. This is a known rig limitation, not a completed production quadruped rig.
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
