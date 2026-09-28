# Paragon Champions

Eric added Epic's full Paragon character collection: 39 packs, 38 heroes plus ParagonMinions. Every hero is now a
playable champion. Each one uses its own skeletal mesh, materials, animation clips and particle systems, and its own
Paragon abilities are its base kit. The numbers are retuned to this game.

## Licensing: local only

The packs are Epic-licensed. They live in `F:\CiresTeamSurvival\Content\Paragon*` and are junctioned into agent
worktrees. `.gitignore` excludes `Content/Paragon*`, which also covers the locally generated
`Content/ParagonDerived` (the portraits). **Nothing from a pack is ever staged or committed.**

What *is* committed is code, plus `Content/Data/ParagonChampions.json`. That file only references `/Game/Paragon*`
paths. A hero registers only when its mesh package exists locally. On a clean clone, or with `-CireNoParagon` or
`-CireNoFab`, nothing registers and the game plays exactly as before. The native smoke test checks both cases.

Gallery captures and portraits are renders of Epic content, so they also stay local (`Saved/`, `Content/ParagonDerived`).

## Pipeline

| Step | Tool | Output |
|---|---|---|
| 1. Inventory the packs (UE commandlet) | `Tools/InspectParagon.py` | `Saved/ParagonInspect.json`: meshes, skins, anim blueprints, every anim sequence and montage (length, slot, skeleton), Cascade/Niagara systems per ability folder, sound cues (local, not committed) |
| 2. Author kits, bindings and registration | `Tools/AuthorParagonChampions.py` (`--check` exits 1 when stale) | `Content/Data/ParagonChampions.json` and the table below |
| 3. Portraits | `Tools/RunParagonPortraits.py` | `/Game/ParagonDerived/Portraits/T_Portrait_<id>` (local) |
| 4. Review gallery | `Tools/RunParagonGallery.py [--only pg_greystone,...]` | `Saved/ParagonGallery/<stamp>/`: one close-up per hero, plus one shot per own ability |

Run step 1 with:

```
UnrealEditor-Cmd CiresTeamSurvival.uproject -run=pythonscript -script=<abs>/Tools/InspectParagon.py -unattended -nullrhi
```

## `ParagonChampions.json`

- `roster`: a `CireChampionRoster` document. It uses the same schema and validation as `ChampionRoster.json`.
  `CireChampionRoster::Reload` appends the installed heroes after the authored roster. The 64-champion cap applies
  only to the authored file.
- `abilities` / `champions` / `schools`: an Ability DB overlay in the same schema as `Abilities.json`.
  `CireAbilityDB::Reload` merges it: it adds the abilities and the Paragon kits.
- `poolAdditions`: the own Paragon **actives**, grouped by type (DPS / TANK / HEAL). Each one joins the Skill Shop pool
  of every champion whose kit has that type. Ultimates and passives stay signature-only.
- `recipes`: how each ability lands (delivery), plus its Paragon cast/impact FX and voice. Numbers never live here.
- `bindings`: `monster_native` rows for the hero's own mesh. They list idle, walk, run, attack, alternate attack,
  hit, death, and one cast clip per ability id. These are the same row shape as `ChampionArtBindings.fab.json`, and
  `CireChampionArt` reads both files.
- `heroes`: presentation and the inventory summary (pack, skins, anim blueprints, voice cues, portrait, background).
- `creeps`: the ParagonMinions inventory (see below).
- `problems`: anything the author could not resolve.

## Gameplay (`CireParagonChampions`)

The module follows the same contract as `CireKitSkills`. `CireSignatureSkills` routes to it for Knows, Handles,
IsPassive, Cast, DescribeShape and the damage/speed hooks.

- **Deliveries:** projectile, pierce, cone, circle, line, self_burst, strike, lunge, leap, dash, pull, ally_heal,
  party_heal, self_buff, storm and mark.
- **Cast checks:** validation, costs, cooldowns, realm/aim checks and the Skill Shop cast level all match the
  signature skills.
