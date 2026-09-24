# NPCs: roles, Pack Leader boss, threat and UI read API

Status: engineering prototype (branch `feat/npc-boss`, 24 September 2026). Behaviour, data, replication and
native tests are real and passing; bodies are still tinted mannequins with prototype weapon props until the
Tripo monster models are merged (see *Mesh slots*). No balance simulation was run for these changes.

## Roles

Every monster archetype has exactly one role. The role picks the movement policy in `CireNPCCombat::Tick`;
the abilities come from data.

| Role | Behaviour | Authored units |
|---|---|---|
| **Tank** | Armored (25% less damage), slow, high health. Opens with **Challenging Roar** (provoke: nearby champions deal 35% less damage to anything except the tank for 6s; bots switch target to it — human input is never forced). **Guardian's Oath** guards the most injured ally (40% of its damage is redirected to the tank). **Shield Wall** below 50% health (50% less damage for 6s). | Hollow Shieldbearer, Hollow Siegebreaker (lane boss) |
| **Bruiser** (melee DPS) | Closes to melee and hits hard; **charges** distant targets along a telegraphed line, slams/cleaves in telegraphed cones. | Hollow Infantry (Lunge), Ironbound Bruiser (Brutal Charge, Crushing Slam), Gravemaw Pack Leader (boss) |
| **Caster** | Holds ~5.6m. Backs away when a champion reaches 3.2m (0.8s, then 4s before it kites again). **Shadow Bolt** is an interruptible 1.4s cast bar; **Dark Mending** is an interruptible 2s heal on the most injured ally (<60%); **Blight Pool** marks the ground, then leaves poison. | Blight Caster |
| **Ranged** | Holds ~5.2m. **Disengage** leaps away from melee (10s cooldown); otherwise kites for 1s (3s lockout) and shoots **Barbed Shot** (0.6s aim, not interruptible); **Rain of Barbs** telegraphed circle. | Barbed Hunter |

Rules shared by all roles:

- Telegraphed abilities (`cone`, `targetCircle`, `selfCircle`, `charge`) spawn an `ACireAreaEffect` warning whose
  warning time equals the cast time; damage lands when the warning completes. Hard cancels (phase change, leash,
  death, escort conversion) destroy the warning.
- `interruptible` casts can be kicked by champions (`shield_slam` now calls `CireNPCCombat::InterruptCast`).
  Non-interruptible casts (all boss abilities, hunter aim) ignore kicks. Monsters without archetype data keep
  the old always-interruptible behaviour.
- Abilities are tried in authored order after a per-ability cooldown and a short ability global cooldown
  (cast time + 1.5s). `enrage` ignores the global cooldown.
- Challenge-pack units are **Elite** (tier scales health; damage is `challengeMonsterDamage x eliteDamageMultiplier`).

## Pack Leader boss

Every challenge pack (tiers 1–3, `challengePacks.leaderFromTier`) spawns its members plus **Gravemaw, Pack
Leader**: 1.7x scale, blood-red body, boss classification (UI boss frame), 5x challenge health, 10% armor.

| Ability | Telegraph | Effect |
|---|---|---|
| Rallying Roar | 1.5s cast, gold 12m ring | Pack members (and itself) gain +25% damage, +25% attack speed and +10% movement for 10s. 18s cooldown, first use after 3s. |
| Sundering Cleave | 1.2s 120° cone, 4.4m | 2.6x damage to everyone in the cone. 9s cooldown. |
| Brutal Charge | 1.0s line to the **farthest** champion within 12m | 1.8x damage along the line, then dashes through it. 14s cooldown, first after 6s. |
| Blood Frenzy | 1.0s roar at 30% health (once) | +50% damage, faster attacks (x0.75 period) and +25% movement until killed; body grows 8%. |

Rewards are unchanged: the existing whole-pack rule (`ACireGameMode::MonsterKilled` / `Cires::RollChallengeReward`)
pays once the last pack unit dies, so the leader must die too. Each kill also pays the normal per-kill team share.
Pack leaders never leak (packs leash). The lane boss (**Hollow Siegebreaker**, final wave of each cycle) is also
data-driven: tank role, boss classification, Siege Stomp / Sundering Cleave / Rallying Bellow / Siege Fury, and
it still costs **10 lives** on reaching town (`leakCost`).

## Threat (WoW rules)

`CireThreat` (tunable in `NPCArchetypes.json` → `threat`):

- Damage threat = effective damage x hero role multiplier (`tankDamageThreatMultiplier` 5, others 1, from CombatTuning).
- Effective healing threat = heal x `healingThreatMultiplier` (0.4), **split** across engaged monsters. Overheal gives none.
- **Pull rule:** a monster keeps its current target until someone exceeds **110%** of that target's threat inside
  melee range (`meleeRangeCm` 300) or **130%** outside it.
