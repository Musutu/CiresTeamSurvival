# Movement feel: visual locomotion

Playtest 4 ruling: gameplay, casting and movement *feel* are solid and must not change; the *visual* locomotion of
champions and monsters looked clunky (skating feet, instant heading snaps, frozen turns in place). This pass is
presentation only. Capsules, speeds, input, AI steering and replication are untouched.

`cire.LocoFeel 0` (or `-CireLocoFeel=0` on the command line) restores the previous presentation exactly; the lab uses
it for before/after runs.

## What changed

| Piece | Where | What it does |
|---|---|---|
| Gait analysis | `CireLocomotion::AnalyzeGait` | Measures the ground speed a clip was authored for from its planted contacts (toes/balls, feet, or the lowest leaf clusters of non-humanoid rigs), per whole stance, touch-down to lift-off. A stance that runs across the loop seam continues from the next cycle, displaced by the clip's own travel. Replaces placeholder data speeds (e.g. 60 cm/s) that made gaits play far too fast or slow. |
| In-place loop detection | `CireAnimClips::Analyze` | A loop whose pelvis ends where it started is in place. A least-squares line through an asymmetric closed cycle had a spurious slope; removing it ramped the pose and popped it back at every loop seam, and it skewed the measured stride. Root-travelling (Tripo) clips keep the fitted drift. |
| Blend-space warp | `CireLocomotion::WarpBlendSpace` | Champion 2D locomotion BlendSpaces get the speed-axis position and play rate at which the blended stride matches the capsule's velocity in any direction. Grid rows whose clip is not a longer stride are kept monotonic, so the play rate does the matching. |
| Monster/creature cadence | `CireMonsterArt::UpdatePresentation`, `CireCreatureArt::UpdateNative` | Phase advances by velocity over measured stride (rate 0.3–2.5x). Bodies with a single gait clip (walk = run, e.g. the Undead zombie) may go to 4x, since they have no faster stride to blend to. |
| Visual heading | `CireLocomotion::FVisualTurn` | Travelling: a ~55 ms critically damped follow capped at 600 deg/s, so there are no one-frame snaps from mouselook, attack facing or steering jitter. Standing: feet stay planted for small turns, and larger or settled turns step round, driving a side-step leg cycle on champions. Applied to the root bone, never to the actor. |
| Travel warp | `CireLocomotion::TravelWarp` | Forward-only gaits (monsters) point their legs along travel within ±70 deg of facing. A held kite beyond ~120 deg (with hysteresis) backpedals, running the gait backwards. |
| Leg IK | `CireLocomotion::FLegIK` | Humanoid feet (`foot_l/foot_r` with thigh/calf/pelvis) follow slopes and steps with a pelvis drop and two-bone IK. |
| Network | `FPoseFeel::Resolve` | The root offset is resolved in anim PreUpdate against the mesh's current network-smoothed heading, so simulated proxies never show a rotation unsmoothed for one frame. Mesh ticks are ordered after the actor/presentation tick. |
| Vendor notifies | `UCireCombatAnimInstance::HandleNotify` | The GDH demo Blueprint notifies (`BP_AddWeapon` / `BP_RemoveWeapon`) spawned a colliding `SM_Spear` actor on the mesh and pushed the capsule. Non-native notifies are now skipped and logged once as `CIRE_VENDOR_NOTIFY_SKIPPED`. Native notifies (sounds) still run. `-CireVendorNotifies` restores them for evidence runs. |

## Locomotion lab

`Tools/RunLocomotionLab.py` (fixture `Source/CiresTeamSurvival/CireLocomotionLab.cpp`) runs every subject through a
scripted 60 Hz course. The segments are: idle, start, stop, run, cruise, 90-degree turn, reversal, strafe, backpedal,
turn in place (smooth and snapped), a 15-degree hill and a cornered path. It writes
`Saved/LocomotionLab/<stamp>-<tag>/{metrics.json, summary.md, samples/, sheets/}`.

```
python Tools/RunLocomotionLab.py                          # after (current presentation), with contact sheets
python Tools/RunLocomotionLab.py --before --no-capture    # previous presentation
python Tools/RunLocomotionLab.py --compare <before> <after>
python Tools/RunLocomotionLab.py --net --local <after-dir> --port 17150   # dedicated server + remote client (proxies)
```

Use the UE Python (`F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe`). Contact sheets need Pillow in
`Saved/pylib`.

Metric notes:
* **footSlide** is the world drift of each contact across a stance, in cm/s. On moving segments, the stance band is
  capped at a third of the contact's lift range, so a dragging gait's swing is not counted as stance.
