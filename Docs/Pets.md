# Pets and companions

A **companion** is a pet that stays with its owner for the whole match. It follows the owner, fights by stance and by order, has its own health bar, and can hold aggro. When it dies, the owner can revive it or wait for it to come back.

The first companion is **Ashfang**, the Huntress's sabercat. The Huntress fights on foot; she has no mount.

## Sources of truth

| What | Where |
|---|---|
| Pet types, stats, scaling, abilities, body, rules | `Content/Data/Pets.json` |
| Pet ability numbers (effect, cooldown, range, radius, duration, slow) | `Tools/BuildAbilityDB.py` (`NEW_CHAMPION_SKILLS`, category `pet`), producing `Content/Data/Abilities.json` |
| Runtime | `Source/CiresTeamSurvival/CirePets.h/.cpp` (`ACirePet`, `CirePets::`) |
| HUD pet frame | `Source/CiresTeamSurvival/CireHUDPets.cpp` (the `Pet` panel) |
| Procedural sabercat body | `Source/CiresTeamSurvival/CireCreatureArt.cpp` (`FCireQuadRig`, `UCireQuadrupedAnimInstance`) |
| Tests | `Source/CiresTeamSurvival/CirePetsTests.cpp` (part of `RunExpansionChecks --only native`), plus the pet stages of `CireExpansionNetProbe` (`--only network`) |
| Captures | `Tools/RunNewChampionsGallery.py --only huntress,combat_huntress,pet,hud_pet` |

## How it works

`ACirePet` is a subclass of `ACireSummon`. Every system that already treats summons as "not a player" treats a pet the same way:

- it is not in the roster;
- it has no class trait;
- it never counts as a life or as a team elimination;
- its damage is credited to the owner in meters and loot;
- it is left out of the party frames.

Unlike a timed summon, a pet never expires.

- **Summoning.** The owner's tick (`CirePets::TickOwner`) summons the pet when the owner is drafted and alive, the match is not finished, and the return timer has run out. If a phase change clears all summons, the pet comes straight back beside its owner.
- **Owner death.** The pet leaves when its owner dies, is removed, changes team or loses the pet. It comes back when the owner is revived.
- **Server-authoritative.** All pet decisions run on the server. Clients receive the pet actor, which carries:
  - `PetId`, `Stance`, `Order`, `StayPoint`
  - `AbilityReadyAt` (per-ability cooldowns)
  - `DiedAt`, `AbilitySerial`
  - the hero fields: health, level, attack serial.

  Clients also receive the owner's timers, so the HUD can show them while the pet is away: `ACireHero::PetResummonAt`, `PetReviveReadyAt`, `PetStance` and `PetGrant`.
- **Pathing.** Pets use navmesh pathing through `CireNav::Steer`, on the Hero agent (radius 44 cm). A pet that is more than 36 m from its owner, or on the other side of a realm boundary, teleports back beside the owner. This covers recall, arena teleports and being stuck.
- **Arena and realm privacy.** A pet has its owner's `TeamId`, so hostility, targeting and visibility all follow the hero rules:
  - During PvE, the other team never sees your pet.
  - In the arena, pets fight the enemy team and can be hit by it.
  - A pet never attacks anything its owner cannot observe.

### Stances

The owner's chosen stance is stored on the owner, so a resummoned pet keeps it.

| Stance | Behaviour |
|---|---|
| **Aggressive** | Everything Defensive does, plus it attacks any enemy within 9 m of itself. It never opens on a neutral challenge pack. |
| **Defensive** (default) | Assists the owner's target, and attacks enemies that are attacking the owner or itself. |
| **Passive** | Never attacks on its own. It still obeys direct attack orders and owner skills. |

### Commands

Commands go through `ACireController::ServerPetCommand`. The server uses its own copy of the owner's target; it never trusts an actor sent by the client.