- **Taunt:** raises the taunter to the top threat (+1) and forces the target for the duration (max 10s); afterwards the
  normal pull rule applies, so the taunter keeps aggro unless outpaced by 110/130%.
- **Decay:** threat that has not grown for `decayDelaySeconds` (10s) decays by `decayPerSecond` (5%/s). The current
  target never decays. Set `decayPerSecond` to 0 for strict WoW behaviour.
- **Transfer/Scale:** `CireThreat::Transfer(M, From, To, Fraction)` (misdirect) and `CireThreat::Scale(M, Hero, x)` (fade).
- Death, despawn, range (22m), leash, phase change and monster death remove threat and publish an event.

## UI read API (server and clients)

Everything is on `ACireMonster` and its replicated `UCireNPCState` component (`Monster->NPCState`, a default
subobject of every monster). Include `CireNPCState.h`.

```cpp
ECireNPCRole  Role  = Monster->GetNPCRole();           // Bruiser | Tank | Caster | Ranged  (role icon)
ECireNPCClass Class = Monster->GetNPCClassification(); // Normal | Elite | Boss             (frame)
FString       Name  = Monster->GetNPCDisplayName();    // "Gravemaw, Pack Leader" (MonsterName keeps prefixes)
bool          Leaks10 = Monster->IsLaneBoss();          // only lane bosses cost 10 lives

UCireNPCState* S = Monster->NPCState;
TArray<FCireNPCAbilityInfo> Abilities = S->Abilities(); // Name, Description, TypeLabel, Kind, Cooldown, CastTime, bInterruptible, bBasic
FCireNPCCastInfo Cast = S->CastInfo();                  // bCasting, Name, Progress 0..1, Remaining s, bInterruptible (cast bar)
S->HasStatus(CireNPCStatus::Enraged | Rallied | ShieldWall | Guarded | Provoking | Charging);
S->ThreatTable;                                          // replicated rows {Hero, Threat}, sorted, top 10, ~4Hz
S->ThreatPercent(MyHero);                                // your threat vs the current target (target = 100)
S->PullPercent(MyHero);                                  // 100 = you pull aggro (applies 110%/130%)
Monster->Victim;                                         // replicated current target ("focusing")

// Aggro events: fired on the server when it happens and on clients when the state replicates.
UCireNPCState::OnAggroChanged().AddLambda([](const FCireAggroEvent& E) {
    // E.Monster, E.NewTarget, E.OldTarget, E.Reason (Acquired, Pulled, Taunted, TauntExpired, TargetLost, Reset)
    FString Line  = UCireNPCState::DescribeAggro(E);          // "Ember 2 gained aggro on Ironbound Bruiser"
    FString Focus = UCireNPCState::DescribeFocus(E.Monster.Get()); // "Ironbound Bruiser is focusing Ember 2"
});
```

Ability type labels: `Attack`, `Cast`, `Cast (interruptible)`, `Telegraph`, `Buff`, `Taunt`, `Enrage`, `Movement`.
The legacy `CombatArchetype` field is still set (0 tank/infantry, 1 bruiser, 2 caster, 3 ranged) so the current
HUD range readout keeps working.

## Data format — `Content/Data/NPCArchetypes.json`

```jsonc
{
  "schemaVersion": 1,
  "legacyKinds": ["hollow_infantry","ironbound_bruiser","blight_caster","barbed_hunter"], // Configure(Kind 0..3)
  "waveComposition": ["hollow_infantry", "...", "hollow_shieldbearer"], // cycled by wave spawn slot
  "waveBoss": "hollow_siegebreaker",
  "challengePacks": { "members": ["hollow_shieldbearer","blight_caster","barbed_hunter"], "leader": "gravemaw_pack_leader", "leaderFromTier": 1 },
  "threat": { "meleePullRatio": 1.1, "rangedPullRatio": 1.3, "meleeRangeCm": 300, "decayDelaySeconds": 10, "decayPerSecond": 0.05, "publishInterval": 0.25, "tauntMaxSeconds": 10 },
  "archetypes": {
    "gravemaw_pack_leader": {
      "displayName": "Gravemaw, Pack Leader", "role": "bruiser", "classification": "boss",
      "tuningKind": "bruiser",          // optional: take health factor + damage from CombatTuning globals instead
      "healthMultiplier": 5.0, "damage": 36, "eliteDamageMultiplier": 1.0, "moveSpeed": 215, "armor": 0.1,
      "attackRange": 210, "attackInterval": 2.0, "preferredRange": 0, "kiteRange": 0, "scale": 1.7, "leakCost": 1,
      "mesh": { "slot": "boss_pack_leader", "path": "", "tint": [0.7, 0.1, 0.06], "scale": 1, "yaw": -90, "material": "" },
      "props": [ { "asset": "/Game/...SM_WarAxe.SM_WarAxe", "bone": "hand_r", "x": 0, "y": 0, "z": 0, "pitch": 0, "yaw": 0, "roll": 0, "scale": 1.25 } ],
      "abilities": [
        { "id": "npc_melee", "name": "Gravemaw Cleaver", "description": "...", "type": "melee", "basic": true, "range": 210 },
        { "id": "boss_leader_cleave", "name": "Sundering Cleave", "description": "...", "type": "cone",
          "cooldown": 9, "castTime": 1.2, "interruptible": false, "range": 400, "radius": 440, "angle": 120,
          "damageMultiplier": 2.6, "color": [0.9, 0.12, 0.05, 0.4] }
      ]
    }
  }
}
```

