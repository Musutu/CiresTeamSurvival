# Monster bodies and champion action animation (creature-anim)

The twelve Tripo batch-03 monster bodies now draw every NPC archetype, animated natively (no Animation
Blueprint), and the Tripo champions swing, shoot, cast and shout with clips from the same Tripo motion
library. Presentation only, except one gameplay timing change: monster melee blows now land on the swing's
contact frame (see "Melee timing").

## Monsters

| Archetype | Bodies (picked per spawn) | Attack / specials | Props kept |
|---|---|---|---|
| hollow_infantry | HollowInfantry, HollowInfantryB | slash | dagger (hand_r) |
| ironbound_bruiser | IronboundBruiserV2 (shieldless; the original is superseded) | slash (chop is the same clip) | war axe |
| hollow_shieldbearer | HollowShieldbearerV2 (capeless; the original is superseded) | slash | shield + sword |
| blight_caster | BlightCaster, BlightCasterB | cast_a_spell for every cast bar | none: staff/sceptre are in the mesh |
| barbed_hunter | BarbedHunter (bow prop), BarbedHunterB (crossbow in the mesh) | attack_bow / attack_crossbow | bow on A only |
| hollow_siegebreaker | HollowSiegebreaker | slash; ground_slam for Siege Stomp and Rallying Bellow | none: maul in the mesh |
| gravemaw_pack_leader | GravemawPackLeader (re-skinned copy, below) | slash; war_cry for Rallying Roar and Blood Frenzy; chop for Brutal Charge | totem on the back |

Data: `Content/Data/NPCMeshes.tripo.json` (Tripo agent: meshes, meshScale, yaw -90, clips) plus
`Content/Data/MonsterArt.json` (mine: variants per archetype, clip timing windows, dropped prop bones, prop
adjustments, re-skinned mesh overrides, elite/boss/enraged rim colours, corpse timing).
`NPCArchetypes.json` is unchanged: its empty mesh paths are the mannequin fallback.

### Runtime (`UCireMonsterArt`, `UCireMonsterAnimInstance`)

* `UCireMonsterArt` is a default subobject of `ACireMonster`. On clients and listen servers
  `UCireNPCState::ApplyVisuals` asks it for the Tripo body first; if the mesh or its idle clip is missing
  (or `cire.Monsters.TripoBodies 0`) the mannequin body, tint and props come back unchanged.
* The server picks `BodySeed` at spawn; it replicates, so every client draws the same variant.
* Placement: mesh scale = meshScale, yaw from the data, pivot (soles) on the capsule bottom. The idle clip's toe
  joints are measured at load and the body is lifted when a clip presses them into the floor (BlightCaster
  ~6 cm). Archetype scale (Pack Leader 1.7x, Siegebreaker 1.35x), elite tier and enrage scale capsule and body
  together, so feet stay grounded at every size.
* `UCireMonsterAnimInstance` samples the sequences itself: idle, walk/run blended by speed, an action layer
  (attack, cast, hit) and a death layer. Root motion is never extracted.
* The Tripo walk/run clips are not in place: the pelvis travels forward (~140 raw units per walk cycle). Each
  clip's pelvis drift is fitted at load and removed, and the pelvis cycles over the idle stance. The fitted
  speed is also the clip's natural ground speed: the cycle rate is actual speed / natural speed (walk
  ~125 cm/s, run ~450 cm/s at 1x), and walk/run phases are aligned on the left-foot apex, so planted feet do
  not slide.
* Action layer blends per bone: spine and above take the full weight, legs only when standing still, so hit
  reactions and casts while moving keep the gait.
* Casts: the clip's start..release window is stretched over the replicated cast bar (CastStartedAt..CastEndsAt);
  an interrupted cast lets go of the pose in 0.2 s. A cast already under way when the body appears is picked up
  mid-bar. Hit reactions (upper body, 0.75 weight) play when health drops, at most once per 1.1 s.
* Held props use a grip frame from the bind-pose hand (handle across the palm, blade on the thumb side, face on
  the back of the hand) and compensate the imported root scale of 100, so they are real-world size.
* Elite / boss / enraged bodies get a dim fresnel rim (`M_SelectionEdge` MID). It only fills an empty overlay
  slot; the selection highlight stores and restores it.

### Melee timing (gameplay change, `cire.Monsters.SwingWindup`)

A basic melee swing is now committed and lands after a windup of 0.3x the attack period (0.22-0.5 s), on the
clip's contact frame, instead of on the first frame. Attack period and damage are unchanged. The blow still
lands if the victim stepped up to 120 cm (+ capsule radius) beyond reach; death, phase pause, stun or kick
(`CireNPCCombat::Interrupt`) cancels it. The monster finishes the swing before choosing another action.
`cire.Monsters.SwingWindup 0` restores instant hits. Wall breaches stay instant but play the swing.

### Death

`ACireMonster::TakeDamage` calls the reliable `MulticastDeath` before `Destroy()`. Each client spawns a local,
non-replicated `ACireMonsterCorpse` copying mesh, materials, pose transform and props; it plays the fall clip
(3 s), holds 2.6 s, sinks 70 cm over 1.4 s and destroys itself. Nothing on the server waits for it.

