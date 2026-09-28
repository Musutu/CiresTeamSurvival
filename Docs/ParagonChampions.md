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

- Heroes appear in the same paged browser as the authored roster. The PARAGON chip (or the keyword "paragon") filters them.
- The live 3D preview uses the real Paragon body. It streams after 0.9 s of hover (`paragonHoverDebounceSeconds`,
  Content/Data/DraftSelect.json) because each Paragon mesh rebuilds its render data for 5-7 s on its first editor load.
  Run `python Tools/ResaveParagonMeshes.py` once, with all editors closed, to stop the rebuild.
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
| Dragonlord | Wyrm Knight | damage, tank | STR | sword melee (230) | Wyrmfall (RMB) / Dragon Path (Q) / Scaled Ward (E) / Wrath of the Wyrm (R) | Dragonblood | ClearAPath, Deflect, Greystone, LeapAOE, Primary, Ultimate | 187 cm (native 206) |
| Novaborn | Star Paladin | tank, support | INT | sword melee (230) | Starfall (RMB) / Comet Path (Q) / Nova Aegis (E) / Supernova (R) | Starforged | ClearAPath, Deflect, Greystone, LeapAOE, Primary, Ultimate | 187 cm (native 205) |
| Mephisto | Hellfire Magus | damage | INT | arcane ranged (1100) | Hellrift (RMB) / Brimstone Weight (Q) / Infernal Door (E) / Abyssal Maw (R) | Infernal Pact | Burden, Gideon, Meteor, Portal, Primary, ProjectileMeteor, Ultimate | 178 cm (native 187) |
| Grand Inquisitor | Zealot Mage | support, healer | INT | arcane ranged (1100) | Judgement Rift (RMB) / Penance (Q) / Absolution (E) / Holy Singularity (R) | Zeal | Burden, Gideon, Meteor, Portal, Primary, ProjectileMeteor, Ultimate | 178 cm (native 187) |
| Royal Guard | Palace Sentinel | tank | STR | sword melee (230) | Guardian Charge (RMB) / Throne Wall (Q) / Halberd Sweep (E) / Royal Decree (R) | Oath of the Throne | Block, BoneCollector, Dash, Primary, RoyalGuardUndertow, SlowShield, Ultimate | 194 cm (native 222) |
| Qilin | Celestial Beast | damage, tank | AGI | axes melee (240) | Thunder Hooves (RMB) / Sky Stampede (Q) / Storm Rend (E) / Celestial Roar (R) | Heaven Stride | HardKnocks, Primary, RipplingSmash, Skins, Stampede, Ultimate | 210 cm (native 256) |
| Magmalord | Molten Warlord | tank | STR | axes melee (240) | Lava Fists (RMB) / Eruption Stampede (Q) / Magma Ripple (E) / Caldera Challenge (R) | Molten Core | HardKnocks, Primary, RipplingSmash, Skins, Stampede, Ultimate | 197 cm (native 228) |
| Mecha Terror | War Engineer | damage, tank | INT | arcane ranged (1000) | Arc Breath (RMB) / Tesla Turret (Q) / Coolant Slick (E) / Meltdown (R) | Overclocked | IggyScorch, OilTrail, OilTrap, Passive, Primary, Recall, Turret, Ultimate, Ultimate_V2 | 181 cm (native 192) |
| Phoenix Rider | Firebird Caller | damage, support | INT | arcane ranged (1000) | Phoenix Breath (RMB) / Ember Nest (Q) / Ash Trail (E) / Rebirth Storm (R) | Rebirth Flame | IggyScorch, OilTrail, OilTrap, Passive, Primary, Recall, Turret, Ultimate, Ultimate_V2 | 204 cm (native 244) |
| Spider Witch | Venom Matriarch | damage | INT | arcane ranged (1100) | Web Snare (RMB) / Venom Sac (Q) / Silk Lash (E) / Brood Queen Bite (R) | Brood Venom | Belica, EruptionBeam, EruptionBeamHeavy, ManaBomb, Primary, TeslaConduit, Ultimate | 187 cm (native 206) |
| Siege Lieutenant | Armoured Tactician | tank, support | STR | arcane ranged (1100) | Suppression Drone (RMB) / Shock Mortar (Q) / Breach Beam (E) / Command Override (R) | Plated Command | Belica, EruptionBeam, EruptionBeamHeavy, ManaBomb, Primary, TeslaConduit, Ultimate | 188 cm (native 207) |
| Northern Mystic | Frost Shaman | healer, support | INT | arcane ranged (1100) | Frost Swarm (RMB) / Rime Doll (Q) / Aurora Mend (E) / Northern Lights (R) | Aurora Blessing | LifeDrain, Morigesh, Primary, SkillshotAOE, StabDoll, Ultimate | 185 cm (native 202) |
| The Executioner | Headsman | damage | AGI | blunderbuss ranged (1000) | Grim Spread (RMB) / Shackle Trap (Q) / Iron Hood (E) / The Sentence (R) | Final Verdict | GunShield, Passive, Primary, SpreadShot, TazerTrap, Ultimate | 180 cm (native 190) |
| Chronoboss | Time Enforcer | damage | AGI | blunderbuss ranged (1300) | Paradox Round (RMB) / Time Mark (Q) / Temporal Rupture (E) / End of Time (R) | Borrowed Seconds | Mark, Obliterate, Primary, Reload, Revenant, Ultimate | 229 cm (native 299) |
| Frost King | Glacial Tyrant | tank, damage | STR | blunderbuss ranged (1300) | Frozen Bolt (RMB) / King Mark (Q) / Glacier Break (E) / Eternal Winter (R) | Permafrost | Mark, Obliterate, Primary, Reload, Revenant, Ultimate | 231 cm (native 303) |
| Masked Reaper | Harvest Shade | damage | AGI | axes melee (250) | Soul Hook (RMB) / Shade Rush (Q) / Harvest Arc (E) / Final Harvest (R) | Reaper Due | Primary, Relentless, SoulSiphon, SoulStackPassive, Subjugate, Ultimate | 234 cm (native 311) |
| Raven Queen | Shadow Archer | damage | AGI | bow ranged (1400) | Raven Shot (RMB) / Crow Rain (Q) / Night Feathers (E) / Storm of Crows (R) | Murder of Crows | DrawABead, Primary, RainOfArrows, Sparrow, Ultimate | 226 cm (native 292) |
| Doomsday | Apocalypse Engine | tank, damage | STR | flail melee (230) | Doom Bash (RMB) / Ram Engine (Q) / Reactor Wall (E) / Doomsday Impact (R) | Reactor Plating | AbilityArmor, Bash, Primary, ShieldBlock, Steel, Ultimate | 250 cm (native 360) |
| Great Sage | Cloud Master | support, damage | INT | lance melee (250) | Heaven Staff (RMB) / Nimbus Slam (Q) / Seventy-Two Forms (E) / Equal of Heaven (R) | Enlightenment | DoubleJump, Poke, Primary, Toggle, Ultimate, Wukong | 208 cm (native 252) |
| Gryphon Knight | Sky Lancer | damage, tank | STR | axes melee (240) | Gryphon Dive (RMB) / Talon Sweep (Q) / Wing Gust (E) / Sky Judgement (R) | Talon Guard | Balance, DoubleSweep, Primary, Shield, Terra, Ultimate | 206 cm (native 249) |
| Star Queen | Astral Sovereign | damage, healer | INT | arcane ranged (1100) | Starlight Mend (RMB) / Astral Needle (Q) / Gravity Spike (E) / Constellation (R) | Stellar Court | DotShot, HealShot, Primary, StunShot, Ultimate, Zinx | 186 cm (native 202) |
| Crash Site | Wreck Brawler | tank, damage | STR | claws melee (220) | Crash Cross (RMB) / Impact Uppercut (Q) / Shrapnel Punch (E) / Meteor Hook (R) | Scrap Armor | Cross, Hook, Primary, Ultimate, Uppercut | 214 cm (native 264) |
| Bash-O-Lantern | Harvest Drummer | damage, support | INT | totem melee (230) | Pumpkin Toss (RMB) / Hollow Drum (Q) / Harvest March (E) / Night of Drums (R) | Hollow Beat | Dash, Primary, Shield, Skins, Throw, Ultimate | 200 cm (native 236) |
| Stone Colossus | Earth Elemental | tank | STR | claws melee (250) | Quake Smash (RMB) / Boulder Toss (Q) / Earthen Surge (E) / Tectonic Rage (R) | Living Rock | JungleKing, Lunge, Primary, Rampage_v001_IceBlue, RipNToss, Roar, Ultimate | 199 cm (native 231) |

