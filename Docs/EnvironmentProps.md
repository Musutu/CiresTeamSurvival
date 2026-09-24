# Authored battlefield props

`Content/Data/EnvironmentPlacements.json` defines eight original supplementary
models and 80 placement rows, mirrored across the two PvE lanes. Anchors can follow
the town, monster spawn, challenge tiers, perimeter or outer skyline. All transforms
and asset paths are validated, with limits of 32 models and 256 rows.

The original meshes include separate barrel staves and iron hoops, framed supply
crates, forged brazier ribs and coals, wedge-stone arches, bevelled courtyard stairs,
branching dead oaks, oath obelisks and watch lanterns. They reuse the project's
authored masonry, timber, slate, metal and warm emissive materials. Sources are in
`Art/Environment/Props01Sources`; `Tools/BuildWorldProps.py` imports only into
`/Game/Art/Environment/Props01` in an isolated builder. Source packages from Tripo
are not modified.

`CireEnvironmentProps` uses hierarchical instances and source-mesh materials. Every
placement is checked against the current world's route, spawn, challenge positions
and defended town entrance. A conflicting prop is omitted. The checks run again
when a developer edits the route; the existing privacy wall and route geometry
remain intact. Blocking props use their mesh collision away from protected travel
and spawn areas; distant trees are decorative.

Tripo town-gate, watchtower and challenge-shrine requests remain explicitly pending
in the data until their actual imported packages can be bound. These authored props
are supplementary environment detail, not finished AAA environment art.

Asset generation passed: eight meshes imported and saved, then transferred from
the isolated builder with byte-identical SHA-256 checks. `Saved/WorldPropsBuild.json`
records the package paths and triangle counts. Native build/render validation passed.
`Tools/RunEnvironmentGallery.py`
now requires more than 40 modeled prop instances, validates their clearances, runs
the original route capsule sweeps and captures four environment views for review.

September 24 result: 44 checks and all four captures passed at
`Saved/EnvironmentGalleryChecks/20260924T053403053686Z/report.json`. All four images
were reviewed: town clutter, challenge arches, perimeter lights and skyline props
are visible; the defended town entry and winding road remain readable and clear.
Runtime placed 156 instances and suppressed four for route clearance. The generator
now wraps tree yaw to 0..359 degrees and validates every numeric field before saving,
after the first render exposed out-of-range rotations. The three generated Tripo
landmarks remain import-pending and were not represented as completed environment art.