### Pack Leader greataxe

The import weighted the fused greataxe to the right foot (`ball_r` 0.77), so the haft stretched whenever the arm
swung. `Tools/BuildMonsterWeaponSkin.py` (GeometryScripting enabled on the command line only) copies the mesh to
`/Game/Art/Characters/MonsterAnim/Bodies/SK_GravemawPackLeader` with the axe island weighted 100% to `hand_r`,
keeping the original skeleton so every original clip plays. The analysis (`Saved/MonsterWeaponSkin.json`) found
the Siegebreaker maul, both caster staves and the Hunter B crossbow fused into the hand geometry and already
following it.

## Champions (`CireChampionActions`, `/Game/Art/Characters/ChampionAttacks02`)

`Tools/RetargetChampionAttacks.py` writes, for all 17 Tripo champion bodies (Warden, Ranger, Scholar and the
Batch01 bodies): `slash` (trimmed from the 6.58 s source to its single swing, 0.80-3.30 s), `cast_a_spell`,
`war_cry`, plus `attack_bow` and `attack_crossbow` on the archer body; 53 clips.

The IK-retargeter pattern of `RetargetTripo.py` was tried first (auto IK rigs, retargeter, batch retarget): with
the imported root scale of 100 on both source and target, every output lost its root scale and put the pelvis at
~9 of 53.6 units with the legs folded. Monster and champion rigs are the same 61-bone Tripo hierarchy, so the
build transfers the motion directly: each target bone takes the source bone's component-space rotation change
from its bind pose, keeps its own bone lengths (so proportions and root scale 100 are exact), the pelvis
translation is scaled by pelvis height, and the pelvis is lifted where the source stance pressed the toes below
the floor. Clips are resampled to 30 fps, root motion off, root lock on. Every clip is validated RAW and
COMPRESSED (root scale 100, pelvis height, head above pelvis above feet, finite, compact).

Runtime: the weapon class comes from `WeaponLoadouts.json` (melee, throw, none: slash; bow; crossbow; cast:
cast_a_spell). The basic attack's contact frame lands on the server release (0.25 of the 0.65 s authored
attack, scaled by attack speed); short windups skip the slow start of the raise (at most 2.4x) and the swing is
settled by 1.5x the attack duration. Casts are detected from the replicated cooldowns (a slot's cooldown starting
means the server accepted it): shouts (`shoutSkills`, "roar", "_cry") play war_cry, spells cast_a_spell, others
the weapon strike. The layer blends per bone, so the legs keep walking during swings and casts. Champion melee
weapons and shields use the same bind-pose grip frame as the monsters. Bodies without ChampionAttacks02 clips keep
the CombatPrototype01 clip.

## Tests and evidence

* Native (`CireMonsterArt::RunSmoke` and `CireChampionActions::RunSmoke`, run by `RunNPCChecks --only native` and
  `RunExpansionChecks --only native`): every archetype resolves its bodies and clips (10 bodies x 6 roles), feet on
  the capsule bottom and head height per body and scale, Pack Leader 1.7x, 260 evaluated poses stay finite and
  compact, walk/run stay in place over the capsule, deaths lie down, stride speeds sane, duplicate props dropped and
  kept props at real scale, aura/footstep bones and armour class, selection overlay round trip, swing scheduled then
  released once and cancelled by interrupt, death leaves one corpse that despawns, fallback to the mannequin and
  back; 53 champion clips with in-clip windows, no root motion, slash trimmed, every roster profile has a weapon
  class. `CIRE_MONSTER_ART_PASS checks=1258`, `CIRE_CHAMPION_ACTIONS_PASS checks=218`.
* Network (`RunNPCChecks --only network`): the client draws the Tripo Pack Leader with a replicated seed and plays
  its cleave windup.
* Renders: `python Tools/RunMonsterGallery.py [--only stage,...]` -> `Saved/MonsterGallery/<stamp>/`: two lineups,
  the elite pack, every archetype at windup/contact/follow-through + special, B variants at release, walk/run frames,
  deaths, six champions mid-attack/cast/shout, and a live wave fighting a champion in the town through the gameplay
  camera framing (march, fight, kill, corpses). Champion bindings: `Tools/RunBatchArtGallery.py --profiles ...`.
* Scripts: `Tools/InspectMonsterAnimation.py` (read-only clip analysis), `Tools/BuildMonsterWeaponSkin.py`,
  `Tools/RetargetChampionAttacks.py`.

## Limits

* The Tripo clips are generic library motion: the slash is a hunched overhead chop for every body (slash and chop
  are identical), there is no second melee variation, and the bow clip holds the bow canted.
* No foot IK: feet are grounded by pivot and toe lift, not planted per step; slopes and stairs will float or sink
  the soles by a few centimetres. Strafing plays the forward cycle.
* Prop grips are computed, not authored: weapons sit in the fist but fingers do not wrap them.
* Champion casts are recognised from cooldowns, so a cast whose cooldown is refunded or zero plays nothing.
* The corpse is local presentation: it ignores realm changes after death and does not ragdoll.