<!-- /paragon-table -->

## Skins: reskins, new champions and monster variants

Eric's direction (2026-09-28). Every alternate skin mesh in the packs is classified as one of four things:

- **Reskin:** a cosmetic variant. It is selectable in champion select with the `<` / `>` arrows (or `[` / `]`) above LOCK IN. It keeps the same kit, so Hero Creator loadouts still apply. The pick replicates as `ACireHero::ChampionSkin`, and the art binding is the row `<profile>@<skin>`.
- **New champion:** the model differs enough (silhouette, weapon, theme) to be its own hero. It gets:
  - its own id `<parent>_<skin>`, name, class, roles and primary stat
  - a kit built from the parent's Paragon clips and FX, recoloured through the ability school (telegraphs, runes, sounds), with a re-roled delivery where the new role needs it (for example, a heal)
  - a new passive
  - normal Ability DB rows, so the Ability Tuner and the Hero Creator both pick them up
- **Monster variant:** an extra body of the closest race unit (`Content/Data/RaceMeshes.paragon.json`, generated by the same tool). `CireMonsterArt` merges it into that unit's variants and does not replace them. **This is opt-in (`-CireParagonMonsters`).** With the flag on, the Paragon bodies fail the monster-body checks in `CireMonsterArtTests`:
  - stride speeds are not measured on these rigs (walk 0 / run 0)
  - the unit's props are attached on top of Paragon's built-in weapons
  - the hit clips (knockbacks) break the "upright" pose check
  - two Khaimera skins report the head at the root bone

  Each body needs `walkSpeedCm` / `runSpeedCm`, `dropPropBones` and a calmer hit clip before it can be on by default.