* **Hill.** The stance-window footSlide on the hill spans flat ground and both ramps and is noisy. Use the fixture's
  per-frame `slip` and `groundErrMean` there.
* **Pops** count joint accelerations above an absolute 3 cm/frame² (height-scaled). Stride-matched cadence is faster,
  so it raises this count without a visible pop. Read it with `jerkMax`.
* **Yaw snaps** in the net compare are a rate (≥ 900 deg/s, i.e. 15 deg in a 60 Hz frame). A client drawing at 30 fps
  is not charged for the same turn in bigger frames.

### Results (2026-09-26, F: worktree, 9 subjects)

The before run is `20260926-053053-before`; the after run is `20260926-054905-after2`. Foot slide is the mean over the
run, cruise, turn90, reverse, strafe, backpedal and corners segments.

| Subject | Foot slide (cm/s) before → after | Cut | Slide / speed | Yaw snaps |
|---|---|---|---|---|
| hero:lancer | 414 → 145 | 65% | 0.86 → 0.29 | 46 → 0 |
| hero:knight | 330 → 138 | 58% | 0.67 → 0.27 | 46 → 0 |
| hero:bear | 524 → 189 | 64% | 1.12 → 0.40 | 46 → 0 |
| monster:ironbound_bruiser | 135 → 77 | 42% | 0.82 → 0.43 | 0 → 0 |
| monster:hollow_infantry | 267 → 31 | 88% | 1.41 → 0.15 | 0 → 0 |
| monster:dire_wolf | 413 → 48 | 88% | 1.85 → 0.19 | 0 → 0 |
| monster:rotting_shambler | 125 → 36 | 71% | 0.79 → 0.22 | 0 → 0 |
| monster:feral_mammoth | 564 → 26 | 95% | 3.90 → 0.18 | 0 → 0 |
| monster:hollow_siegebreaker | 72 → 17 | 76% | 0.58 → 0.12 | 0 → 0 |

Total yaw snaps: 138 → 0.

**Rotting shambler.** The Undead zombie `Anim_Walk` is an in-place 1.53 s loop with a single gait (walk = run) and
~3 cm foot lift, carried at 175 cm/s. The false pelvis drift popped both feet ~23 cm at every seam, and the stride
read 41 cm/s instead of 52 cm/s. With in-place detection and the single-gait 4x cadence, its slide fell from
125 → 36 cm/s. Pops fell from 104 → 28, and cruise jerkMax from 43 → 6.8.

**Bruiser.** Its segments swap places from run to run (turn90 19 ↔ 112, reverse 106 ↔ 23 cm/s); treat ±15% as noise.

### Network proxies (`20260926-060039-net-client`, dedicated server + remote client at ~33 fps)

| Subject | Yaw snaps local → proxy | yawAcc p99 | meshAcc p99 |
|---|---|---|---|
| hero:bear | 0 → 0 | 10327 → 16323 | 7412 → 7934 |
| hero:knight | 0 → 0 | 10504 → 11714 | 7412 → 8493 |
| hero:lancer | 0 → 0 | 10504 → 11654 | 7412 → 7798 |
| monster:dire_wolf | 0 → 0 | 7960 → 8582 | 3411 → 3788 |
| monster:feral_mammoth | 0 → 0 | 5378 → 6909 | 2062 → 2336 |
| monster:hollow_infantry | 0 → 0 | 7743 → 8631 | 3117 → 3251 |
| monster:hollow_siegebreaker | 1 → 1 | 6461 → 7326 | 2062 → 2344 |
| monster:ironbound_bruiser | 1 → 1 | 7637 → 8821 | 2540 → 2818 |
| monster:rotting_shambler | 0 → 0 | 7374 → 8054 | 2194 → 2665 |

Proxies show the same heading behaviour as the standalone body. Mesh acceleration p99 is within about +20%; replicated
yaw arrives quantised (11.25-degree steps were seen) and the client samples at ~30 fps. The bruiser and infantry each
show one larger max spike (meshAccMax 5.2k → 10.5k and 5.6k → 8.9k). The p99 does not move with it, so it is a single
event, not a per-cycle pop. It is unexplained and worth a look with `--lag`.

## Known gaps

* Champions still slide about 0.27–0.29 of their speed. The GDH / MaleLocomotionSet blend grids have few speed rows,
  and the play rate is clamped at 0.5–1.75x.
* Turn-in-place drift rose, from ~0–6 cm/s to ~14–34 cm/s, because the legs now step instead of the whole body
  rotating rigidly. It shows as footwork, not skating.
* The shambler still slides a little when strafing (0.37 of speed). Its single forward gait is warped at most 70 deg
  toward travel.