| Command | Default key | Effect |
|---|---|---|
| Attack my target | `Y` | Attacks the selected hostile target until it dies or leaves the 24 m leash. |
| Follow | `U` | Returns to the owner and resumes its stance. |
| Stay | `I` | Holds its current spot. It still fights anything that reaches it, unless it is Passive. |
| Special | `O` | Uses the pet's special ability, on the pet's own cooldown. For Ashfang this is Dread Roar. |
| Revive / call | `P` | See [Death, revive and return](#death-revive-and-return). |
| Stance: aggressive / defensive / passive | `Shift+Y` / `Shift+U` / `Shift+I` | Sets the stance. |

All of these keys can be rebound on the Keybindings page (Combat group). WoW puts its pet bar on `Ctrl`, but `Ctrl` is the dodge roll here. The pet frame has a clickable button for every command.

### Death, revive and return

When a pet dies it becomes a corpse:

- its collision is turned off and its threat is dropped;
- the wolf fallback body plays its death clip, and the procedural sabercat rolls onto its side.

The owner then has two choices:

- **Revive.** The owner must be within 15 m of the corpse. The pet stands up at 50% health. Revive has a 30 s cooldown.
- **Wait.** The pet returns at full health beside the owner 20 s after it fell.

The revive/call key also works in two other cases:

- If the pet is **away**, the key calls it once the return timer has run out.
- If the pet is **alive but has strayed**, the key calls it back to the owner's side.

### Threat

Pets use the WoW hunter-pet threat model, in `CireThreat`:

- **Its own threat.** A pet's damage generates threat equal to damage × the pet's `threatMultiplier`. For Ashfang this is 2.0, which sits between a DPS (1×) and a tank (5×).
- **Threat share.** While the pet is alive, not Passive, and already engaged on a monster, 30% of the owner's damage threat goes to the pet instead of the owner. This is the `ownerThreatShare` rule. As a result, a Huntress who lets her cat open the fight can keep attacking without pulling aggro.
- **Maul** adds 50% bonus threat.
- **Dread Roar** taunts the monsters it hits onto the pet for 2 s.
- **Death** removes all of the pet's threat.

### Scaling

The pet's stats are recomputed every second from its owner:

| Stat | Formula |
|---|---|
| Max health | `health + healthPerLevel × (level − 1) + ownerHealthShare × owner max health` |
| Damage | `(damage + damagePerLevel × (level − 1) + ownerPrimaryScale × owner primary attribute) × team power` |

When max health changes, the pet keeps the same health fraction.

Ability strength is `(Ability DB effect + scaling × owner primary) × team power`:

- A command from the owner (a skill cast or the special key) uses full strength.
- When the pet uses an ability on its own (autocast), it uses 60% strength.

### Owner power (Eric's rules)

Pets, summons and constructs inherit their owner's power. For pets this means:

- **Directly targetable.** A pet is an `ACireHero` on its owner's team. Monsters put it in their threat table and choose it as their victim, and in the arena enemy champions can select and hit it.
- **Primary-stat damage.** Damage and ability strength scale with the owner's primary attribute: STR, AGI or INT, whichever the champion's primary is. Other attributes do not change it. A granted pet on an INT caster scales with INT.
- **Owner cooldown reduction.** Every pet ability cooldown, whether autocast, the special or an owner command, goes through the owner's CDR (`Cires::CooldownSeconds`, with the same floor as the owner's skills).
- **Owner attack speed.** The pet's swing timer is its `attackSeconds` divided by the owner's attack-speed multiplier: 1 + AGI%, plus items, class trait and haste fields, times Battle Rhythm.

Other owned units can reuse the helpers in `CirePets.h`: `OwnerPrimaryScale(Owner)`, `OwnerAttackSpeed(Owner)` and `OwnerCooldown(Owner, BaseSeconds)`.

## Ashfang, the Huntress's sabercat

| Stat | Value |
|---|---|
| Health | 420, +60 per level, +35% of the owner's max health |
| Damage | 20, +3.5 per level, +0.45× AGI, every 1.25 s at 1.9 m |
| Move speed | 600 |
| Threat multiplier | 2.0 |
| Damage taken | 85% |

Each ability is a Huntress skill in the Ability Database, in the Skill Shop's **COMPANION** filter. Each is also an ability the cat has innately.

| Skill | Kind | Owner cast | Pet on its own |
|---|---|---|---|
| **Ashfang: Pounce** (`sabercat_pounce`) | active (enemy) | Ashfang leaps up to 7 m onto your target and mauls everything within 2.6 m: 75 + 1× AGI and a 40% slow for 2 s. CD 12 s. | Autocast at 3.2 to 7 m. |
| **Ashfang: Maul** (`sabercat_maul`) | active (enemy) | Ashfang mauls your target for 70 + 1.2× AGI, with +50% threat. CD 7 s. | Autocast in melee. |
| **Ashfang: Dread Roar** (`sabercat_roar`) | active (self) | Enemies within 4.5 m of the cat take 30 + 0.4× AGI, are slowed 35% for 3 s and are taunted onto Ashfang for 2 s. CD 16 s. | Only on command: this is the special. |

When the owner casts one of these skills, the order is queued on the pet: it closes in and then acts. A cast only costs energy and starts its cooldown once the order is accepted. It is refused when:

- the pet is dead or away;
- the pet has no hostile target;
- the target is out of the leash range.

An owner cast ignores the pet's own cooldown for that ability, and then puts the ability on cooldown.

The Huntress's six actives are:

- Bouncing Glaive
- Ashfang: Pounce
- Owl Scout
- Moonlit Sprint
- Crescent Volley
- Ashfang: Maul

