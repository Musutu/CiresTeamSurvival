# Buff, aura and empowered-attack visuals

Every buff, debuff, aura and stance in the game now has a **signature visual**
that you can recognise at gameplay distance without hovering a tooltip, and
buffs that empower attacks change the attacks themselves (for example a Blood
Rage unit swings bloody crimson arcs and splashes blood on a confirmed hit).

Everything here is local, cosmetic presentation driven by replicated state.
It never changes damage, collision, targeting, AI or relevancy.

## How it works

| Piece | File | Role |
|---|---|---|
| Named effect records | `CireBuffs.h/.cpp` | `UCireBuffState` is a replicated component on every hero and monster. The server records *which* effect is on a unit (`CireBuffs::Apply(unit, id, seconds, source, stacks)`), so clients can draw the right signature. Gameplay still uses the existing fields (`ShieldUntil`, `TauntUntil`, `SlowUntil`, NPC status flags). |
| Visual data | `Content/Data/BuffVisuals.json` | One row per id: name, kind, palette, priority, layers (shape + attach point + style), stack intensity, burst/fade timing, light, attack modifier, sound cue ids. `cire.Auras.Reload` reloads it. |
| Renderer | `CireAuraVisuals.h/.cpp` | `UCireAuraComponent` (one per unit, attached by `CireStatusVisual::Attach`) turns records and derived state into layered procedural geometry. `UCireAuraSubsystem` enforces the caps, LOD, realm privacy and the other-units intensity option, and drives empowered attacks. |
| Geometry | `CireAuraShapes.h/.cpp` | 19 procedural layer shapes plus swipe / on-hit / projectile-trail / muzzle strike geometry. |
| Materials | `Content/Art/FX/Auras/M_AuraCore`, `M_AuraSoft` | Additive, unlit, depth-tested. Core: vertex colour x `Boost` (3.0) with a slow rising world-space shimmer. Soft: radial-falloff glows. Rebuild with `Tools/BuildAuraMaterials.py` (see header). Falls back to the spell-polish materials if missing. |
| Tests | `CireAuraTests.cpp` | `CireAuraVisuals::RunSmoke`, run by the native expansion probe. |
| Gallery | `CireAuraGallery.cpp`, `Tools/RunAuraGallery.py` | 11 offscreen 1920x1080 pages on real champion and monster bodies. |

### Where records come from (server)

| Id | Written by |
|---|---|
| `iron_guard`, `war_cry`, `sanctuary`, `bastion_of_dawn`, `shield_slam`, `frost_bind` (legacy path) | `ACireHero::Cast` (`// aura-vfx` lines) |
| `challenge_of_iron`, `mass_aegis`, `wellspring` | `CireRoleSkills::Cast` |
| `frost_bind` (skillshot) | `ACireSkillshot` Frost Bind hit |
| `taunted` (on a monster, source = taunting champion) | `CireThreat::Taunt` |
| `npc_tank_provoke_debuff` (on a champion, source = Shieldbearer) | `CireNPCCombat` provoke |

Records are dropped when they expire, when the unit dies, on revive, and on any
match phase change (`UCireBuffState::Prune`, server tick 0.2 s). A unit keeps at
most 12 records; a re-cast refreshes the end time without replaying the burst.

### Derived without records (client)

`guarded` (any other `ShieldUntil`), `taunting` (any other `TauntUntil`),
`slowed` (any other `SlowUntil`), `poisoned` (stacks = `PoisonAreaCount`),
NPC flags: Enraged -> the archetype's enrage ability id (`boss_leader_frenzy`,
`boss_siege_fury`, else `enraged`), Rallied -> `rallied`, Shield Wall ->
`npc_tank_wall`, Guarded -> `npc_tank_guard`, Provoking -> `npc_tank_provoke`,
and learned passives `battle_rhythm`, `soul_conduit`.

Named records stay honest with the gameplay state they decorate: a guard record
disappears when `ShieldUntil` is cleansed/reset, a slow record when the slow is
cleansed (Purify, Last Stand, Mass Aegis, Renewal), and so on.

## Signature table

Layers are listed in draw priority. "Rim" = thin allegiance ring at the feet
(teal friendly / red hostile, relative to the local player).