- **Numbers:** they come from the Ability DB row. Damage and healing use base + coefficient × PRIMARY via
  `CireKits::Amount`. Utility effects use potency, which also scales with PRIMARY. Crowd control uses the row's
  `effects`, which `CireCrowdControl::OnAbilityHit` applies automatically.
- **Buffs:** `pg_guard` (less damage taken), `pg_haste` (move speed), `pg_frenzy` (attack speed), `pg_empower`
  (damage) and `pg_marked` (more damage taken). Each has a `BuffVisuals.json` signature visual and a
  `BuffModifiers.json` row.
- **Passives:** `guard`, `lifesteal`, `haste`, `frenzy` and `power`. Each is a percentage, scales with potency and is
  capped at 60%.
- **Presentation:** `CireSpellPresentation::Play` also calls `CireParagonChampions::PlayFX`. On top of the school
  telegraph, this spawns the hero's own Cascade/Niagara systems: cast systems at the caster, impact systems at the
  aim. Paragon loops are stopped after the ability's beat. The ultimate plays the hero's own voice line.
- **Body:** `UCireCreatureArt` drives the Paragon mesh natively (`monster_native` + reactions):
  - locomotion (idle / jog, stride-matched, turn in place, leg IK)
  - alternating primary-attack clips synced to the server's release frame
  - one cast clip per ability, which starts when that slot's cooldown starts
  - a flinch on damage
  - a held death clip

  Paragon weapons are part of the skinned mesh, so grips are exact by construction. The Paragon anim blueprints are
  inventoried, but the game does not run them: see "Anim blueprints" below.

## Champion select

- Heroes appear after the authored roster, in the same grid, filters and pages.
- The live 3D preview uses the real Paragon body.
- **Portrait:** `/Game/UI/Draft/Portraits` first, then the local `/Game/ParagonDerived/Portraits` capture.
- **Background:** the hero's closest-theme existing painting (`heroes[].background`) until a dedicated painting exists.

## Roles and primary stats

Roles follow the Paragon role, mapped onto this game's roles. Threat role and primary stat follow the class traits
(Tank: STR, flat −10 per hit; DPS: 10% crit; Support: heals the lowest-HP ally). The four own abilities are RMB, Q
and E (actives) and R (the ultimate). Actives 4–6 of the draft example come from the role template champion
(Knight, Troll Berserker, Ranger, Wizard or Scholar). That template's pool also seeds the hero's Skill Shop pool.

<!-- paragon-table -->