- **Not usable:** parts (drones, ultimate guns, VFX shells, antlers) and meshes on a skeleton that has no clips in the pack.

ParagonMinions are handled the same way: the lane minions (Paragon's robots) become Aetheri variants, and the jungle buffs map by colour to the matching race.

**Classification for Eric's review (skin, decision, reason):**

<!-- paragon-skins -->

| Hero | Skin | Decision | Detail |
|---|---|---|---|
| Greystone | Dragonlord | **new champion** | Dragonlord (Wyrm Knight; damage/tank; STR; fire) |
| Greystone | Novaborn | **new champion** | Novaborn (Star Paladin; tank/support; INT; arcane) |
| Greystone | Tough | **reskin** | same silhouette and weapon: a cosmetic variant |
| Greystone | White Tiger | **reskin** | same silhouette and weapon: a cosmetic variant |
| Aurora | Frozen Hearth | **reskin** | same silhouette and weapon: a cosmetic variant |
| Aurora | Glacial Empress | **reskin** | same silhouette and weapon: a cosmetic variant |
| Aurora | Moon Crystal | **reskin** | same silhouette and weapon: a cosmetic variant |
| Aurora | Spring Ceremony | **reskin** | same silhouette and weapon: a cosmetic variant |
| Crunch | Golden Gloves | **reskin** | same silhouette and weapon: a cosmetic variant |
| Crunch | Military | **reskin** | same silhouette and weapon: a cosmetic variant |
| Crunch | Titanium | **reskin** | same silhouette and weapon: a cosmetic variant |
| Crunch | Valentine | **reskin** | same silhouette and weapon: a cosmetic variant |
| Crunch | Black Site | **monster variant** | aetheri / aetheri_warframe: black-ops cyborg |
| Crunch | Crash Site | **new champion** | Crash Site (Wreck Brawler; tank/damage; STR; storm) |
| Feng Mao | Inner Fire | **reskin** | same silhouette and weapon: a cosmetic variant |
| Feng Mao | Winter | **reskin** | same silhouette and weapon: a cosmetic variant |
| Feng Mao | Royal Guard | **new champion** | Royal Guard (Palace Sentinel; tank; STR; holy) |
| Grux | Halloween | **monster variant** | hollow / ironbound_bruiser: undead brute look |
| Grux | Warchief | **reskin** | same silhouette and weapon: a cosmetic variant |
| Grux | Chestplate | **reskin** | same silhouette and weapon: a cosmetic variant |
| Grux | Molten | **new champion** | Magmalord (Molten Warlord; tank; STR; fire) |
| Grux | Beetle Red | **monster variant** | drakkari / drakkari_scalebreaker: red carapace reads as scaled |
| Grux | Qilin | **new champion** | Qilin (Celestial Beast; damage/tank; AGI; storm) |
| Steel | Aegis | **reskin** | same silhouette and weapon: a cosmetic variant |
| Steel | Black Carbon | **reskin** | same silhouette and weapon: a cosmetic variant |
| Steel | Cobalt | **reskin** | same silhouette and weapon: a cosmetic variant |
| Steel | Diesel | **reskin** | same silhouette and weapon: a cosmetic variant |
| Steel | Doomsday | **new champion** | Doomsday (Apocalypse Engine; tank/damage; STR; fire) |
| Terra | Gryphon Knight | **new champion** | Gryphon Knight (Sky Lancer; damage/tank; STR; storm) |
| Terra | Mountain Forge | **reskin** | same silhouette and weapon: a cosmetic variant |
| Sevarog | Chronos | **monster variant** | stoneborn / stoneborn_forgelord: clockwork titan: a forge boss |
| Sevarog | Bloodred | **monster variant** | fallen_order / dread_knight: blood-armour reaper knight |
| Sevarog | Masked Reaper | **new champion** | Masked Reaper (Harvest Shade; damage; AGI; shadow) |
| Rampage | Elemental | **new champion** | Stone Colossus (Earth Elemental; tank; STR; earth) |
| Rampage | Tough LPMaster For Variants Test | **not usable** | a part of another skin (drone, ultimate gun, VFX shell, antlers), not a body |
| Rampage | Redneck | **monster variant** | feral_kin / werebear_mauler: hulking beast brawler |
| Kwang | Manbun | **reskin** | same silhouette and weapon: a cosmetic variant |
| Kwang | Frostwalker | **reskin** | same silhouette and weapon: a cosmetic variant |
| Kallari | Death Lotus | **monster variant** | voidborn / void_ravager: shadow assassin |
| Kallari | Peppermint | **reskin** | same silhouette and weapon: a cosmetic variant |
| Kallari | Red Death | **reskin** | same silhouette and weapon: a cosmetic variant |
| Kallari | Rogue | **reskin** | same silhouette and weapon: a cosmetic variant |
| Kallari | Tough | **reskin** | same silhouette and weapon: a cosmetic variant |
| Kallari | Wasteland | **reskin** | same silhouette and weapon: a cosmetic variant |
| Countess | Carnivale | **monster variant** | fallen_order / flagellant: masked carnival zealot |
| Countess | Gilded | **reskin** | same silhouette and weapon: a cosmetic variant |
| Countess | Red Revo | **reskin** | same silhouette and weapon: a cosmetic variant |
| Countess | Shogun | **reskin** | same silhouette and weapon: a cosmetic variant |
| Shinbi | Dynasty | **reskin** | same silhouette and weapon: a cosmetic variant |
| Shinbi | Jade | **reskin** | same silhouette and weapon: a cosmetic variant |
| Khaimera | Halloween | **monster variant** | voidborn / rift_stalker: spectral hunter silhouette |
| Khaimera | Snakekiller | **reskin** | same silhouette and weapon: a cosmetic variant |
| Khaimera | Bengal | **reskin** | same silhouette and weapon: a cosmetic variant |
| Khaimera | White Tiger | **reskin** | same silhouette and weapon: a cosmetic variant |
| Khaimera | Grux Pelt | **monster variant** | feral_kin / wild_outrider: wears a beast pelt: a feral raider |
| Khaimera | Grux Pelt Ghost Beast VFX | **not usable** | a part of another skin (drone, ultimate gun, VFX shell, antlers), not a body |
| Serath | Frost | **reskin** | same silhouette and weapon: a cosmetic variant |
| Serath | Future | **reskin** | same silhouette and weapon: a cosmetic variant |
| Serath | Gold | **reskin** | same silhouette and weapon: a cosmetic variant |
| Serath | Valentine | **reskin** | same silhouette and weapon: a cosmetic variant |
| Sun Wukong | Fury | **reskin** | same silhouette and weapon: a cosmetic variant |
| Sun Wukong | Fury FX | **not usable** | a part of another skin (drone, ultimate gun, VFX shell, antlers), not a body |
| Sun Wukong | Future King | **reskin** | same silhouette and weapon: a cosmetic variant |
| Sun Wukong | Great Sage | **new champion** | Great Sage (Cloud Master; support/damage; INT; storm) |
| Sun Wukong | Infernal | **reskin** | same silhouette and weapon: a cosmetic variant |
| Sun Wukong | Royal | **reskin** | same silhouette and weapon: a cosmetic variant |
| Yin | Crypt Goddess | **monster variant** | hollow / barbed_hunter: crypt huntress |
| Yin | Wind Gauntlet | **reskin** | same silhouette and weapon: a cosmetic variant |
| Riktor | Bronze | **reskin** | same silhouette and weapon: a cosmetic variant |
| Riktor | Bad Santa Belly Riktor | **not usable** | a part of another skin (drone, ultimate gun, VFX shell, antlers), not a body |
| Riktor | Bad Santa | **reskin** | same silhouette and weapon: a cosmetic variant |
| Sparrow | Autumn Fire | **reskin** | same silhouette and weapon: a cosmetic variant |
| Sparrow | Feline Queen | **reskin** | same silhouette and weapon: a cosmetic variant |
| Sparrow | Monarch | **reskin** | same silhouette and weapon: a cosmetic variant |
| Sparrow | Raven | **new champion** | Raven Queen (Shadow Archer; damage; AGI; shadow) |
| Sparrow | Rogue | **reskin** | same silhouette and weapon: a cosmetic variant |
| Sparrow | Tough | **not usable** | its own skeleton (Tough_Skeleton) has no clips in the pack |
| Sparrow | Zechin Huntress | **reskin** | same silhouette and weapon: a cosmetic variant |
| TwinBlast | Halloween | **reskin** | same silhouette and weapon: a cosmetic variant |
| TwinBlast | Wasteland | **reskin** | same silhouette and weapon: a cosmetic variant |
| TwinBlast | Electro | **reskin** | same silhouette and weapon: a cosmetic variant |
| TwinBlast | Action Hero | **reskin** | same silhouette and weapon: a cosmetic variant |
| TwinBlast | Shadow Ops | **reskin** | same silhouette and weapon: a cosmetic variant |
| TwinBlast | Shadow Ops Ult Gun | **not usable** | a part of another skin (drone, ultimate gun, VFX shell, antlers), not a body |
| TwinBlast | Summer Time | **reskin** | same silhouette and weapon: a cosmetic variant |
| TwinBlast | Summer Time Ult Gun | **not usable** | a part of another skin (drone, ultimate gun, VFX shell, antlers), not a body |
| Murdock | Executioner | **new champion** | The Executioner (Headsman; damage; AGI; shadow) |
| Murdock | Magma | **reskin** | same silhouette and weapon: a cosmetic variant |
| Murdock | SF | **reskin** | same silhouette and weapon: a cosmetic variant |
| Murdock | Corrupt | **reskin** | same silhouette and weapon: a cosmetic variant |
| Murdock | Merc | **reskin** | same silhouette and weapon: a cosmetic variant |
| Murdock | Shogun | **reskin** | same silhouette and weapon: a cosmetic variant |
| Murdock | Wasteland | **reskin** | same silhouette and weapon: a cosmetic variant |
| Revenant | Chrono Boss | **new champion** | Chronoboss (Time Enforcer; damage; AGI; arcane) |
| Revenant | Frost King | **new champion** | Frost King (Glacial Tyrant; tank/damage; STR; cold) |
| Revenant | Raven Quill | **monster variant** | fallen_order / fallen_inquisitor_crossbow: plague-doctor gunman: ranged fallen |
| Wraith | Lunar Ops | **reskin** | same silhouette and weapon: a cosmetic variant |
| Wraith | ODGreen | **reskin** | same silhouette and weapon: a cosmetic variant |
| GRIM.exe | Metallic Green | **reskin** | same silhouette and weapon: a cosmetic variant |
| GRIM.exe | Razor Red | **reskin** | same silhouette and weapon: a cosmetic variant |
| GRIM.exe | Wasteland | **monster variant** | ironhide / redmoon_axethrower: scrap raider with a ranged weapon |
| Drongo | Alien Invader | **monster variant** | voidborn / rift_gazer: alien gunner from beyond |
| Drongo | Scavenger | **reskin** | same silhouette and weapon: a cosmetic variant |
| Gideon | Inquisitor | **new champion** | Grand Inquisitor (Zealot Mage; support/healer; INT; holy) |
| Gideon | Mephisto | **new champion** | Mephisto (Hellfire Magus; damage; INT; fire) |
| Gideon | Tough | **reskin** | same silhouette and weapon: a cosmetic variant |
| Gideon | Undertow | **monster variant** | drowned_deep / tidecaller: drowned sorcerer |
| Howitzer | Domed | **monster variant** | stoneborn / crystal_ballista: domed artillery mech |
| Iggy & Scorch | Char Demon | **reskin** | same silhouette and weapon: a cosmetic variant |
| Iggy & Scorch | Fireball | **reskin** | same silhouette and weapon: a cosmetic variant |
| Iggy & Scorch | Jingle Bombs | **reskin** | same silhouette and weapon: a cosmetic variant |
| Iggy & Scorch | Mecha Terror | **new champion** | Mecha Terror (War Engineer; damage/tank; INT; storm) |
| Iggy & Scorch | Phoenix | **new champion** | Phoenix Rider (Firebird Caller; damage/support; INT; holy) |
| Lt. Belica | Biohazard | **reskin** | same silhouette and weapon: a cosmetic variant |
| Lt. Belica | Everfrost | **reskin** | same silhouette and weapon: a cosmetic variant |
| Lt. Belica | Heavy Armor | **new champion** | Siege Lieutenant (Armoured Tactician; tank/support; STR; storm) |
| Lt. Belica | Polar Strike | **reskin** | same silhouette and weapon: a cosmetic variant |
| Lt. Belica | Spider Witch | **new champion** | Spider Witch (Venom Matriarch; damage; INT; poison) |
| Morigesh | Dark Heart | **reskin** | same silhouette and weapon: a cosmetic variant |
| Morigesh | Northern Mystic | **new champion** | Northern Mystic (Frost Shaman; healer/support; INT; cold) |
| Morigesh | Northern Mystic Antlers | **not usable** | a part of another skin (drone, ultimate gun, VFX shell, antlers), not a body |
| Gadget | Guerilla | **reskin** | same silhouette and weapon: a cosmetic variant |
| Gadget | bot Guerilla | **not usable** | a part of another skin (drone, ultimate gun, VFX shell, antlers), not a body |
| Gadget | WInter | **reskin** | same silhouette and weapon: a cosmetic variant |
| Gadget | bot Winter | **not usable** | a part of another skin (drone, ultimate gun, VFX shell, antlers), not a body |
| Gadget | bot Shellshock | **not usable** | a part of another skin (drone, ultimate gun, VFX shell, antlers), not a body |
| Gadget | Shellshock | **reskin** | same silhouette and weapon: a cosmetic variant |
| Zinx | Battle Queen | **reskin** | same silhouette and weapon: a cosmetic variant |
| Zinx | Star Queen | **new champion** | Star Queen (Astral Sovereign; damage/healer; INT; arcane) |
| The Fey | Autumn Keeper | **reskin** | same silhouette and weapon: a cosmetic variant |
| The Fey | Frostbloom | **reskin** | same silhouette and weapon: a cosmetic variant |
| The Fey | Nightshade | **monster variant** | blightwood / rotbloom_shaman: poisoned grove caster |
| The Fey | Opaline | **reskin** | same silhouette and weapon: a cosmetic variant |
| Muriel | Amethyst | **reskin** | same silhouette and weapon: a cosmetic variant |
| Muriel | Black | **reskin** | same silhouette and weapon: a cosmetic variant |
| Muriel | Sepia | **reskin** | same silhouette and weapon: a cosmetic variant |
| Narbash | Bash OLantern | **new champion** | Bash-O-Lantern (Harvest Drummer; damage/support; INT; fire) |
| Narbash | Ginger Jamz | **reskin** | same silhouette and weapon: a cosmetic variant |
| Narbash | Tribal Vibe | **reskin** | same silhouette and weapon: a cosmetic variant |
| Phase | Kitty | **reskin** | same silhouette and weapon: a cosmetic variant |
| Phase | Pandapack | **reskin** | same silhouette and weapon: a cosmetic variant |
| Dekker | Energized | **reskin** | same silhouette and weapon: a cosmetic variant |
| Dekker | Arctic | **reskin** | same silhouette and weapon: a cosmetic variant |
| Dekker | Magma | **reskin** | same silhouette and weapon: a cosmetic variant |
| Dekker | Valentine | **reskin** | same silhouette and weapon: a cosmetic variant |
| ParagonMinions | Minion Lane Melee Dawn | **monster variant** | aetheri / aetheri_phaseblade: lane melee robot (Dawn) |
| ParagonMinions | Minion Lane Melee Dusk | **monster variant** | aetheri / aetheri_phaseblade: lane melee robot (Dusk) |
| ParagonMinions | Minion Lane Ranged Dawn | **monster variant** | aetheri / aetheri_lancer: lane ranged robot (Dawn) |
| ParagonMinions | Minion Lane Ranged Dusk | **monster variant** | aetheri / aetheri_lancer: lane ranged robot (Dusk) |
| ParagonMinions | Minion Lane Siege Dawn | **monster variant** | aetheri / aetheri_warframe: siege robot (Dawn) |
| ParagonMinions | Minion Lane Siege Dusk | **monster variant** | aetheri / aetheri_warframe: siege robot (Dusk) |
| ParagonMinions | Minion Lane Super Dawn | **monster variant** | aetheri / aetheri_bulwark: super minion (Dawn) |
| ParagonMinions | Minion Lane Super Dusk | **monster variant** | aetheri / aetheri_bulwark: super minion (Dusk) |
| ParagonMinions | Buff Black | **monster variant** | voidborn / void_ravager: black jungle buff beast |
| ParagonMinions | Buff Red | **monster variant** | drakkari / drakkari_scalebreaker: red jungle brute |
| ParagonMinions | Buff White | **monster variant** | stoneborn / granite_crusher: white camp golem (minion rig) |
| ParagonMinions | Buff Blue | **monster variant** | drowned_deep / barbspitter: floating blue caster (fly clips) |
| ParagonMinions | Prime Helix | **monster variant** | aetheri / aetheri_colossus: Prime Helix guardian: a boss body |

<!-- /paragon-skins -->

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

The minions pack has no heroes. `creeps` records its inventory. `units` wires the usable bodies in as monster variants
(see the classification above): the lane melee, ranged, siege and super minions in both colours become Aetheri units,
Prime Helix becomes an Aetheri colossus body, and the jungle buffs map by colour (Black to voidborn, Red to drakkari,
White to stoneborn, Blue to drowned_deep). Buff_Green has no attack or death clips, so it is not used.

## Unusable or partial content

See the `problems` list in the JSON and the gallery `CIRE_PARAGON_GALLERY_POSE` lines. The notes below are also
maintained in `Docs/RESUME-paragon-champions.md`.