| Id | Name | Kind | Signature (silhouette + motion) | Palette | Empowered attack |
|---|---|---|---|---|---|
| `iron_guard` | Iron Guard | buff | 5 riveted iron plates orbiting the waist; rune-ticked ground ring | steel blue / white | - |
| `war_cry` | War Cry | stance | golden crown overhead; expanding shock-ripple rings | orange / gold | gold swipe, spark burst |
| `challenge_of_iron` | Challenge of Iron | stance | iron spiked crown overhead; rotating chain-link belt; dashed crimson ring | iron / crimson | steel swipe, sparks |
| `sanctuary` | Sanctuary | buff | golden faceted dome (fresnel lattice); rising glints | gold | - |
| `bastion_of_dawn` | Bastion of Dawn | aura | sunburst halo behind the head; large rune ring; rising glints; light | dawn gold | holy golden swipe, glint stars |
| `mass_aegis` | Mass Aegis | buff | 3 large kite shields orbiting the chest inside a faint hex ward | pale blue / white | - |
| `wellspring` | Wellspring | buff | 2 water spirals; ground ripples; rising bubbles | teal | - |
| `frost_bind` | Frost Bind | debuff | ice shards burst from the ground around the feet; falling snow | ice blue | - |
| `shield_slam` | Shield Slam daze | debuff | orbiting stars over the head | yellow | - |
| `taunted` | Taunted (monster) | debuff | glowing eye overhead; burst tether to the taunter | orange | - |
| `npc_tank_provoke_debuff` | Provoked (champion) | debuff | down-chevrons overhead; persistent chain tether to the Shieldbearer | steel blue | - |
| `guarded` | Guarded (generic) | buff | blue faceted dome | blue | - |
| `taunting` | Commanding presence (generic) | stance | "!" overhead; dashed ring | amber | - |
| `slowed` | Slowed (generic) | debuff | sinking chevrons; slow dashed ring | muted violet | - |
| `poisoned` | Poisoned | debuff | skull overhead; green puddle; rising bubbles; drips. Stacks 1..4 scale density and brightness | toxic green | - |
| `boss_leader_frenzy` | Blood Frenzy (Gravemaw) | stance | 3 crimson rage ribbons spiralling up the body; blood pool with droplets; dripping blood; blood-drop mark overhead; red light | crimson | bloody crimson swipe, blood splash on hit |
| `boss_siege_fury` | Siege Fury (Siegebreaker) | stance | flame tongues licking up the body; molten ground cracks; molten weapon; embers; light | molten orange | ember swipe, spark burst |
| `enraged` | Enraged (generic) | stance | red spiked crown; ember storm; glowing hands | red | ember swipe, sparks |
| `rallied` | Rallied (Rallying Roar / Bellow) | buff | up-chevron mark; rising up-chevrons; gold ring | war gold | gold swipe, sparks |
| `npc_tank_wall` | Shield Wall | stance | translucent tower shield raised in front of the unit; rune ring | blue | - |
| `npc_tank_guard` | Guardian's Oath | buff | shield mark overhead; rune ring | blue | - |
| `npc_tank_provoke` | Challenging Roar | aura | roar mark (sound arcs) overhead; shock ripples | blue | - |
| `battle_rhythm` | Battle Rhythm (passive) | passive | faint wrist glows only | amber | rhythm swipe with beat bars, sparks |
| `soul_conduit` | Soul Conduit (passive) | passive | a few faint rising "+" motes | teal | - |
| `blood_rage` | Blood Rage | buff | rage ribbons; hands glowing with rage swirls; blood pool; red embers; light | crimson | bloody swipe, blood splash |
| `frost_weapon` | Frost Weapon | buff | glowing frosted weapon line; drifting snow | ice | frost trail, ice shatter |
| `blessing` | Blessing | buff | small sun mark; golden hands; glints | gold | holy glints |
| `regeneration` | Regeneration (heal over time) | buff | rising leaves and "+" motes; soft ring | leaf green | - |
| `stunned` | Stunned | debuff | large star orbit; fast dashed ring | yellow | - |

`blood_rage`, `frost_weapon`, `blessing`, `regeneration` and `stunned` are
data-ready for item actives and future skills; no current ability produces them.
`stone_skin` and `deep_reserves` are always-on stat passives with no state
change, so they intentionally have no aura (it would be permanent noise).

### Colour-blind clarity

No two entries share the same set of layer silhouettes (checked by the native
test), so identification never depends on hue alone: guards are shells/plates/
shields, controls are overhead marks (stars, eye, chevrons, "!"), damage-over-time
is bubbles/drips, rage is ribbons + pool, fire is tongues + cracks, frost is
shards + snow. Overhead marks use distinct shapes, and the friend/foe rim differs
in both hue and placement (it is the thin ring closest to the feet).

## Lifecycle

* **Start burst**: layers pop in at up to 1.35x scale and extra brightness for
  `burstSeconds`; `burstOnly` layers (for example the taunt tether) show only then.
* **Loop**: continuous procedural motion (orbiting, spiralling, rising, dripping).
* **Expiring**: in the last `expiringSeconds` the effect blinks.
* **Fade-out**: when the record/state ends (expiry, cleanse, death, phase change)
  the effect shrinks and fades over `fadeSeconds`, then its geometry is cleared.