| Champion | Class | Roles | Primary | Basic attack | Own kit (RMB / Q / E / ultimate) | Passive | Paragon FX groups | Height |
|---|---|---|---|---|---|---|---|---|
| Greystone | Stoneguard Knight | tank, damage | STR | sword melee (230) | Leap Smash (RMB) / Clear a Path (Q) / Deflect (E) / Hard to Kill (R) | Stone Oath | ClearAPath, Deflect, Greystone, LeapAOE, Primary, Ultimate | 187 cm (native 206) |
| Aurora | Frost Duelist | damage, tank | AGI | sword melee (230) | Glacial Charge (RMB) / Hoarfrost (Q) / Frozen Simulacrum (E) / Cryoseism (R) | Winter's Edge | Aurora, Dash, Freeze, Leap, Primary, Ultimate | 176 cm (native 181) |
| Crunch | Cybernetic Brawler | damage, tank | STR | claws melee (220) | Dashing Cross (RMB) / Uppercut (Q) / Gut Punch (E) / Hook Empowered (R) | Pit Fighter | Cross, Hook, Primary, Ultimate, Uppercut | 212 cm (native 261) |
| Feng Mao | Blade Samurai | damage, tank | STR | sword melee (230) | Dash Strike (RMB) / Tranquility (Q) / Slowing Strike (E) / Blade Rush (R) | Ronin's Resolve | Block, BoneCollector, Dash, Primary, RoyalGuardUndertow, SlowShield, Ultimate | 191 cm (native 215) |
| Grux | Warlord | tank, damage | STR | axes melee (240) | Double Pain (RMB) / Stampede (Q) / Rippling Smash (E) / Warlord's Challenge (R) | Thick Hide | HardKnocks, Primary, RipplingSmash, Skins, Stampede, Ultimate | 194 cm (native 222) |
| Steel | Bulwark | tank, support | STR | flail melee (230) | Shield Bash (RMB) / Cow Catcher (Q) / Shield Wall (E) / Ground Smash (R) | Ability Armor | AbilityArmor, Bash, Primary, ShieldBlock, Steel, Ultimate | 250 cm (native 355) |
| Terra | Mountain Warden | tank, damage | STR | axes melee (240) | Shield Charge (RMB) / Double Sweep (Q) / Balance (E) / Wrath of the Mountain (R) | Stone Skin | Balance, DoubleSweep, Primary, Shield, Terra, Ultimate | 206 cm (native 249) |
| Sevarog | Soul Reaper | tank, damage | STR | axes melee (250) | Soul Siphon (RMB) / Relentless (Q) / Stage of Harvest (E) / Subjugate (R) | Soul Eater | Primary, Relentless, SoulSiphon, SoulStackPassive, Subjugate, Ultimate | 234 cm (native 311) |
| Rampage | Jungle King | tank, damage | STR | claws melee (250) | Ground Smash (RMB) / Rip and Toss (Q) / Energize (E) / Enrage (R) | King of the Jungle | JungleKing, Lunge, Primary, Rampage_v001_IceBlue, RipNToss, Roar, Ultimate | 194 cm (native 221) |
| Kwang | Heaven's Blade | damage, tank | STR | sword melee (240) | Heaven's Bolt (RMB) / Sword of Light (Q) / Hallowed Guard (E) / Judgement of Heaven (R) | Chosen of the Blade | FX, LightStrike, Primary, Sword, Ultimate | 182 cm (native 195) |
| Kallari | Shadow Assassin | damage | AGI | claws melee (220) | Shadow Strike (RMB) / Dagger Throw (Q) / Marked for Death (E) / Death Blossom (R) | Shadow Plane | DaggerThrow, Dodge, Kallari, Passive, Primary, ShadowPlane, Ultimate | 176 cm (native 182) |
| Countess | Blood Countess | damage | INT | claws melee (220) | Blink Strike (RMB) / Blade Siphon (Q) / Rolling Dark (E) / Feast (R) | Bloodthirst | BladeSiphon, BlinkStrike, Primary, RollingDark, Ultimate | 176 cm (native 181) |
| Shinbi | Wolf Dancer | damage | AGI | sword melee (220) | Rending Wolves (RMB) / Circling Wolves (Q) / Wolf Dash (E) / Dance of Death (R) | Wolf Pack | AttackWolves, CirclingWolves, Dash, Emotes, Primary, Shinbi_Wolf, Ultimate | 180 cm (native 189) |
| Khaimera | Bloodhound | damage | STR | claws melee (230) | Leap (RMB) / Glaive Throw (Q) / Bloodlust (E) / Dash of Blood (R) | Hunter's Appetite | Leap, Primary, ThreeStrikeBuff, Ultimate, WarriorSustain | 189 cm (native 210) |
| Serath | Fallen Seraph | damage | STR | flail melee (230) | Ascend (RMB) / Heaven's Fury (Q) / Chastise (E) / Retribution (R) | Wings of Wrath | Ascend, Chastise, Fury, Primary, Ultimate | 181 cm (native 193) |
| Sun Wukong | Monkey King | damage | AGI | lance melee (250) | Staff Poke (RMB) / Cloud Flip (Q) / Mischief (E) / Staff of Heaven (R) | Nimble Trickster | DoubleJump, Poke, Primary, Toggle, Ultimate, Wukong | 182 cm (native 196) |
| Yin | Lash Dancer | damage | AGI | sword melee (230) | Whip Crack (RMB) / Reaching Pain (Q) / Lash Kick (E) / Thousand Cuts (R) | Blade Rhythm | LashKick, Lashkick, Primary, ReachingPain, Ultimate, WhipCrack, Whipcrack, Yin | 176 cm (native 181) |
| Boris | Serum Beast | damage, tank | STR | claws melee (230) | Swipe (RMB) / Tracker (Q) / Inject (E) / Unleashed (R) | Serum Rush | AOE, Forage, Primary, Pull, Shield, Swipe, Trail, Ultimate | 211 cm (native 259) |
| Riktor | Chain Warden | tank, support | STR | flail melee (230) | Hook (RMB) / Shocking Punch (Q) / Lockdown (E) / Electro Chain (R) | Warden's Plate | BadSanta, Charge, Hook, Primary, ShockingGround, ShockingPunch, Ultimate | 211 cm (native 259) |
| Sparrow | Warrior Archer | damage | AGI | bow ranged (1400) | Draw a Bead (RMB) / Rain of Arrows (Q) / Inner Fire (E) / Arrow Storm (R) | Keen Eye | DrawABead, Primary, RainOfArrows, Sparrow, Ultimate | 210 cm (native 256) |
| TwinBlast | Double Gunslinger | damage | AGI | blunderbuss ranged (1200) | Nitro Dash (RMB) / Vortex Grenade (Q) / Charge Blast (E) / Double Trouble (R) | Showboat | Dive, Ego, Nitro, Primary, SummerTime, Ultimate, VortexGrenade | 182 cm (native 195) |
| Murdock | Bounty Marshal | damage | AGI | blunderbuss ranged (1000) | Spread Shot (RMB) / Tazer Trap (Q) / Gun Shield (E) / The Eleven (R) | Marshal's Grit | GunShield, Passive, Primary, SpreadShot, TazerTrap, Ultimate | 180 cm (native 190) |
| Revenant | Revenant Gunslinger | damage | AGI | blunderbuss ranged (1300) | Haunting Shot (RMB) / Mark of the Revenant (Q) / Obliterate (E) / Death Sentence (R) | Undying Hunt | Mark, Obliterate, Primary, Reload, Revenant, Ultimate | 229 cm (native 299) |
| Wraith | Shadow Sniper | damage | AGI | blunderbuss ranged (1500) | Scoped Shot (RMB) / Shadow Canon (Q) / Shadow Step (E) / Deadeye (R) | Patient Hunter | Drone, Primary, ScopedShot, TeleportTarget, Ultimate | 182 cm (native 194) |
| GRIM.exe | Salvage Gunner | damage | AGI | blunderbuss ranged (1200) | Deathbolt (RMB) / Sap Zap (Q) / Energy Shield (E) / Overclock (R) | Scrap Plating | BFG, EnergyShield, Passive, Primary, SlowOnHit, Ultimate | 234 cm (native 310) |
| Drongo | Boomerang Raider | damage | AGI | blunderbuss ranged (1200) | Boomerang (RMB) / Pyro Grenade (Q) / Drongo Rounds (E) / Bazooka (R) | Scavenger | Boomerang, Emotes, Grenade, Primary, Shards, Ultimate | 181 cm (native 192) |
| Gideon | Cosmic Sorcerer | damage | INT | arcane ranged (1100) | Cosmic Rift (RMB) / Burden (Q) / Torn Space (E) / Black Hole (R) | Cosmic Insight | Burden, Gideon, Meteor, Portal, Primary, ProjectileMeteor, Ultimate | 178 cm (native 187) |
| Howitzer | Artillery Mech | damage | INT | arcane ranged (1100) | Missile Swarm (RMB) / Det Charge (Q) / Slow Grenade (E) / LRM Barrage (R) | Payload | DetCharge, LRM, MissileSwarm, Mobility, Primary, SlowGrenade, Ultimate | 215 cm (native 268) |
| Iggy & Scorch | Pyro Duo | damage | INT | arcane ranged (1000) | Scorch Breath (RMB) / Flame Turret (Q) / Oil Slick (E) / Fire Storm (R) | Pyromania | IggyScorch, OilTrail, OilTrap, Passive, Primary, Recall, Turret, Ultimate, Ultimate_V2 | 190 cm (native 212) |
| Lt. Belica | Mana Tactician | damage, support | INT | arcane ranged (1100) | Neural Disruptor (RMB) / Mana Bomb (Q) / Eruption Beam (E) / Neural Detonator (R) | Mana Siphon | Belica, EruptionBeam, EruptionBeamHeavy, ManaBomb, Primary, TeslaConduit, Ultimate | 187 cm (native 206) |
| Morigesh | Hex Witch | damage | INT | arcane ranged (1100) | Swarm of Insects (RMB) / Stab the Doll (Q) / Life Drain (E) / Blood Price (R) | Hexweaver | LifeDrain, Morigesh, Primary, SkillshotAOE, StabDoll, Ultimate | 182 cm (native 195) |
| Gadget | Gadgeteer | damage | INT | arcane ranged (1100) | Power Mine (RMB) / Charge Shot (Q) / Overdrive (E) / Speed Gate (R) | Tinkerer | ElectroGate, Primary, RollingBot, StickyBomb, Ultimate, VisionBot | 186 cm (native 202) |
| Zinx | Plague Medic | healer, support | INT | arcane ranged (1100) | Heal Shot (RMB) / Toxin Shot (Q) / Stun Shot (E) / Mass Revival (R) | Field Medicine | DotShot, HealShot, Primary, StunShot, Ultimate, Zinx | 186 cm (native 203) |
| The Fey | Grove Mother | support, healer, damage | INT | staff ranged (1100) | Nettles (RMB) / Brambles (Q) / Life Bloom (E) / Mother of the Grove (R) | Verdant Heart | Brambles, Fey, Growth, Nettles, Primary, Ultimate | 179 cm (native 188) |
| Muriel | Guardian Angel | healer, support | INT | staff ranged (1100) | Reversal of Fortune (RMB) / Consecrated Ground (Q) / Winged Boots (E) / Guardian Angel (R) | Grace | Boots, ConsGround, LifeLock, Primary, Ultimate | 184 cm (native 199) |
| Narbash | War Drummer | support, healer, tank | INT | totem melee (230) | Lantern Toss (RMB) / Healing Drum (Q) / Drum Rush (E) / Grand Finale (R) | Rhythm of War | Dash, Primary, Shield, Skins, Throw, Ultimate | 193 cm (native 220) |
| Phase | Psionic Linker | support, healer | INT | arcane ranged (1100) | Psionic Link (RMB) / Clarity Beam (Q) / Psionic Flash (E) / Mind Storm (R) | Shared Mind | Beam, Flash, Link, Primary, Ultimate | 176 cm (native 180) |
| Dekker | Stasis Engineer | support, damage | INT | arcane ranged (1100) | Stasis Bomb (RMB) / Slow Field (Q) / Rocket Boots (E) / Containment Fence (R) | Stasis Engineer | BotPassive, Cage, Line, Primary, RocketBoots, SkyBeam, SlowBomb, SlowField, StasisBomb, Ultimate | 178 cm (native 186) |

