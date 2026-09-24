# Creature and environment playtest review

Reviewed September 24, 2026. This accepts a bounded prototype checkpoint, not finished
AAA assets or animation. No additional Tripo generation was used for these fixes.

## Evidence

- Initial five-profile mobility fixture: `Saved/BatchArtGalleryChecks/20260924T053138400847Z/report.json`,
  175 native checks, 24 captures. Exact bodies, pose state and finite geometry passed.
  Visual review found white custom-creature materials and withheld approval.
- Corrected three-creature fixture: `Saved/BatchArtGalleryChecks/20260924T054036214600Z/report.json`,
  117 checks, 16 captures; all processes exited normally. Captures are in
  `Saved/BatchArtGallery/20260924-054047-43B1DD/`.
- Material-copy build: `Saved/CreatureSurfacesBuild.json`, three body copies and three
  material copies, 1,101 preexisting packages preserved. Each material uses the sole
  verified sRGB BaseColor texture in its original UUID-bound import folder.
- Environment: `Saved/EnvironmentGalleryChecks/20260924T053403053686Z/report.json`,
  44 checks, four captures in `Saved/EnvironmentGallery/20260924-053414/`.

## Findings

Lancer and Summoner retain their distinct imported armored bodies and correct
textures. Reviewed idle, walking, attack release, airborne and mid-roll frames show
no severe body collapse or floor penetration. Their open fingers and weapon grip
alignment remain visible prototype limitations. The tumble is a local procedural
pose, not an authored motion-capture clip.

Bear now shows the actual textured quadruped, never the knight fallback. The
diagonal leg motion and front-paw attack are distinct from idle. The imported right
rear leg has only one useful hip joint, so its swing remains rigid. Centaur displays
the actual four-legged imported body, lowered arms, limited gait and casting motion.
Its spatial deformation needs a dedicated rig before production-quality animation.
Whisp shows the actual lantern-spirit body, pale green/cream source texture, hovering
and casting displacement. Its very light source palette and overlapping gallery
label remain presentation polish; no missing source texture is silently substituted.

All four environment captures were inspected. Barrels/crates frame the town without
obstructing its defended entry, arches and obelisks identify challenge areas, and the
road remains readable. Route capsule/clearance checks passed. 156 supplementary
instances are present; four unsafe placements were omitted. The large privacy wall
and repetitive building shapes are intentionally still prototype environment art.

The gatehouse, watchtower and shrine generated in Tripo are still awaiting verified
imports and do not block this playtest. Their UUIDs and pending states are retained
in `Art/TripoEnvironmentImportRequests.json`.

## Capture tooling correction

Two early combined attempts only rendered Lancer because Unreal FParse stopped at
the first comma. Their validators correctly failed the requested profile coverage.
The native parser now explicitly allows commas; the complete 24-capture run above
supersedes those attempts. No failed run was relabeled as passed.