* **Stacks**: intensity = `base + perStack x (stacks - 1)` (capped by `maxStacks`)
  scales brightness and particle density (poison stacks per area).

## Empowered attacks

When the highest-priority active effect has an `attack` block:

* **Champion basic attacks**: the replicated `AttackSerial` is watched; at the
  real release moment (0.25 of the 0.65 s reference clip) a melee champion gets a
  swipe arc toward the target and a ranged champion a muzzle burst plus a trail
  following its real projectile (`ACireTargetProjectile`).
* **Monster melee**: the existing `npc_melee` impact cue is matched to the
  attacker at its origin (spell presentation hook).
* **On-hit** bursts (splash/shatter/glint/sparks/ripple) spawn only when a
  *confirmed damage* combat event from that attacker arrives, so a miss or dodge
  never shows an empowered impact.

## Clarity and performance budgets (`limits` in the JSON)

| Budget | Default |
|---|---|
| Units with auras rendered | 32 (local player first, then your target, then by priority and distance) |
| Layers per unit | 5 |
| Aura point lights | 6 (full-detail units only, no shadows) |
| Live strike effects (swipes, hits, trails) | 24 |
| Vertices per unit | 9000 core + 1800 soft |
| Full detail | < 28 m from the camera |
| Reduced detail (2 leading layers, half particles, refresh every other frame) | < 55 m |
| Overhead marks only | < 90 m |
| Culled | beyond 90 m |

All meshes are collision-free, never affect navigation and cast no shadows.

**Option**: F9 -> Display / Graphics -> *Other units' aura effects* (0..1,
default 1). It scales everyone else's auras and empowered strikes; below 0.35
other units keep only their overhead marks. Your own effects stay full. Stored
as `OtherEffectsIntensity` in the UI settings profile (older profiles default to 1).

Your own unit never draws its overhead mark (the buff bar covers it and the mark
would sit in the camera's sightline); its body/ground layers always render.

## Privacy and replication

* Records replicate with their unit, so the realm relevancy rule already keeps
  opponents' records off your machine outside the arena.
* The renderer additionally checks `CireRealm::CanObserve` and actor visibility
  every frame, the same rule used for body visibility; hidden units never draw.
* Strikes are local actors that die on any phase change.
* Dedicated servers create no renderer, subsystem work or strike actors.

## Sound hooks

Each row's `sound` block carries cue ids for the audio pass: `start`, `loop`,
`end`, `hit` (for example `stance.blood_frenzy.start`, `buff.blood_rage.hit`).
`CireAuraVisuals::PlaySoundCue` is called on start, end and confirmed empowered
hits. Until `CireAudio::PlayCue/PlayAttached` lands it only logs (Verbose,
`LogCireAura`). Loop cues are declared but not yet started; wire them when the
audio API exposes attached looping sounds.

## Adding an effect (items, new skills)

1. Server: `CireBuffs::Apply(unit, TEXT("my_item_active"), seconds, sourceActor, stacks)`
   (and `CireBuffs::Remove` if it ends early).
2. Add a `buffs.my_item_active` row to `Content/Data/BuffVisuals.json`, choosing
   a layer combination no other row uses.
3. If a skill produces it, add the id to `CireBuffs::KnownIds()` so the native
   test requires its row.

## Verification

```powershell
# Native (includes CIRE_AURA_SMOKE_PASS), replay, and two-client network checks
F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/RunExpansionChecks.py
# Offscreen gallery (11 pages, Saved/AuraGallery/<stamp>)
F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/RunAuraGallery.py
```

The native smoke covers: every producible id has a row, distinct signatures,
parser rejection keeps loaded data, start/loop/cleanse-fade/expire-removal,
every signature renders within budget, derived states and named-over-generic
replacement, death cleanup (no geometry/light left, server records dropped),
phase-change cleanup, per-unit record cap, realm privacy and hidden units,
unit/light/layer/strike caps, attack-modifier priority, end-to-end empowered
swing on an attack serial (and none without a buff), intensity option
persistence, and renderer unregistration on destroy. The network probe checks
that a record replicates to its own client and drives the aura there, that an
opponent's aura never renders outside the arena, that death and phase changes
clear records on clients, and that arena records reach both teams.

## Limits

* Procedural meshes and two materials, not authored Niagara systems; particles
  are CPU-built each frame for nearby units (budgeted above).
* Hand glows use `hand_l`/`hand_r`; bodies without those bones fall back to
  chest-side points. The weapon line uses the largest hand-held prop's bounds.
* Monster empowered swipes are driven by the melee impact cue, so they appear at
  impact time rather than at a wind-up; monster ranged attacks are not empowered.
* Summons do not receive records yet (they inherit the component but no skill
  writes to them).
* Loop sound cues are data only until the audio API is available.