<!-- /paragon-table -->

## Anim blueprints

Every hero's `*_AnimBlueprint` is recorded in `heroes[].animBlueprint`. The game plays the same Paragon clips through
its native layer instead of running those blueprints, for four reasons:

- The blueprints' event graphs expect Paragon's gameplay variables (targeting states, ability flags, stance
  enums), which this game does not have.
- Cast and attack clips must meet the server's release frame (`contact`).
- The game's own locomotion feel (turn in place, leg IK, stride matching) and hit/death reactions apply to every
  champion.
- The blueprints' montage slots differ by hero.

Switching a hero to its blueprint later only needs a new `motion` kind in the binding row.

## ParagonMinions (creeps)

The minions pack has no heroes. `creeps` records its units: lane melee, ranged, siege and super minions in both
colours, and the jungle buff creatures (Black/Blue/Green/Red/White), each with meshes and anim blueprints. They are
good candidates for monster variants: lane waves (melee / ranged / siege / super), and the buff creatures as challenge
packs or bosses. Nothing uses them yet. Wiring them in is a follow-up for the monster-race system (`MonsterArt.json`).

## Unusable or partial content

See the `problems` list in the JSON and the gallery `CIRE_PARAGON_GALLERY_POSE` lines. The notes below are also
maintained in `Docs/RESUME-paragon-champions.md`.