Dread Roar joins her identity kit through `SIGNATURE_EXTRA`, so it can be bought in the Skill Shop. The old Sabercat Rake cone has been removed: it was a mount attack.

### Body

The Tripo sabercat, `/Game/Tripo/Champions/HuntressSabercat/CTS_Mount_HuntressSabercat`, is rigged but has no clips yet. `Pets.json` binds it with motion `quadruped_procedural`.

`FCireQuadRig::Analyze` finds the rig from the reference skeleton alone; there is no table of bone names:

- **Legs.** The four ground chains become the legs. They are split front/rear and left/right using the mesh's facing (`yaw`).
- **Spine.** The path between the hip branch and the shoulder branch.
- **Neck and head.** The forward-most free chain.
- **Tail.** The rear-most free chain.

`UCireQuadrupedAnimInstance` then drives the body:

- a diagonal trot with planted paws: the phase rate is derived from leg length and speed, as for the Bear;
- idle breathing, a head that looks around, and a swaying tail;
- a lunge on each attack or ability;
- reaching legs while in the air (pounce);
- a roll onto the side when dead.

**Clip override.** When the 3D collector writes `Content/Data/PetArt.tripo.json`, a ready row for a pet replaces the procedural body with its clips. The row uses motion `monster_native` (idle, walk, run, attack, death), and the procedural body becomes its fallback. Either of these shapes is accepted:

```json
{"pets": [{"petId": "sabercat", "status": "ready", "mesh": "/Game/...", "heightCm": 118, "yaw": -90,
           "animations": {"idle": "...", "walk": "...", "run": "...", "attack": "...", "death": "..."}}]}
```

```json
{"sabercat": {"status": "ready", "animations": {"idle": "...", "walk": "...", "run": "...", "attack": "..."}}}
```

**Fallback.** If a pet's primary body cannot be used (a missing mesh, or a rig the analysis rejects), `UCireChampionArt` falls back to the binding's `fallback` body. For Ashfang this is the animated Quaternius wolf, tinted as a sabercat.

A pet always wears its creature body. It does not need the `-CireTripoChampions` review flag.

## HUD pet frame

The existing `Pet` panel slot (default 250×112 at 290,306, movable in the layout editor) shows the companion frame. It is built with the CireUIStyle kit and follows the active UI theme:

- **Header.** The pet's painted portrait in the theme's portrait ring, with a level medallion, its name, and the line family / stance / order.
- **Health.** A health bar with a damage trail, and the pet's buffs and debuffs.
- **Order buttons.** ATTACK, FOLLOW and STAY. Each shows its key, and the active order is highlighted.
- **Special.** An icon slot with a cooldown sweep and its key.
- **Revive / call.** An icon slot that glows when a revive is ready and shows the revive cooldown.
- **Stances.** Buttons for the three stances; the current one is selected.
- **Pet down.** When the pet is dead or away, the frame shows "FALLEN" or "RESTING" with the revive and return timers.

Owners without a companion keep the previous summons strip, for the Oathbound Guardian and the Spectral Pack.

## Design: a second pet class (later)

### The Bone-Warden (necromancer) and the bone hound Gnash

This is an INT caster with a support role. His companion is disposable, so it plays very differently from the sabercat. Ashfang is a bruiser you keep alive; Gnash is a resource you spend.

- **Gnash.** A skeletal hound. It has 70% of the sabercat's health, deals shadow damage, and has threat multiplier 1.0, so it is not built to hold aggro. It takes 110% damage.
  - **Rattle Bones** (special): a 4 m fear pulse. Monsters flee for 1.5 s (bosses: they are slowed instead).
  - **Grave Lunge** (autocast): a leap that applies a 20% healing reduction for 4 s.
- **Resummon.** Resummoning Gnash is instant and free, but only from a fresh corpse. When any monster dies within 10 m of the Bone-Warden, he can raise Gnash from it, at full health, with a 6 s cooldown. There is no revive.
- **Owner kit** (commands and synergy):
  - **Corpse Explosion:** Gnash detonates for 120 + 2× INT in 3 m and dies. With the fresh-corpse rule, this is his burst loop.
  - **Bone Armor:** a 3 s shield on the owner equal to 40% of Gnash's current health.
  - **Soul Leash:** for 6 s, 50% of the damage the owner takes is redirected to Gnash.
- **Data.** A new `Pets.json` row, plus `owners: {"bone_warden": "bone_hound"}`. The body is the Quaternius wolf re-tinted bone-white with a green eye glow until a Tripo hound exists.
- **Code.** Two new ability kinds for `ACirePet::Use`: `fear` and `detonate`. Everything else (stances, commands, frame, threat, scaling) is shared.

### Alternative: the Beastmaster

A STR bruiser who can bond one of three pets out of combat: sabercat (DPS), bear (tank, threat multiplier 4, taunt special) or raptor (fast, bleed). He would reuse `PetGrant` (see below) to switch the pet between waves.

