# Integrating a verified Tripo Bridge batch

`Tools/IntegrateTripoBatch.py` consumes the existing cloud manifests and an explicit local Bridge binding manifest. It never purchases or regenerates art, opens the Bridge, downloads a model, or guesses a model folder from a generated name. The original Warden, Ranger, and Scholar meshes, skeletons, materials, locomotion, and attack assets are protected.

After a successful Bridge import, record the UUID and the observed saved folder in `Art/TripoImportBindings.json` (or pass another manifest with `--bindings`). Use actual observed paths, not the illustrative folder names below:

```json
{
  "schemaVersion": 1,
  "batch": "Batch01",
  "bindings": [
    {
      "uuid": "2ef5bf56-708e-436b-ac9a-1086e61faa41",
      "importFolder": "/Game/TripoModels/VERIFIED_LANCER_FOLDER",
      "mesh": "/Game/TripoModels/VERIFIED_LANCER_FOLDER/VERIFIED_MESH",
      "profileIds": ["lancer"],
      "attackPose": "Lancer",
      "heightCm": 182
    },
    {
      "uuid": "39a83098-f50e-4c5f-a955-a5decdd99d4a",
      "importFolder": "/Game/TripoModels/VERIFIED_SUMMONER_FOLDER",
      "mesh": "/Game/TripoModels/VERIFIED_SUMMONER_FOLDER/VERIFIED_MESH",
      "profileIds": ["summoner"],
      "attackPose": "Scholar",
      "heightCm": 176
    }
  ]
}
```

UUIDs must match `Art/TripoHumanoidBatch.json`, `Art/TripoCreatureBatch.json`, or the Summoner entry in `Art/TripoManifest.json`. A UUID cannot bind two folders and two UUIDs cannot share a folder. The explicit `mesh` field may be omitted only if Unreal finds exactly one SkeletalMesh under that folder. `profileIds` must match the cloud manifest's role. A shared body may bind both Paladin profiles or both Troll Berserker profiles. `heightCm` is the intended rendered height, 80–400 cm; imported mesh units are measured independently.

The first pass selects Lancer and Summoner. `--profile profile_id` can select another explicitly bound humanoid, and `--all-humanoids` requires bindings for every new humanoid in the manifests. The latter reports all missing bindings before attempting a build. Bear, Whisp, Centaur, and Dragon are explicitly deferred to their custom creature pipelines; a cloud skeleton alone does not qualify them for Manny retargeting.

## Preflight

Ordinary Python can inspect manifests and saved package presence without loading Unreal:

```powershell
& 'F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe' Tools/IntegrateTripoBatch.py
```

This is read-only by default and prints JSON. `files_present_engine_validation_pending` is not an engine validation pass. Missing imports are aggregated, and no runtime binding or output asset is written. `--report PATH` explicitly saves a diagnostic JSON report.

The same script in an Unreal Python commandlet performs additional read-only asset checks: actual SkeletalMesh type, required bones, supported root hierarchy, plausible bounds, humanoid auto-characterization, exact retarget-chain mappings, and material repair feasibility. It calls the established `RetargetTripo.configure_rig` and `configure_retargeter` helpers on transient assets. The source files remain unchanged.

Use the existing commandlet launch pattern with `-run=pythonscript -script="<absolute path>/Tools/IntegrateTripoBatch.py" -unattended -nullrhi -nosound -NoLiveCoding`. Commandlet-safe flags avoid nesting script argument quotes:

- `-CireTripoBatchBindings="<absolute binding manifest path>"`
- `-CireTripoBatchLabel=Batch01`
- `-CireTripoBatchAllHumanoids` (optional)
- `-CireTripoBatchReport="<absolute diagnostic report path>"` (optional)

No mutation occurs without the explicit build flag. Do not run these commandlets while a build or another asset writer is active.

## Build and verify

1. Run the commandlet preflight and resolve all reported issues.
2. Run a new commandlet with `-CireTripoBatchBuild` (or script `--build`). All output goes into the fresh `/Game/Art/Characters/TripoBatch/Batch01` namespace. Existing output is never reused or overwritten; select a new batch name after a partial build.
3. Close that process. Run a fresh commandlet with `-CireTripoBatchVerify -CireTripoBatchLabel=Batch01` (or `--verify --batch Batch01`). This reloads saved assets and publishes runtime mappings only after every required check succeeds.
4. Render and inspect idle, walking, running, windup, release, recovery, and weapon attachment for the new bodies. `ready` means the saved assets passed structural and pose validation; the build report retains `visualReviewAccepted: false` until a separate visual review establishes acceptance.

The build first duplicates each new imported body into its batch. Missing `BaseColorTex` values (including the Bridge default white placeholder) are repaired only in copied material instances, and only when exactly one sRGB `*_BaseColor` texture exists in the bound import's `Textures` folder. Existing real color assignments and other material channels remain unchanged. Ambiguous candidates stop preflight. Original material packages are retained as immutable backups under `Art/Imports/TripoBatchMaterialBackups/<uuid>`; a differing existing backup is never overwritten. Neither the original imported mesh nor its original material is saved.