Ability `type`: `melee`, `projectile` (needs `skillshot` = CombatTuning skillshot id), `cone`, `targetCircle`
(`damagePerSecond` + `duration` make a lingering pool), `selfCircle`, `charge` (`targeting`: `victim`|`farthest`,
`length`, `width`), `guard`, `provoke`, `rally`, `enrage` (`healthThreshold`), `healAlly` (`healthThreshold`,
`magnitude` = heal fraction), `shieldWall`, `disengage` (`length` = leap distance). Common fields: `cooldown`,
`castTime`, `interruptible`, `range`, `minRange`, `radius`, `angle`, `damageMultiplier`, `duration`, `magnitude`,
`initialCooldown`, `color`. Every archetype needs exactly one `basic` ability (melee for tank/bruiser, projectile
for caster/ranged). The loader is strict (bounds, safe `/Game/` or `/Engine/` paths, known ids, bosses need
boss classification); a bad document logs `CIRE_NPC_DATA_ERROR` and keeps the previous data. Reload in a
development build with the console command `cire.NPCs.Reload` (affects newly configured monsters).

### Mesh slots (for the Tripo monster models)

`mesh.path` is a soft object path. When the package exists it is used — a `SkeletalMesh` replaces the mannequin
(keep the unarmed anim BP unless the model brings its own), a `StaticMesh` is attached to the capsule and hides
the mannequin (`mesh.material` then applies). Missing or empty paths fall back to the mannequin tinted with
`mesh.tint` ("Paint Tint"/"Global BaseColor"), so the build never depends on the art branch. After the
`Content/Tripo/Monsters/` merge, set the paths for slots `npc_infantry`, `npc_bruiser`, `npc_shieldbearer`,
`npc_caster`, `npc_hunter`, `boss_siegebreaker`, `boss_pack_leader`, and tune `mesh.scale`/`mesh.yaw`.

## Routes

Monsters walk via `CireNPCCombat::RouteDestination(M)` and leak via `CireNPCCombat::ReachedGoal(M)`. Today they
wrap `CireLanePath::NextWaypoint` and `ACireTownGoal::ContainsLocation`; when the town layout lands, only these
two functions need to point at its route API.

## Wave and pack schedule

- Wave `N` of a cycle: `4 + min(round, 8)` units per lane from `waveComposition` (cycled: infantry, bruiser,
  caster, hunter, shieldbearer). The shieldbearer replaces what used to be a fifth infantry.
- Final wave of each cycle: + 1 Hollow Siegebreaker per lane (lane boss, 10 lives).
- Escort waves unchanged (`CireLanePath::ConfigureEscort`).
- Challenge packs: members (tank, caster, ranged) + Pack Leader per tier 1–3 per team, refreshed each cycle.

## Verification

- `python Tools/RunNPCChecks.py` → `Saved/NPCChecks/<stamp>/report.json`:
  - native: `-CireCombatExpansionProbe` incl. `CIRE_NPC_SMOKE` (12), `CIRE_NPC_DATA_SMOKE` (38),
    `CIRE_THREAT_RULES` (22), `CIRE_NPC_ROLES` (55) and all pre-existing suites;
  - preview: `-CireNPCPackPreview` two 1920x1080 captures of a tier-2 pack with its leader and a cleave telegraph;
  - network: dedicated server + remote client verify replicated role, boss classification, ability list, cast bar,
    threat table, victim and the client-side aggro event.
- `Tools/RunExpansionChecks.py --only network`, `-CireSmoke` (full cycle incl. two boss leaks → 75/75 lives),
  `-CireCombatFeaturesProbe` and `-CireTelemetryProbe` still pass.

## Known limits

- Bodies are tinted mannequins with prototype weapon props (grips are approximate); no role-specific animations,
  so casts/charges read through telegraphs, cast bars and movement only.
- Charge is a fast walk along the telegraphed line (damage lands with the line warning), not a root-motion lunge.
- Kiting is simple back-pedalling without navmesh/obstacle awareness; units can back into walls.
- Rally/enrage/guard/provoke have no dedicated VFX yet — they are exposed as status flags for the UI.
- The fifth wave unit is now a Shieldbearer (2.2x health) and every challenge pack gains a leader; these make
  PvE harder and have **not** been balance-simulated (simulations are paused by request).
- Threat decay is a mild, documented deviation from strict WoW (disable with `decayPerSecond: 0`).
