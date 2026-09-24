# Ability targeting

`CireTargeting::Describe(skillId)` provides the runtime target category, readable label, cast range in centimeters, and any ground footprint. HUD labels distinguish Self, Ally with Self fallback, Enemy, and Ground. Ally fallback means no friendly unit was selected; a selected unavailable or out-of-range ally is rejected by the ability handler. Enemy skills require a living hostile selection in range. Self skills use their own authoritative implementation regardless of selection.

Ground abilities, projectile aim, and construct/summon placement arm when their hotkey or action-bar slot is pressed. A filled green/red ground preview follows the cursor. Left click on valid battlefield ground confirms; Esc or right mouse cancels. Menus, chat, death, possession changes, and phase transitions remove the preview. A separate click is required after the initial arming input. The optional saved `bQuickGroundCast` preference casts directly at the cursor and defaults to false for both new and existing profiles.

The preview is a transient local actor with collision, shadows, replication, and navigation disabled. It cannot apply damage. Material references are retained by the component, and mesh sections rebuild only when boundary dimensions or validity colors change. Area boundaries use `ACireAreaEffect::BoundaryPoints`, including concave custom polygons. Circle/square/custom areas center on the cursor; cones and lines anchor at the caster and face the cursor. Projectile strips show the collision diameter and maximum forward travel allowed by speed, lifetime, and range, with standalone developer radius overrides applied. A projectile may hit an intervening unit or obstacle before the displayed travel endpoint. Summon circles indicate the placement spread, not a damaging area.

Both local preflight and the server RPC check floor support, active realm/phase, range, and line of sight. Constructs also check their oriented footprint against realm edges, town, level support, units, and obstacles. Actual server ability handlers independently validate ownership, costs, cooldowns, collision, and effect limits before paying resources.

`RunDescriptorSmoke()` validates metadata and boundary parity. `RunRuntimeSmoke(Mode)` uses a temporary raised platform to exercise accepted aim, out-of-range and opposite-realm rejection, absent floor, a blocking wall, construct clearance, harmless local geometry, cancellation/death cleanup, and accepted/rejected authoritative RPCs. These are native checks; visual/input capture still requires a rendered run after compilation.

The rebuilt native run recorded 17 descriptor checks and 18 runtime checks,
both passing, in `Saved/ExpansionChecks/20260924T053213951677Z/report.json`.
That run also passed all 28 role-skill checks, including Starfall's direct-cast
LOS regression. The earlier `20260924T024718203141Z` batch passed network
expansion. These checks establish the tested native behavior, not a completed
visual inspection of the cursor preview.