`RetargetTripo.run` creates a fresh raw directional BlendSpace and its 17 referenced locomotion clips for each body. `RepairTripoAnimationScale.repair_clip` then repairs new locomotion copies when the imported reference root is 100 and retargeted animation root is 1, dividing direct-root-child translations to preserve component trajectories. Unit-scale rigs already matching the reference are kept intact. Unsupported scales or hierarchies stop the build. No skeleton reference scale is changed.

`BuildAttackAnimations.authored_poses` and `sample` supply the existing local articulated prototype attacks. Supported pose families are Warden, Ranger, Scholar, and Lancer; these describe provisional motion style, not final bespoke class animation. Summoner uses the Scholar casting family on its own skeleton. The script writes new AnimDataController tracks from the repaired idle, preserving all local translations/scales, with a 0.25-second release and a 0.65-second gameplay duration. It does not regenerate weapons.

Validation covers matching mesh/BlendSpace/sequence skeletons, all 17 saved dependencies in the repaired folder, every raw root key's reference scale, finite raw and compressed bone positions, head/feet proportions relative to that body's own reference, preserved pelvis trajectories, and visible windup-to-release articulation. Hashes of every preexisting Unreal package are checked after building and again before publication. Reports live at `Saved/TripoBatchIntegration/<batch>.json`; failures never publish a runtime mapping.

Observed Bridge names containing hyphens are preserved. Filesystem hashing and material backups use Windows extended paths so long imported package names remain covered by the same protection checks; no imported package is renamed or excluded to avoid the path limit.

## Runtime contract

The separate verification pass atomically merges validated entries into `Content/Data/ChampionArtBindings.json`, retaining other profiles and preserving a prior-file backup. The runtime consumes only entries with `status: "ready"`:

```json
{
  "schemaVersion": 1,
  "bindings": [
    {
      "profileId": "lancer",
      "mesh": "/Game/Art/Characters/TripoBatch/Batch01/Bodies/lancer/SK_lancer.SK_lancer",
      "locomotion": "/Game/Art/Characters/TripoBatch/Batch01/Locomotion/lancer/Animations/BS_Idle_Walk_Run_lancer.BS_Idle_Walk_Run_lancer",
      "attack": "/Game/Art/Characters/TripoBatch/Batch01/Attacks/lancer/A_lancer_Attack.A_lancer_Attack",
      "heightCm": 182,
      "status": "ready"
    }
  ]
}
```

Success markers are `CIRE_TRIPO_BATCH_BUILD_PASS` (assets built, no runtime publication) and `CIRE_TRIPO_BATCH_VERIFY_PASS` (saved validation and publication). `CIRE_TRIPO_BATCH_FAIL` identifies a failed commandlet. The native runtime also validates the skeleton and locomotion relationship and keeps its existing fallback if a binding is unusable.

Run `Tools/TestIntegrateTripoBatch.py` with ordinary Python for offline manifest/no-overwrite/read-only/publication tests. These tests do not establish animation or visual acceptance; those require the coordinated Unreal passes above.

## Tripo champions (tripo-races, 2026-09-25)

`Content/Data/ChampionArt.tripo.json` maps the new-champions profiles (`gunblade`, `witch_slayer`, `huntress`,
`aetheri_artificer`, `aetheri_warden`) to their Tripo bodies under `/Game/Tripo/Champions/`. Each `champions[]` row is a
drop-in `ChampionArtBindings.json` row (`status: "ready"`: mesh, `locomotion` BlendSpace, `attack`, `heightCm`) plus
`yaw`/`animations` for the `monster_native`/`mounted` modes, its ChampionAttacks02 folder and its props.

- Locomotion: `Tools/BuildTripoChampionMotion.py` writes in-place copies of the native walk/run (Tripo bakes travel
  into the pelvis) and `BS_Idle_Walk_Run_<Name>` with the lancer's Direction x Speed axes, samples at the clips' own
  scaled ground speed.
- Actions: `Tools/RetargetChampionAttacks.py -CireChampionAttacksAdd` transfers slash / cast_a_spell / war_cry
  (+ attack_crossbow for the Witch Slayer, attack_bow for the Huntress) into `ChampionAttacks02/Tripo<Name>/`;
  the bodies are registered in `ChampionAttacks02.json` `bodies`.
- Props: WeaponLoadouts tokens `tripo/<Name>` resolve to `/Game/Tripo/Props/<Name>/CTS_Prop_<Name>`; handles live in
  `WeaponGrips.json` (`CTS_Prop_*`). `loadoutPresets` in the mapping file are ready-made presets.
- The Huntress's sabercat (`CTS_Mount_HuntressSabercat`) is rigged but has no clips; see the row's `mount.note`.
- Review render: `Tools/RenderTripoChampionLineup.py` (Saved/TripoChampionLineup/<stamp>/).
