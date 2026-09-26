# Ability targeting

`CireTargeting::Describe(skillId)` provides the runtime target category, readable label, cast range in centimeters, and any ground footprint. HUD labels distinguish Self, Ally with Self fallback, Enemy, and Ground. Ally fallback means no friendly unit was selected; a selected unavailable or out-of-range ally is rejected by the ability handler. Enemy skills require a living hostile selection in range. Self skills use their own authoritative implementation regardless of selection.

Ground abilities, projectile aim, and construct/summon placement arm when their hotkey or action-bar slot is pressed. A filled green/red ground preview follows the cursor. Left click on valid battlefield ground confirms; Esc cancels, and so does a clean right click (a press released within 0.35 s without dragging, when Options > `bRightClickCancelsAim` is on). Moving, strafing and right-drag steering or panning never cancel an armed reticle (Eric's playtest-3 ruling; `CireController.cpp`). Menus, chat, death, possession changes, and phase transitions remove the preview. A separate click is required after the initial arming input. The optional saved `bQuickGroundCast` preference casts directly at the cursor and defaults to false for both new and existing profiles.

The preview is a transient local actor with collision, shadows, replication, and navigation disabled. It cannot apply damage. Material references are retained by the component, and mesh sections rebuild only when boundary dimensions or validity colors change. Area boundaries use `ACireAreaEffect::BoundaryPoints`, including concave custom polygons. Circle/square/custom areas center on the cursor; cones and lines anchor at the caster and face the cursor. Projectile strips show the collision diameter and maximum forward travel allowed by speed, lifetime, and range, with standalone developer radius overrides applied. A projectile may hit an intervening unit or obstacle before the displayed travel endpoint. Summon circles indicate the placement spread, not a damaging area.

Both local preflight and the server RPC check floor support, active realm/phase, range, and line of sight. Constructs also check their oriented footprint against realm edges, town, level support, units, and obstacles. Actual server ability handlers independently validate ownership, costs, cooldowns, collision, and effect limits before paying resources.

`RunDescriptorSmoke()` validates metadata and boundary parity. `RunRuntimeSmoke(Mode)` uses a temporary raised platform to exercise accepted aim, out-of-range and opposite-realm rejection, absent floor, a blocking wall, construct clearance, harmless local geometry, cancellation/death cleanup, and accepted/rejected authoritative RPCs. These are native checks; visual/input capture still requires a rendered run after compilation.

The rebuilt native run recorded 17 descriptor checks and 18 runtime checks,
both passing, in `Saved/ExpansionChecks/20260924T053213951677Z/report.json`.
That run also passed all 28 role-skill checks, including Starfall's direct-cast
LOS regression. The earlier `20260924T024718203141Z` batch passed network
expansion. These checks establish the tested native behavior, not a completed
visual inspection of the cursor preview.

## Tab targeting (WoW style)

`CireSelection::NextTarget` implements unit cycling. **Tab** picks the nearest living, observable
hostile inside the camera's horizontal view cone (half the FOV + 15 degrees, capped at 85), then cycles
outward by distance without repeating until every candidate in view has been visited, then restarts
from the nearest. **Shift+Tab** walks back through that tab history (then continues from the farthest).
The chain resets after 3 s without Tab or when the target changes by other means. If nothing hostile
is in view, the nearest hostile around the hero is chosen. Candidates must be within 2,500 cm, not
hidden, alive and pass `CireRealm::CanObserve`, so the other team's realm is never selected.
**F** cycles living allies by distance; **F1** selects yourself.

When a hostile target dies (or is destroyed) the selection is cleared. The optional
*Auto-target next enemy* preference (F9 → Controls, `bAutoReacquireTarget`, default off) instead
selects the next Tab target. Left click selects on button release only when the mouse did not drag
the camera, so a left drag can orbit the camera without dropping the target.

The runtime smoke adds 9 checks: cone preference over a closer hostile behind the camera, outward
cycling, wrap-around, Shift+Tab history, other-realm exclusion, all-around fallback, clear on death and
auto-reacquire. Latest run: 27/27 runtime checks passed (`Saved/ExpansionChecks/20260924T084431773010Z`).

## Aim indicator decorations (ability-vfx)

The armed preview keeps its exact boundary sections (fill + rim) and adds an animated section 2:
lines and skillshots get an arrowhead at the far end and chevrons travelling from the caster, cones
get outward chevrons, circles/squares/custom shapes get the designated-spot marker. All decorations
stay inside the true footprint. Server validation is unchanged. See [AbilityVFXAudit.md](AbilityVFXAudit.md).

## Casting while moving, smart cast and ground aim (September 25 playtest pass)

The playtest report: "strafing and camera get wonky while casting; I lose my target; queue a spell,
start moving or right-click and the AoE circle is gone".

**Root causes found**
- **Right mouse cancelled ground aim** (`CireTargeting::Tick`). A WoW player steers with the right
  mouse button, so pressing it to turn toward the target cancelled the reticle. The camera also
  refused to steer while a reticle was armed, because steering needed a "press eligible" frame. The
  reticle now survives movement, right-button pressing, dragging and releasing. Only **Escape**
  always cancels. A **clean right click** (press and release, under 8 mouse counts and 0.35 s)
  cancels when *Right click cancels ground aim* is on. Confirming is a **clean left click**. A left
  drag orbits the camera without confirming. The confirm is sent while you keep moving.
- **Clicking empty ground cleared the target.** A quick left tap to nudge the camera while
  strafing counted as a click and sent "clear target". Ground clicks now keep the target; Escape
  clears it once no menu is open (WoW).
- **Body swung sideways after a strafe.** When movement keys were released, the hero fell back to
  orient-to-movement, so the braking velocity of a strafe turned the body (and the next W direction)
  90 degrees. Player-controlled heroes no longer auto-orient to velocity; bots still do.
- **Enemy spells and summons with no target simply failed** ("Select an Enemy...").
- **Cast-time spells ignored movement entirely.**

**Rules now**
- Instant and ground-aim spells cast while moving and strafing.
- Cast-time spells follow `castWhileMoving` in `Content/Data/Abilities.json` (generated by
  `Tools/BuildAbilityDB.py`, list `CAST_WHILE_MOVING`). The default is WoW: cast-time spells need
  you to stand still. Starting one while moving answers **"Can't cast while moving."** Moving (input
  or a jump) during the cast cancels it with **"Moved: cast cancelled."** Knockbacks and residual
  braking do not count. Bots are not gated.
- *Stop moving to cast* (default on): pressing such a spell while running holds your drive and
  strafe keys. Turning still works. The cast is sent once the hero has stopped. Pressing a movement
  key again releases the hold (and so cancels the cast). The hold ends by itself after the cast.
- *Smart cast* (default on): an enemy spell with no valid hostile target selects the hostile under
  the cursor, else the nearest one inside the camera view, else the nearest one around you (within
  the spell range), then casts. A hostile target that is merely out of range reports "Out of
  range." and is kept. Summons that need an enemy (Spectral Pack) do the same within tab range,
  then arm placement. Ally spells with no target fall back to yourself.
- *Mouseover casting* (default off): the valid unit under the cursor receives the spell (enemy
  spells on enemies, ally spells on allies) without changing your selection. The server uses it
  for that one cast.
- Ground placement falls back to a point on the ground in front of you when the cursor has no
  valid ground (quick cast, press-again and confirm).
- *Press ability again to cast at reticle* (default on).
- The camera no longer auto-follows while a reticle is armed, so the aim point does not drift.
  During right-button mouselook the cursor is hidden and locked, so the reticle keeps projecting
  from the last cursor position while the camera turns.

**Tests**
- `Tools/RunPlaySession.py` (native probe `CirePlaySession.cpp`) runs a standalone offscreen match
  and drives real inputs through PlayerInput. It covers smart cast; target retention through
  strafe, RMB steer, an LMB tap and an LMB drag; Eric's exact sequence (key, W, RMB down, pan,
  RMB up, LMB click, the cast lands at the reticle, and the hero never stops); LMB drag does not
  confirm; a clean RMB click cancels; press-again casts; stop-to-cast; "Can't cast while moving";
  strafing cancels a cast; and a summon with no target.
- Native: movement suite (cast rule, gate, cancel), targeting runtime (smart-cast selection,
  front-point fallback).
- Pet/summon **Attack** with no hostile selected commands the nearest visible hostile within tab
  range (server side), else "No enemy nearby to attack.". Move/Follow/Hold are unchanged.

Latest evidence (September 25):
- Play session: 24/24 checks passed, three runs in a row (`Saved/PlaySession/20260925T140715437054Z`).
- Native expansion: passed (`Saved/ExpansionChecks/20260925T140422427104Z`); movement 54 checks,
  targeting runtime 32.
- Two-client network: passed (`20260925T140528244702Z`).
- Interface smoke: passed.