## Giving any champion a pet (pet talent)

`ACireHero::PetGrant` (replicated) overrides the profile's own companion. Setting it to a `Pets.json` id is enough to give any champion a pet. Summoning, the HUD frame, keybinds and all commands follow automatically, and clearing it dismisses the pet.

A talent or skill would work like this:

1. **Grant.** A passive skill, for example "Beast Bond", sets `PetGrant` on learn, with a Pets.json id chosen per role. Tanks would get a bear with a taunt special; healers a spirit owl that heals as its special.
2. **Balance.** Scale a granted pet down with a `grantScale` on its stats (suggested 0.6). Its owner's kit does not command it, so it relies on its autocast and its special.
3. **Owner commands.** Command skills (`category: "pet"`, `EDelivery::Pet`) can be added to any kit. A cast fails cleanly, and costs nothing, when there is no pet.

## Tests

`CirePets::RunSmoke`, run from `RunExpansionChecks --only native`, checks:

- **Data.** Pets.json and its Ability DB and keybinding links load.
- **The Huntress.**
  - Her roster kit and art binding: on foot, no mount, rider or wolf.
  - Her Tripo body.
- **Summoning.**
  - One persistent pet, not a roster champion.
  - It scales with the owner's level and stats.
- **Movement.**
  - Follow.
  - Teleport when left behind.
  - Stay and Follow commands.
- **Attacking.**
  - The attack command, and basic attacks at the pet's scaled damage.
  - The pet threat multiplier.
- **Threat share.**
  - Only while the pet is engaged, and none while Passive.
  - The pet holding aggro against its owner.
- **Stances.**
  - Passive ignores the owner's fight.
  - Defensive assists the owner and defends against attackers.
  - Aggressive engages nearby enemies, but never a neutral pack.
- **Abilities.**
  - The roar special: slow, taunt, cooldown.
  - The Huntress casting Maul (queued; bonus threat) and Pounce (leap and landing).
- **Death and revive.**
  - Death leaves a corpse, drops threat and starts the return timer.
  - The corpse is never revived at base.
  - Revive range and cooldown.
  - Resummon at full health, keeping the stance.
- **Owner lifecycle.**
  - The pet leaves with a dead owner and returns with the revived owner.
  - The pet-talent grant, and dismissal when it is revoked.
- **Owner power.**
  - A monster takes the pet as its victim and can hit it.
  - Damage scales with the owner's primary stat (AGI for the Huntress; INT for an INT champion), and a non-primary stat does not change it.
  - The owner's CDR shortens pet ability cooldowns.
  - The owner's attack speed sets the pet's swing timer.
- **Replication.** The replicated property flags of the pet and the owner.

`CireExpansionNetProbe`, run from `RunExpansionChecks --only network`, adds these checks with real clients:

- **Survival.**
  - Each client sees its own pet with the replicated id, stance and ability cooldowns.
  - Each client does not see the other team's pet (PvE privacy).
  - A Stay command sent by the client over RPC comes back replicated.
- **Owner death.** The pet is gone when its owner dies.
- **Arena.**
  - Both teams' pets are visible.
  - An enemy champion's damage to a pet replicates with the 85% damage-taken rule.

## Known limits

- **Clips.** The sabercat has no clips; its motion is procedural until `PetArt.tripo.json` arrives.
- **Uncapped pet stats.** Pet stats are not capped by `CombatTuning`, and the numbers have not been through the Balance Lab.
- **Bots.** Bot Huntresses get their pet, keep it in the default Defensive stance, and never use its commands.
- **Minimap.** Pets have no minimap marker.
- **Borrowed icons.** The Maul and Dread Roar icons reuse painted art: Sabercat Rake's claws and the planned Bear Roar. Fresh paintings would replace `Art/Icons/ChatGPT/Abilities/sabercat_maul.png` and `sabercat_roar.png`.
- **Short front leg.** The Tripo sabercat rig lacks lower bones on one front leg, so the procedural rig swings that leg from the shoulder as one piece.

## Verification (2026-09-25)

| Check | Result |
|---|---|
| `RunExpansionChecks --only native` | `CIRE_PETS_PASS checks=86`, `CIRE_COMBAT_EXPANSION_PASS` |
| `RunExpansionChecks --only network` | PASS with `pets=1`; `CIRE_PET_NET_CLIENT_PASS` on both clients |
| `RunInterfaceSmoke` | PASS |
| Gallery | `RunNewChampionsGallery --only huntress_close,combat_huntress,pet_roar,hud_pet`: 8 captures |

The analyzed rig is logged as `CIRE_PET_RIG ... legs FL=bone_4 FR=bone_8 RL=bone_19 RR=bone_24 spine=3 neck=3 tail=4`.
