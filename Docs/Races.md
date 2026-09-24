# Monster races: the race bible, rank colours and skill progression

monster-races (branch `feat/monster-races`). Nine monster races, each with six unit types and two bosses. This file is
generated with `Content/Data/Races.json` by `Tools/AuthorRaces.py` (edit the script, then run it). The Tripo art
agent reads the **Tripo art prompt** lines; they describe the model to generate for each unit id.

## How it fits together

| Piece | Where |
|---|---|
| Race, unit, boss and skill-pool data | `Content/Data/Races.json` (generated), loaded by `CireRaces` and merged into the NPC archetype database |
| Real art per unit (overlay, by priority) | `Content/Data/RaceMeshes.tripo.json` (tripo-races agent), same shape as `NPCMeshes.tripo.json`'s `archetypes` object: `{ "archetypes": { "<unit id>": { "mesh", "meshScale", "heightCm", "yaw", "variant", "animations": {...}, "alternates": [...] } } }` |
| Fallback bodies | Until a unit has real art, it draws its `fallback` archetype's Tripo body (`NPCMeshes.tripo.json`) reskinned with the race palette through `M_CireMonsterSkin` |
| Rank colours | `Races.json` `ranks` (table below); applied by `CireRaces::ApplySkin` |
| Wave race, rotation and skill schedule | `Content/Data/Waves.json` `campaign` and `skillProgression` (`Docs/Waves.md`) |

Body priority for a unit id: `RaceMeshes.tripo.json` > `NPCMeshes.tripo.json` > its `fallback` archetype's body > the tinted mannequin.

## Slots

Every race fills the same six unit slots and two boss slots, so a wave can be written once by slot and played by any race:
`line` (the basic melee soldier), `bruiser` (heavy melee), `tank`, `caster`, `ranged`, `special` (swarm, skirmisher or support),
`warlord` (a boss that commands: rallies, summons, pulls) and `colossus` (a huge boss that smashes). Wave rows written with a
slot (`"slot": "caster"`) resolve to the wave's race; rows with an explicit `archetype` keep that unit.

## Roles

`bruiser`, `tank`, `caster`, `ranged` as before, plus **`support`** (keeps range like a caster; heals, wards, rallies and
silences; healer role icon) and **`swarm`** (small, fast, fragile melee; comes in numbers; bruiser behaviour, 0.55x health,
0.65x damage, 0.8x size).

## Rank colours

WoW-style quality tiers. The same body, reskinned per race palette and per rank, must read as a different unit:
the race palette recolours the body; the rank recolours the armour (metallic mask of the Tripo PBR set), adds an emissive
trim glow on the armour, a fresnel rim in the rank colour, a small body-wide tint and a size bump. Nameplates colour the
name by rank and the target frame uses the rank colour for its border and header. Neutral packs stay yellow.

| Rank | Colour | Health | Damage | Size | Extra skills | Classification | Look |
|---|---|---|---|---|---|---|---|
| Normal | #d1ccbc `[0.82, 0.8, 0.74]` | x1 | x1 | x1 | +0 | normal | Race base colours only: the plain soldier of the race. |
| Veteran | #1eff19 `[0.12, 1.0, 0.1]` | x1.3 | x1.1 | x1.05 | +0 | normal | WoW uncommon green: armour trim and a green rim. |
| Elite | #0c72ff `[0.05, 0.45, 1.0]` | x1.6 | x1.25 | x1.1 | +1 | elite | WoW rare blue: blue armour, a blue glowing rim, one extra skill. |
| Champion | #b238ff `[0.7, 0.22, 1.0]` | x2.2 | x1.4 | x1.18 | +1 | elite | WoW epic purple: purple armour and trim glow, larger, one extra skill. |
| Warlord | #ff7f05 `[1.0, 0.5, 0.02]` | x1 | x1 | x1 | +2 | boss | WoW legendary orange: every boss. Stats come from the boss archetype; two extra skills. |
| Mythic | #ff1e14 + trim #ffc63f `[1.0, 0.12, 0.08]` | x1.5 | x1.3 | x1.12 | +3 | boss | Red body glow with gold trim: late-cycle bosses and hand-placed terrors; three extra skills. |

Classification mapping: the old `Normal / Elite / Boss` classification maps onto ranks (Normal -> normal, Elite -> elite,
Boss -> warlord), so every existing caller keeps working. Wave rows choose a rank (`"rank": "champion"`); the legacy
`"elite": true` flag is rank elite. Challenge-pack members are elite (tier 1-2) or champion (tier 3+); pack leaders are warlords.
From `campaign.promotions` cycles on, some normal wave units are promoted (veteran, elite, champion) and lane bosses become
mythic from `campaign.mythicBossFromCycle`.

## Skill progression and per-match randomisation

- **No monster skills in the first waves.** Before `skillProgression.firstSkillWave` (global wave number, default 4: waves 1-3
  of cycle 1) every monster, bosses included, uses its basic attack only.
- **Unlocks.** From that wave a normal unit has 1 skill, +1 every `unlockEveryWaves` waves (default 3), up to `maxSkills` (3).
  Ranks add `skillBonus` (elite +1, champion +1, warlord +2, mythic +3), capped by the unit's pool.
- **Stronger versions.** Skill tier I from the first skill wave, II and III every `tierEveryWaves` waves (default 5), up to
  `maxTier` (3). Each tier above I: +20% ability damage, -10% cooldown, +15% control duration (root, silence, slow). The
  ability name gets its tier numeral ("Tidal Slam II").
- **Per-match draw.** Each match picks a seed (`-CireRaceSeed=N` fixes it). For every unit type the pool is shuffled with that
  seed; normal and veteran units only ever use the first `poolDraw` skills of the shuffled pool, higher ranks can reach the
  rest. So a race plays differently match to match, but every skill is on theme. Abilities marked "always in the kit"
  (boss enrages) are not drawn: they join once skills unlock.
- **Replication.** The seed is on `ACireGameState::MonsterSkillSeed`; each monster's active skills, tier, rank, race and
  palette are replicated on `UCireNPCState` (`Loadout`, `SkillTier`, `Rank`, `PaletteIndex`). The target frame's ability
  list shows only the active skills with their tier.

## Engine additions for race skills

Riders on telegraphed skills (applied to champions inside the telegraph when it lands): `root` (cannot move; `npc_rooted`),
`silence` (cannot cast skills; `npc_silenced`), `slow` (the existing slow) and `knockback` (cm, away from the caster or the
circle centre). New kinds: `pull` (a line telegraph to the victim, or the farthest champion; champions in the line are dragged
to the caster) and `summon` (interruptible cast; `count` units of `summon` join the caster's wave, at most 2x count alive).
Every race skill names a themed visual (`buff`, BuffVisuals.json) shown on champions it hits and an audio cue (`cue`).

## Races at a glance

| Race | Player counterpart | line | bruiser | tank | caster | ranged | special | warlord | colossus |
|---|---|---|---|---|---|---|---|---|---|
| The Drowned Deep | scholar, summoner | `abyssal_stalker` | `deepspawn_thrall` | `coralshell_guardian` | `tidecaller` | `barbspitter` | `mind_leech` | `drowned_prophet` | `maw_of_the_deep` |
| The Blightwood | dryad, evergrove_centaur, ether_golem_support | `vinelasher` | `sapling_brute` | `barkhide_warden` | `rotbloom_shaman` | `thornspitter` | `sporeling` | `withered_matron` | `elder_oakheart` |
| The Hollow Legion | knight, ranger, scholar, lancer | `hollow_infantry` | `ironbound_bruiser` | `hollow_shieldbearer` | `blight_caster` | `barbed_hunter` | `grave_hound` | `gravemaw_pack_leader` | `hollow_siegebreaker` |
| The Ironhide Warband | orc_chieftain, troll_berserker_melee, troll_berserker_ranged | `ironhide_grunt` | `redmoon_ravager` | `ironhide_bulwark` | `blood_hexer` | `redmoon_axethrower` | `ironhide_drummer` | `ironhide_warchief` | `ironhide_juggernaut` |
| The Drakkari Brood | drakish_footman | `drakkari_whelpguard` | `drakkari_scalebreaker` | `drakkari_scaleguard` | `drakkari_flamecaller` | `drakkari_wingshot` | `ember_whelp` | `drakkari_broodmother` | `drakkari_ashwing` |
| The Stoneborn | ether_golem_tank, ether_golem_bruiser, ether_golem_support, dwarf_miner | `rune_sentinel` | `granite_crusher` | `bastion_golem` | `deepforge_runesmith` | `crystal_ballista` | `ether_mote` | `stoneborn_forgelord` | `stoneborn_colossus` |
| The Feral Kin | bear, totemic_behemoth, evergrove_centaur | `dire_wolf` | `werebear_mauler` | `tusked_behemoth` | `feral_shaman` | `wild_outrider` | `bristleback` | `feral_ursoth` | `feral_mammoth` |
| The Fallen Order | knight, paladin_righteous, paladin_holy, keeper_of_light | `fallen_squire` | `dread_knight` | `oathbreaker_templar` | `blighted_chaplain` | `fallen_inquisitor_crossbow` | `flagellant` | `fallen_high_inquisitor` | `fallen_crusader` |
| The Voidborn | whisp, keeper_of_light, summoner, wizard | `rift_stalker` | `void_ravager` | `null_warden` | `rift_weaver` | `rift_gazer` | `voidling` | `voidborn_herald` | `voidborn_devourer` |

## Player races (ChampionRoster.json `race`)

| Champion profile | Player race | Monster counterpart |
|---|---|---|
| `knight` | human | hollow, fallen_order |
| `ranger` | human | hollow |
| `scholar` | human | hollow, drowned_deep |
| `lancer` | human | hollow |
| `summoner` | human | voidborn, drowned_deep |
| `bear` | beast | feral_kin |
| `paladin_righteous` | human | fallen_order |
| `paladin_holy` | human | fallen_order |
| `dwarf_miner` | dwarf | stoneborn |
| `ether_golem_tank` | ether-construct | stoneborn |
| `ether_golem_support` | ether-construct | stoneborn, blightwood |
| `ether_golem_bruiser` | ether-construct | stoneborn |
| `orc_chieftain` | orc | ironhide |
| `totemic_behemoth` | beast | feral_kin |
| `drakish_footman` | drakkari | drakkari |
| `wizard` | human | voidborn |
| `troll_berserker_melee` | troll | ironhide |
| `troll_berserker_ranged` | troll | ironhide |
| `dryad` | sylvan | blightwood |
| `whisp` | spirit | voidborn |
| `evergrove_centaur` | centaur | blightwood, feral_kin |
| `keeper_of_light` | human | voidborn, fallen_order |

## The Drowned Deep (`drowned_deep`)

*Squid-faced heralds of a sleeping god, dragged from the trench by the Breach's tide. They drown the land to make it theirs.*

- **Origin (player counterpart):** No champion is drowned-born: the Deep answers the Veil Scholar's forbidden tide-lore and the Rift Summoner's pacts. Its counterpart is humanity's drowned coast. Player races: human; profiles: `scholar`, `summoner`.
- **Palette:** base #195b66 `[0.1, 0.36, 0.4]`, accent #33e5d8 `[0.2, 0.9, 0.85]`, secondary #592672, glow #4cffe5.
- **Reskin sets (palette variants):** Abyssal (base #195b66, accent #33e5d8); Bleached Coral (base #b79e8c, accent #f27266); Crimson Tide (base #72111e, accent #ff4c4c).
- **Footsteps:** `cloth` (per-unit overrides below). **Ambience cue:** `amb.race.drowned`. **Voice cue:** `voice.race.growl`.

| Slot | Unit id | Name | Role | Fallback body | Draw per match |
|---|---|---|---|---|---|
| line | `abyssal_stalker` | Abyssal Stalker | bruiser | `hollow_infantry` | 3 |
| bruiser | `deepspawn_thrall` | Deepspawn Thrall | bruiser | `ironbound_bruiser` | 3 |
| tank | `coralshell_guardian` | Coralshell Guardian | tank | `hollow_shieldbearer` | 3 |
| caster | `tidecaller` | Tidecaller | caster | `blight_caster` | 3 |
| ranged | `barbspitter` | Barbspitter | ranged | `barbed_hunter` | 3 |
| special | `mind_leech` | Mind Leech | support | `blight_caster` | 3 |
| warlord | `drowned_prophet` | The Drowned Prophet | caster boss | `blight_caster` | all |
| colossus | `maw_of_the_deep` | Maw of the Deep | tank boss | `hollow_siegebreaker` | all |

### Abyssal Stalker (`abyssal_stalker`, line)

**Tripo art prompt:** Lean hunched cephalopod humanoid assassin, squid head with a beard of short writhing tentacles, huge black eyes, translucent blue-grey skin with glowing cyan spots, long arms ending in hooked bone claws, tattered kelp sash, barefoot webbed feet, low predatory crouch. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Hooked Claws** (melee): Two quick raking claws at its current target.
- Skill pool:
  - `drowned_stalker_lunge` **Riptide Lunge** (charge, 0.7s cast, 11s cd): Marks a line and darts along it, raking everyone in the path.
  - `drowned_rend` **Riptide Rend** (cone, 0.9s cast, 9s cd, slows 3s): A frontal rake that drenches and slows everyone in the cone for 3s.
  - `drowned_undertow` **Undertow Grab** (pull, 1.0s cast, 16s cd): Marks a line to a distant champion, then hooks and drags them back to the Stalker.
  - `drowned_ink_veil` **Ink Veil** (disengage, 12s cd): Squirts ink and leaps away when a champion closes in.

### Deepspawn Thrall (`deepspawn_thrall`, bruiser)

**Tripo art prompt:** Hulking barnacle-crusted squid-headed brute, one arm a massive crab claw, the other a club of fused coral, thick tentacle beard, slick dark-teal hide with pale belly, rusted anchor chain wrapped around its torso, heavy stooped stance. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Coral Club** (melee): A heavy coral-studded swing at its current target.
- Skill pool:
  - `drowned_tidal_slam` **Tidal Slam** (cone, 1.1s cast, 9s cd, knockback 450cm): Winds up a wave-crash slam that knocks champions in the cone back.
  - `drowned_barnacle_charge` **Barnacle Charge** (charge, 0.9s cast, 14s cd): Marks a line and barrels through it, hitting everyone in the line.
  - `drowned_undertow_stomp` **Crushing Undertow** (selfCircle, 1.2s cast, 12s cd, slows 3s): Marks a ring around itself; the undertow slows everyone inside for 3s.
  - `drowned_brine_frenzy` **Brine Frenzy** (enrage, 1.0s cast): At 35% health it frenzies: +35% damage and faster attacks.

### Coralshell Guardian (`coralshell_guardian`, tank)

**Tripo art prompt:** Squat armoured cephalopod warrior in a carapace of layered pink-white coral plates, mantis-shrimp shell pauldrons, a huge round shield grown from brain coral, short trident, squid face peering from a coral helm, stocky immovable stance. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Coral Bash** (melee): A shield bash at its current target.
- Skill pool:
  - `drowned_abyssal_roar` **Abyssal Roar** (provoke, 0.5s cast, 16s cd): Taunts nearby champions: for 6s they deal 35% less damage to anything but the Guardian.
  - `drowned_coral_bulwark` **Coral Bulwark** (shieldWall, 0.5s cast, 30s cd): Below 50% health it closes its shell: 50% less damage for 6s.
  - `drowned_tidewall_oath` **Tidewall Oath** (guard, 0.4s cast, 14s cd): Guards the most injured nearby ally for 8s, taking 40% of its damage.
  - `drowned_shell_bash` **Shell Crash** (cone, 0.9s cast, 10s cd, knockback 380cm): A short shield-crash cone that knocks champions back.

### Tidecaller (`tidecaller`, caster)

**Tripo art prompt:** Tall robed cephalopod priest, flowing sea-green robes crusted with shells, long drooping tentacle beard glowing at the tips, a conch-and-driftwood staff with a swirling water orb, pearl necklaces, hovering droplets of water around its hands. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Tide Bolt** (projectile): Interruptible cast. Hurls a bolt of seawater in a fixed direction.
- Skill pool:
  - `drowned_drowning_pool` **Drowning Pool** (targetCircle, 1.2s cast, 12s cd): Marks the ground under its target, then floods it with a choking pool for 5s.
  - `drowned_whirlpool` **Whirlpool** (targetCircle, 1.3s cast, 14s cd, roots 2.0s): Marks a circle; the whirlpool roots everyone inside for 2s.
  - `drowned_brine_mending` **Brine Mending** (healAlly, 2.0s cast, 12s cd): Interruptible 2s cast that restores 20% health to the most injured ally below 60%.
  - `drowned_crashing_wave` **Crashing Wave** (cone, 1.2s cast, 11s cd, knockback 500cm): A wave rolls out in a wide cone, knocking champions back.
  - `drowned_riptide` **Riptide** (targetCircle, 1.0s cast, 10s cd, slows 4s): Marks a circle; the riptide slows everyone inside for 4s.

### Barbspitter (`barbspitter`, ranged)

**Tripo art prompt:** Frog-postured cephalopod with a bulbous siphon mouth, quills and barbed spines along its back, mottled olive and violet skin, glowing yellow slit eyes, harpoon of whalebone slung on its back, crouched spitting pose. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Barbed Spine** (projectile): Short aim, then spits a barbed spine in a fixed direction.
- Skill pool:
  - `drowned_ink_spit` **Ink Spit** (targetCircle, 1.0s cast, 11s cd): Spits a blinding ink puddle at its target: poison for 4s.
  - `drowned_spine_volley` **Spine Volley** (targetCircle, 1.0s cast, 10s cd): Marks a circle on its target, then a volley of spines lands there.
  - `drowned_jet_retreat` **Jet Retreat** (disengage, 10s cd): Jets away from melee on a spray of water.
  - `drowned_harpoon_spine` **Harpoon Spine** (pull, 1.1s cast, 16s cd): Marks a line and fires a tethered harpoon that drags the champion to it.

### Mind Leech (`mind_leech`, special)

**Tripo art prompt:** Small floating cephalopod with an oversized exposed brain-like mantle, pulsing violet veins, trailing thin tentacles instead of legs, a single cluster of glowing magenta eyes, faint psychic haze around its head. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Psychic Lash** (projectile): Interruptible cast. A lash of psychic force in a fixed direction.
- Skill pool:
  - `drowned_mind_scream` **Mind Scream** (selfCircle, 1.3s cast, 16s cd, silences 2.5s): Marks a ring around itself; champions inside are silenced for 2.5s.
  - `drowned_dread_whisper` **Dread Whisper** (targetCircle, 1.2s cast, 14s cd, silences 2.0s): Marks a circle on its target; champions inside are silenced for 2s.
  - `drowned_leech_mending` **Leech Mending** (healAlly, 2.0s cast, 12s cd): Interruptible cast: siphons vitality into the most injured ally (18% health).
  - `drowned_abyssal_ward` **Abyssal Ward** (guard, 0.4s cast, 14s cd): Wraps the most injured ally in a psychic ward, taking 40% of its damage for 8s.
  - `drowned_maddening_gaze` **Maddening Gaze** (targetCircle, 1.2s cast, 15s cd, roots 1.5s): Marks a circle; champions inside are paralysed (rooted) for 1.5s.

### The Drowned Prophet (`drowned_prophet`, warlord)

**Tripo art prompt:** Towering gaunt cephalopod prophet in tattered abyssal-blue vestments, crown of black coral and pearls, a mass of long tentacles for a beard and a skirt of dragging tentacles, glowing cyan eyes, a floating barnacled tome and a staff topped with an eldritch eye. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Word of the Deep** (projectile): Interruptible cast. A bolt of drowning scripture.
- Skill pool:
  - `drowned_tidal_prophecy` **Tidal Prophecy** (targetCircle, 1.5s cast, 12s cd): Marks a wide circle; the sea rises there as a drowning pool for 6s.
  - `drowned_call_of_the_deep` **Call of the Deep** (summon, 1.6s cast, 26s cd, summons 2x abyssal_stalker): Interruptible cast: two Abyssal Stalkers crawl out of the tide at its side.
  - `drowned_mind_shatter` **Mind Shatter** (selfCircle, 1.6s cast, 18s cd, silences 3.0s): Marks a large ring; champions inside take damage and are silenced for 3s.
  - `drowned_drowning_grasp` **Drowning Grasp** (pull, 1.2s cast, 16s cd): A tentacle marks a line to the farthest champion and drags them in.
  - `drowned_prophet_madness` **Prophet's Madness** (enrage, 1.0s cast, always in the kit): At 30% health: +40% damage and faster casting until killed.

### Maw of the Deep (`maw_of_the_deep`, colossus)

**Tripo art prompt:** Colossal kraken-crab abomination walking on two thick barnacled legs, a gaping circular maw of needle teeth ringed by long tentacles where its head should be, armoured shell back overgrown with coral and anchor chains, enormous crab claw arm, bioluminescent cyan lures. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Crushing Claw** (melee): A crushing claw blow at its current target.
- Skill pool:
  - `drowned_tentacle_sweep` **Tentacle Sweep** (cone, 1.3s cast, 9s cd, knockback 500cm): Winds up a wide tentacle sweep that knocks champions back. Only the tank should stand in front.
  - `drowned_devour` **Devour** (pull, 1.4s cast, 18s cd): Marks a line to the farthest champion, drags them into the maw and bites.
  - `drowned_tsunami_slam` **Tsunami Slam** (selfCircle, 1.3s cast, 11s cd, slows 3s): Marks a ring around itself, then slams; everyone inside is slowed for 3s.
  - `drowned_ink_tide` **Ink Tide** (targetCircle, 1.3s cast, 14s cd): Vomits a huge ink pool under its target that burns for 6s.
  - `drowned_leviathan_rage` **Leviathan Rage** (enrage, 1.0s cast, always in the kit): At 30% health it enrages: +40% damage and faster attacks until killed.

## The Blightwood (`blightwood`)

*The old forest woke sick. Treants, briars and spore-things march out of the rotting groves to reclaim the fields root by root.*

- **Origin (player counterpart):** The corrupted mirror of the Thornweave Dryad's grove, the Evergrove Centaur's woods and the Verdant ether golem's living stone. Player races: sylvan, fey, construct; profiles: `dryad`, `evergrove_centaur`, `ether_golem_support`.
- **Palette:** base #4c3823 `[0.3, 0.22, 0.14]`, accent #4c8c2d `[0.3, 0.55, 0.18]`, secondary #8c723f, glow #b2ff4c.
- **Reskin sets (palette variants):** Blightgrove (base #4c3823, accent #4c8c2d); Autumn Rot (base #8c3f14, accent #e58c19); Winter Deadwood (base #8c8c99, accent #99d8f2); Bloodroot (base #661414, accent #d83326).
- **Footsteps:** `bark` (per-unit overrides below). **Ambience cue:** `amb.race.grove`. **Voice cue:** `voice.race.growl`.

| Slot | Unit id | Name | Role | Fallback body | Draw per match |
|---|---|---|---|---|---|
| line | `vinelasher` | Vinelasher | bruiser | `hollow_infantry` | 3 |
| bruiser | `sapling_brute` | Sapling Brute | bruiser | `ironbound_bruiser` | 3 |
| tank | `barkhide_warden` | Barkhide Warden | tank | `hollow_shieldbearer` | 3 |
| caster | `rotbloom_shaman` | Rotbloom Shaman | caster | `blight_caster` | 3 |
| ranged | `thornspitter` | Thornspitter | ranged | `barbed_hunter` | 3 |
| special | `sporeling` | Sporeling | swarm | `hollow_infantry` | 3 |
| warlord | `withered_matron` | The Withered Matron | caster boss | `blight_caster` | all |
| colossus | `elder_oakheart` | Elder Oakheart | tank boss | `hollow_siegebreaker` | all |

### Vinelasher (`vinelasher`, line)

**Tripo art prompt:** Wiry humanoid woven from braided thorny vines and dry bark strips, whip-like vine arms trailing to the ground, a knot of glowing green sap for a face, leaves and small red berries sprouting from its shoulders, lithe forward-leaning stance. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Vine Lash** (melee): A stinging whip of thorny vines at its current target.
- Skill pool:
  - `blight_vine_lash` **Thorn Whip** (cone, 0.9s cast, 9s cd, slows 3s): Whips a cone of thorny vines that slows everyone hit for 3s.
  - `blight_strangling_vines` **Strangling Vines** (targetCircle, 1.2s cast, 14s cd, roots 2.0s): Marks a circle; vines burst out and root everyone inside for 2s.
  - `blight_vine_snare` **Vine Snare** (pull, 1.0s cast, 16s cd): Marks a line and lashes a vine around a distant champion, reeling them in.
  - `blight_briar_rush` **Briar Rush** (charge, 0.8s cast, 12s cd): Marks a line and rushes through it, raking everyone with thorns.

### Sapling Brute (`sapling_brute`, bruiser)

**Tripo art prompt:** Young bulky treant brute of pale ash wood, one arm ending in a massive splintered stump club, mossy shoulders with fresh green shoots, cracked bark chest showing amber glowing heartwood, short stubby root legs, aggressive hunched stance. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Stump Club** (melee): A heavy stump-club blow at its current target.
- Skill pool:
  - `blight_stump_slam` **Stump Slam** (cone, 1.1s cast, 9s cd, knockback 420cm): Winds up an overhead slam; champions in the cone are knocked back.
  - `blight_uproot_charge` **Uproot Charge** (charge, 0.9s cast, 14s cd): Tears itself from the ground and charges along a marked line.
  - `blight_splinter_burst` **Splinter Burst** (selfCircle, 1.2s cast, 11s cd): Marks a ring around itself, then bursts into flying splinters.
  - `blight_sap_frenzy` **Sap Frenzy** (enrage, 1.0s cast): At 35% health its sap boils: +35% damage and faster attacks.

### Barkhide Warden (`barkhide_warden`, tank)

**Tripo art prompt:** Thick squat oak treant guardian covered in overlapping shield-like bark plates, a slab of petrified wood as a tower shield on one arm, knotted face with deep-set amber eyes, thick moss mantle, gnarled root feet planted wide. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Bark Bash** (melee): A bark-shield bash at its current target.
- Skill pool:
  - `blight_grove_challenge` **Grove Challenge** (provoke, 0.5s cast, 16s cd): Creaks a challenge: for 6s nearby champions deal 35% less damage to anything but the Warden.
  - `blight_rooted_stance` **Rooted Stance** (shieldWall, 0.5s cast, 30s cd): Below 50% health it roots itself: 50% less damage for 6s.
  - `blight_barkskin_oath` **Barkskin Oath** (guard, 0.4s cast, 14s cd): Grows bark over the most injured ally, taking 40% of its damage for 8s.
  - `blight_root_quake` **Root Quake** (selfCircle, 1.2s cast, 14s cd, roots 1.5s): Marks a ring around itself; roots erupt and hold everyone inside for 1.5s.

### Rotbloom Shaman (`rotbloom_shaman`, caster)

**Tripo art prompt:** Hunched fungal treant shaman, head a huge drooping rotten flower bloom with a glowing sickly-green core, bark robe hung with mushroom shelves, crooked branch staff sprouting pale fungus and dangling seed pods, spores drifting around it. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Rot Bolt** (projectile): Interruptible cast. A bolt of rotting sap in a fixed direction.
- Skill pool:
  - `blight_rot_pool` **Rot Pool** (targetCircle, 1.2s cast, 12s cd): Marks the ground under its target, then leaves a rotting pool for 5s.
  - `blight_entangle` **Entangle** (targetCircle, 1.3s cast, 14s cd, roots 2.0s): Marks a circle; roots burst out and hold everyone inside for 2s.
  - `blight_sap_mending` **Sap Mending** (healAlly, 2.0s cast, 12s cd): Interruptible 2s cast that restores 20% health to the most injured ally below 60%.
  - `blight_wither_curse` **Wither Curse** (targetCircle, 1.1s cast, 11s cd, slows 4s): Marks a circle; champions inside wither and are slowed for 4s.
  - `blight_thorn_burst` **Thorn Burst** (cone, 1.0s cast, 10s cd): Sprays a cone of thorns in front of it.

### Thornspitter (`thornspitter`, ranged)

**Tripo art prompt:** Walking carnivorous pitcher-plant creature on short root legs, a tall veined green-and-crimson pitcher body with a lidded mouth full of thorn teeth, two leafy arms, bundles of long thorns bristling from its back like quivers. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Thorn Spit** (projectile): Short aim, then spits a thorn in a fixed direction.
- Skill pool:
  - `blight_thorn_volley` **Thorn Volley** (targetCircle, 1.0s cast, 10s cd): Marks a circle on its target, then a volley of thorns lands there.
  - `blight_briar_patch` **Briar Patch** (targetCircle, 1.1s cast, 12s cd): Seeds a briar patch under its target: poison and thorns for 5s.
  - `blight_seedpod_mortar` **Seedpod Mortar** (targetCircle, 1.3s cast, 12s cd, knockback 420cm): Lobs an exploding seedpod; champions in the circle are knocked back.
  - `blight_root_hop` **Root Hop** (disengage, 10s cd): Springs away on its roots when a champion reaches melee.

### Sporeling (`sporeling`, special)

**Tripo art prompt:** Small waddling mushroom creature knee-high to a man, a wide spotted red-brown cap with glowing yellow-green gills, stubby root legs and little twig arms, beady glowing eyes under the cap, a trail of drifting spores. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Spore Nip** (melee): A weak nip that leaves spores behind.
- Skill pool:
  - `blight_spore_cloud` **Spore Cloud** (selfCircle, 1.0s cast, 12s cd): Marks a ring and puffs a poison cloud around itself for 4s.
  - `blight_choking_puff` **Choking Puffball** (selfCircle, 1.1s cast, 15s cd, silences 1.5s): Marks a small ring; champions inside choke and are silenced for 1.5s.
  - `blight_mycelial_link` **Mycelial Link** (healAlly, 1.6s cast, 14s cd): Interruptible cast: shares nutrients with the most injured ally (12% health).
  - `blight_spore_scatter` **Spore Scatter** (disengage, 11s cd): Bounces away in a puff of spores.

### The Withered Matron (`withered_matron`, warlord)

**Tripo art prompt:** Tall gaunt corrupted dryad queen, grey-violet dead-bark skin, a crown of blackened thorns and dead lilies, long flowing gown of dried roots and moss that drags like a train, hollow glowing green eyes, clawed branch fingers, drifting withered petals. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Withering Touch** (projectile): Interruptible cast. A bolt of withering sap.
- Skill pool:
  - `blight_thornstorm` **Thornstorm** (targetCircle, 1.4s cast, 11s cd): Marks a wide circle, then a storm of thorns lands there.
  - `blight_strangling_grove` **Strangling Grove** (selfCircle, 1.5s cast, 16s cd, roots 2.0s): Marks a large ring around herself; roots hold everyone inside for 2s.
  - `blight_blight_bloom` **Blight Bloom** (targetCircle, 1.3s cast, 13s cd): Blooms a rotting flower under her target that poisons for 6s.
  - `blight_matron_call` **Matron's Call** (summon, 1.6s cast, 24s cd, summons 3x sporeling): Interruptible cast: three Sporelings sprout at her feet.
  - `blight_rotting_embrace` **Rotting Embrace** (pull, 1.2s cast, 16s cd): Roots mark a line to the farthest champion and drag them to her.
  - `blight_withering_wrath` **Withering Wrath** (enrage, 1.0s cast, always in the kit): At 30% health: +40% damage and faster casting until killed.

### Elder Oakheart (`elder_oakheart`, colossus)

**Tripo art prompt:** Ancient towering oak treant colossus, massive trunk body with deep bark fissures glowing with molten amber heartwood, a long hanging moss beard, antler-like branching crown with a few living leaves, huge root-fist arms, stone-embedded roots for feet, birds' nests and mushrooms in its bark. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Bough Smash** (melee): A crushing bough blow at its current target.
- Skill pool:
  - `blight_root_eruption` **Root Eruption** (targetCircle, 1.3s cast, 12s cd, roots 2.0s): Marks a circle under its target; giant roots erupt and hold everyone for 2s.
  - `blight_oakheart_stomp` **Oakheart Stomp** (selfCircle, 1.3s cast, 10s cd, knockback 520cm): Marks a ring around itself, then stomps, knocking everyone inside back.
  - `blight_crushing_bough` **Crushing Bough** (cone, 1.3s cast, 9s cd): Winds up a wide frontal sweep. Only the tank should stand in front.
  - `blight_awaken_saplings` **Awaken Saplings** (summon, 1.6s cast, 28s cd, summons 2x sapling_brute): Interruptible cast: two Sapling Brutes tear free of the ground.
  - `blight_heartwood_fury` **Heartwood Fury** (enrage, 1.0s cast, always in the kit): At 30% health its heartwood blazes: +40% damage and faster attacks.

## The Hollow Legion (`hollow`)

*The dead of the old kingdom, raised hollow and marched back to the gates they once defended.*

- **Origin (player counterpart):** The undead reflection of the human champions: Iron Warden, Ash Ranger, Veil Scholar, Dusk Lancer. Player races: human; profiles: `knight`, `ranger`, `scholar`, `lancer`.
- **Palette:** base #9e8e70 `[0.62, 0.56, 0.44]`, accent #726b60 `[0.45, 0.42, 0.38]`, secondary #4c4742, glow #8cf2d8.
- **Reskin sets (palette variants):** Gravebound (base #9e8e70, accent #726b60); Ashen (base #4c4c51, accent #b24c26); Frostgrave (base #8ca5bf, accent #66bfff).
- **Footsteps:** `mail` (per-unit overrides below). **Ambience cue:** `amb.race.hollow`. **Voice cue:** `voice.race.growl`.

| Slot | Unit id | Name | Role | Fallback body | Draw per match |
|---|---|---|---|---|---|
| line | `hollow_infantry` | Hollow Infantry | (existing) | `hollow_infantry` | 2 |
| bruiser | `ironbound_bruiser` | Ironbound Bruiser | (existing) | `ironbound_bruiser` | 3 |
| tank | `hollow_shieldbearer` | Hollow Shieldbearer | (existing) | `hollow_shieldbearer` | 3 |
| caster | `blight_caster` | Blight Caster | (existing) | `blight_caster` | 3 |
| ranged | `barbed_hunter` | Barbed Hunter | (existing) | `barbed_hunter` | 2 |
| special | `grave_hound` | Grave Hound | swarm | `hollow_infantry` | 2 |
| warlord | `gravemaw_pack_leader` | Gravemaw, Pack Leader | (existing) | `gravemaw_pack_leader` | all |
| colossus | `hollow_siegebreaker` | Hollow Siegebreaker | (existing) | `hollow_siegebreaker` | all |

### Hollow Infantry (`hollow_infantry`, line)

**Tripo art prompt:** (existing Tripo body) skeletal undead footsoldier in rusted mail, torn tabard, dagger. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Skill pool (added to its existing kit):
  - `hollow_rusted_cleave` **Rusted Cleave** (cone, 1.0s cast, 9s cd): A telegraphed sweep of its rusted blade.
  - `hollow_deathless` **Deathless Resolve** (shieldWall, 0.5s cast, 30s cd): Below 40% health it refuses to fall: 40% less damage for 5s.

### Ironbound Bruiser (`ironbound_bruiser`, bruiser)

**Tripo art prompt:** (existing Tripo body) hulking undead brute in riveted iron plates, war axe. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Skill pool (added to its existing kit):
  - `hollow_iron_stomp` **Iron Stomp** (selfCircle, 1.2s cast, 12s cd, slows 3s): Marks a ring around itself; the stomp slows everyone inside for 3s.
  - `hollow_hook_chain` **Hook Chain** (pull, 1.0s cast, 16s cd): Marks a line and throws a hooked chain that drags a distant champion to it.

### Hollow Shieldbearer (`hollow_shieldbearer`, tank)

**Tripo art prompt:** (existing Tripo body) undead soldier with a battered kite shield and sword. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Skill pool (added to its existing kit):
  - `hollow_shield_rush` **Shield Rush** (charge, 0.9s cast, 14s cd, knockback 350cm): Marks a short line and rushes behind its shield, knocking champions aside.

### Blight Caster (`blight_caster`, caster)

**Tripo art prompt:** (existing Tripo body) robed undead warlock with a bone staff. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Skill pool (added to its existing kit):
  - `hollow_grave_silence` **Grave Silence** (targetCircle, 1.2s cast, 14s cd, silences 2.0s): Marks a circle; champions inside are silenced for 2s.
  - `hollow_withering_hex` **Withering Hex** (targetCircle, 1.0s cast, 11s cd, slows 4s): Marks a circle; champions inside are slowed for 4s.

### Barbed Hunter (`barbed_hunter`, ranged)

**Tripo art prompt:** (existing Tripo body) undead archer in leather with a bow or crossbow. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Skill pool (added to its existing kit):
  - `hollow_pinning_net` **Pinning Net** (targetCircle, 1.0s cast, 14s cd, roots 1.5s): Marks a small circle on its target, then a weighted net roots everyone inside for 1.5s.

### Grave Hound (`grave_hound`, special)

**Tripo art prompt:** Emaciated undead hound the size of a wolf, exposed ribs and spine through torn grey hide, glowing pale-green eyes, iron collar with a broken chain, long skeletal jaw, running on four legs (fallback: hunched two-legged ghoul). Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Grave Bite** (melee): A tearing bite at its current target.
- Skill pool:
  - `hollow_pounce` **Pounce** (charge, 0.7s cast, 11s cd): Marks a short line and pounces along it.
  - `hollow_rending_bite` **Rending Bite** (cone, 0.8s cast, 9s cd, slows 3s): A telegraphed bite that slows the victims for 3s.
  - `hollow_pack_howl` **Pack Howl** (rally, 1.2s cast, 20s cd): Howls: nearby monsters gain +20% damage and attack speed for 8s.

### Gravemaw, Pack Leader (`gravemaw_pack_leader`, warlord)

**Tripo art prompt:** (existing Tripo body) blood-red undead war chief with a greataxe and a totem on its back. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Skill pool (added to its existing kit):
  - `hollow_bone_hook` **Bone Hook** (pull, 1.2s cast, 18s cd): Marks a line to the farthest champion and hooks them into the pack.

### Hollow Siegebreaker (`hollow_siegebreaker`, colossus)

**Tripo art prompt:** (existing Tripo body) giant undead siege warrior with a fused maul. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Skill pool (added to its existing kit):
  - `hollow_rubble_toss` **Rubble Toss** (targetCircle, 1.4s cast, 13s cd, knockback 400cm): Hurls a chunk of wall at its target; champions in the circle are knocked back.

## The Ironhide Warband (`ironhide`)

*Orc clans and red-moon trolls who broke their blood-oaths to raid the Breach. They fight for plunder and the drums never stop.*

- **Origin (player counterpart):** The oath-breaking clans of the Blood-Oath Chieftain and the Red-Moon Berserkers. Player races: orc, troll; profiles: `orc_chieftain`, `troll_berserker_melee`, `troll_berserker_ranged`.
- **Palette:** base #4c662d `[0.3, 0.4, 0.18]`, accent #262321 `[0.15, 0.14, 0.13]`, secondary #8c140c, glow #ff4c19.
- **Reskin sets (palette variants):** Blood Clan (base #4c662d, accent #8c140c); Ashskin (base #595451, accent #e57219); Red Moon (base #597f8c, accent #cc1919).
- **Footsteps:** `plate_heavy` (per-unit overrides below). **Ambience cue:** `amb.race.war`. **Voice cue:** `voice.race.growl`.

| Slot | Unit id | Name | Role | Fallback body | Draw per match |
|---|---|---|---|---|---|
| line | `ironhide_grunt` | Ironhide Grunt | bruiser | `hollow_infantry` | 3 |
| bruiser | `redmoon_ravager` | Red-Moon Ravager | bruiser | `ironbound_bruiser` | 3 |
| tank | `ironhide_bulwark` | Ironhide Bulwark | tank | `hollow_shieldbearer` | 3 |
| caster | `blood_hexer` | Blood Hexer | caster | `blight_caster` | 3 |
| ranged | `redmoon_axethrower` | Red-Moon Axe-Thrower | ranged | `barbed_hunter` | 3 |
| special | `ironhide_drummer` | Warband Drummer | support | `blight_caster` | 2 |
| warlord | `ironhide_warchief` | Skullsplitter Warchief | bruiser boss | `gravemaw_pack_leader` | all |
| colossus | `ironhide_juggernaut` | Mountain Troll Juggernaut | tank boss | `hollow_siegebreaker` | all |

### Ironhide Grunt (`ironhide_grunt`, line)

**Tripo art prompt:** Muscular green-skinned orc grunt with jutting tusks, topknot, crude black-iron shoulder plate on one side, fur loincloth and leather straps, notched cleaver axe, war paint in red handprints. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Cleaver** (melee): A brutal cleaver chop at its current target.
- Skill pool:
  - `ironhide_cleave` **Cleave** (cone, 1.0s cast, 9s cd): Winds up a wide cleave in front of it.
  - `ironhide_war_charge` **War Charge** (charge, 0.9s cast, 13s cd): Marks a line and charges through it, hitting everyone in the line.
  - `ironhide_bloodlust` **Bloodlust** (rally, 1.2s cast, 20s cd): Bellows: nearby monsters gain +20% damage and attack speed for 8s.
  - `ironhide_gut_punch` **Gut Punch** (cone, 0.8s cast, 10s cd, knockback 380cm): A short shoulder-charge cone that knocks champions back.

### Red-Moon Ravager (`redmoon_ravager`, bruiser)

**Tripo art prompt:** Tall lean blue-grey troll berserker with long arms, huge tusks and a red mohawk, ritual bone jewellery and red cloth wraps, a notched axe in each hand, scars and tribal tattoos, hunched loping stance. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Twin Axes** (melee): A double axe chop at its current target.
- Skill pool:
  - `ironhide_whirlwind` **Whirlwind** (selfCircle, 1.2s cast, 10s cd): Marks a ring and spins with both axes, hitting everyone around it.
  - `ironhide_berserk` **Berserk** (enrage, 1.0s cast): At 40% health it goes berserk: +40% damage and faster attacks.
  - `ironhide_leaping_axe` **Leaping Axes** (charge, 0.9s cast, 13s cd): Marks a line and leaps along it, axes first.
  - `ironhide_rend` **Rend** (cone, 1.0s cast, 9s cd, slows 3s): A telegraphed rending chop that slows everyone hit for 3s.

### Ironhide Bulwark (`ironhide_bulwark`, tank)

**Tripo art prompt:** Massive orc in layered black-iron plate, a tower shield made from a spiked iron door, heavy chain and meat hook at the belt, horned helm with tusks protruding through the visor, broad planted stance. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Door Bash** (melee): A shield bash with its iron door.
- Skill pool:
  - `ironhide_war_cry` **Challenging Shout** (provoke, 0.5s cast, 16s cd): Taunts nearby champions: for 6s they deal 35% less damage to anything but the Bulwark.
  - `ironhide_shield_wall` **Iron Door** (shieldWall, 0.5s cast, 30s cd): Below 50% health it hides behind its door: 50% less damage for 6s.
  - `ironhide_bodyguard` **Bodyguard** (guard, 0.4s cast, 14s cd): Guards the most injured nearby ally for 8s, taking 40% of its damage.
  - `ironhide_chain_hook` **Meat Hook** (pull, 1.0s cast, 16s cd): Marks a line and throws a chained hook that drags a champion in.

### Blood Hexer (`blood_hexer`, caster)

**Tripo art prompt:** Wiry troll witch-doctor with a bone mask over its face, dreadlocks woven with feathers and small skulls, necklaces of teeth, shrunken-head totems on a crooked staff, red and black face paint, hunched shuffling pose. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Hex Bolt** (projectile): Interruptible cast. A crackling bolt of blood magic.
- Skill pool:
  - `ironhide_blood_hex` **Blood Hex** (targetCircle, 1.2s cast, 14s cd, silences 2.0s): Marks a circle; champions inside are silenced for 2s.
  - `ironhide_spirit_mend` **Spirit Mend** (healAlly, 2.0s cast, 12s cd): Interruptible 2s cast that restores 20% health to the most injured ally below 60%.
  - `ironhide_bog_curse` **Bog Curse** (targetCircle, 1.2s cast, 12s cd): Curses the ground under its target: poison for 5s.
  - `ironhide_frenzy_ritual` **Frenzy Ritual** (rally, 1.2s cast, 20s cd): A blood ritual: nearby monsters gain +25% damage and attack speed for 8s.

### Red-Moon Axe-Thrower (`redmoon_axethrower`, ranged)

**Tripo art prompt:** Lean troll with a bandolier of throwing axes across the chest, red cloth wraps, bone earrings, one axe raised to throw, long legs in a wide throwing stance. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Thrown Axe** (projectile): Short aim, then hurls an axe in a fixed direction.
- Skill pool:
  - `ironhide_axe_barrage` **Axe Barrage** (targetCircle, 1.0s cast, 10s cd): Marks a circle on its target, then a barrage of axes lands there.
  - `ironhide_hamstring_axe` **Hamstring Axe** (targetCircle, 0.9s cast, 14s cd, roots 1.5s): Marks a small circle; the axe hamstrings and roots everyone inside for 1.5s.
  - `ironhide_bounding_retreat` **Bounding Retreat** (disengage, 10s cd): Leaps away when a champion reaches melee range.
  - `ironhide_spinning_axe` **Spinning Axe** (cone, 1.0s cast, 11s cd): Throws a spinning axe in a narrow long cone.

### Warband Drummer (`ironhide_drummer`, special)

**Tripo art prompt:** Stocky orc carrying an enormous skin war drum strapped to its back and a pair of bone mallets, rows of trophies on the drum rim, fur cloak, open roaring mouth. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Thrown Stone** (projectile): A thrown stone in a fixed direction.
- Skill pool:
  - `ironhide_war_drums` **War Drums** (rally, 1.2s cast, 20s cd): Pounds the drums: nearby monsters gain +25% damage and attack speed for 10s.
  - `ironhide_battle_hymn` **Battle Hymn** (healAlly, 2.0s cast, 12s cd): Interruptible cast: rallies the most injured ally (15% health).
  - `ironhide_deafening_boom` **Deafening Boom** (selfCircle, 1.2s cast, 16s cd, silences 1.5s, knockback 380cm): Marks a ring; the drum's boom knocks back and silences everyone inside for 1.5s.

### Skullsplitter Warchief (`ironhide_warchief`, warlord)

**Tripo art prompt:** Huge scarred orc warchief in black-iron and bone plate, a cloak of wolf pelts, a banner pole of skulls strapped to his back, a colossal two-handed cleaver, one tusk capped in gold, glowing red war paint. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Skullsplitter** (melee): A savage cleaver blow at its current target.
- Skill pool:
  - `ironhide_warchief_roar` **Warchief's Roar** (rally, 1.5s cast, 20s cd): 1.5s roar (gold ring): monsters within 12m gain +25% damage and attack speed for 10s.
  - `ironhide_skull_cleave` **Skull Cleave** (cone, 1.2s cast, 9s cd): Winds up a 120-degree cleave for heavy damage. Only the tank should stand in front.
  - `ironhide_warpath` **Warpath** (charge, 1.0s cast, 14s cd): Marks a line to the farthest champion within 12m and charges through it.
  - `ironhide_call_the_clans` **Call the Clans** (summon, 1.6s cast, 26s cd, summons 2x ironhide_grunt): Interruptible cast: two Ironhide Grunts answer his horn.
  - `ironhide_blood_fury` **Blood Fury** (enrage, 1.0s cast, always in the kit): At 30% health: +50% damage, faster attacks and movement until killed.

### Mountain Troll Juggernaut (`ironhide_juggernaut`, colossus)

**Tripo art prompt:** Colossal mountain troll with stone-grey hide and moss, iron plates riveted into its skin, a whole tree trunk bound with iron bands as a club, broken chains dangling from its wrists, tiny eyes under a heavy brow. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Trunk Club** (melee): A crushing tree-trunk blow at its current target.
- Skill pool:
  - `ironhide_earthsplitter` **Earthsplitter** (selfCircle, 1.3s cast, 10s cd, knockback 520cm): Marks a ring around itself, then slams, knocking everyone inside back.
  - `ironhide_trunk_sweep` **Trunk Sweep** (cone, 1.3s cast, 9s cd): Winds up a wide frontal sweep. Only the tank should stand in front.
  - `ironhide_boulder_hurl` **Boulder Hurl** (targetCircle, 1.4s cast, 12s cd, slows 3s): Hurls a boulder at its target; the circle is crushed and slowed for 3s.
  - `ironhide_thick_hide` **Troll Hide** (shieldWall, 0.5s cast, 30s cd): Below 50% health its hide hardens: 40% less damage for 6s.
  - `ironhide_juggernaut_rage` **Juggernaut Rage** (enrage, 1.0s cast, always in the kit): At 30% health it enrages: +40% damage and faster attacks.

## The Drakkari Brood (`drakkari`)

*Dragon-blooded broods who never kept the Dragon Pact. They burn what their cousins swore to guard.*

- **Origin (player counterpart):** The pact-breaking brood of the Drakish Footman's dragon line. Player races: drakkari; profiles: `drakish_footman`.
- **Palette:** base #8c2814 `[0.55, 0.16, 0.08]`, accent #cc9933 `[0.8, 0.6, 0.2]`, secondary #331e19, glow #ff8c19.
- **Reskin sets (palette variants):** Ember Brood (base #8c2814, accent #cc9933); Obsidian Brood (base #231e21, accent #ff660c); Verdigris Brood (base #2d7259, accent #d8bf4c).
- **Footsteps:** `mail` (per-unit overrides below). **Ambience cue:** `amb.race.war`. **Voice cue:** `voice.race.growl`.

| Slot | Unit id | Name | Role | Fallback body | Draw per match |
|---|---|---|---|---|---|
| line | `drakkari_whelpguard` | Drakkari Whelpguard | bruiser | `hollow_infantry` | 2 |
| bruiser | `drakkari_scalebreaker` | Scalebreaker | bruiser | `ironbound_bruiser` | 3 |
| tank | `drakkari_scaleguard` | Brood Scaleguard | tank | `hollow_shieldbearer` | 3 |
| caster | `drakkari_flamecaller` | Flamecaller | caster | `blight_caster` | 3 |
| ranged | `drakkari_wingshot` | Drakkari Wingshot | ranged | `barbed_hunter` | 3 |
| special | `ember_whelp` | Ember Whelp | swarm | `hollow_infantry` | 2 |
| warlord | `drakkari_broodmother` | Broodmother Vyrsha | caster boss | `blight_caster` | all |
| colossus | `drakkari_ashwing` | Ashwing the Scorcher | tank boss | `hollow_siegebreaker` | all |

### Drakkari Whelpguard (`drakkari_whelpguard`, line)

**Tripo art prompt:** Young lizard-headed dragonkin footsoldier with red-orange scales, small horns, a short spear and a scale-mail skirt, thin tail, ember glow in its throat, upright soldier stance. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Spear Thrust** (melee): A quick spear thrust at its current target.
- Skill pool:
  - `drakkari_spear_lunge` **Spear Lunge** (charge, 0.8s cast, 12s cd): Marks a line and lunges along it, spear first.
  - `drakkari_tail_sweep` **Tail Sweep** (cone, 0.9s cast, 10s cd, knockback 380cm): Sweeps its tail through a wide cone, knocking champions back.
  - `drakkari_ember_breath` **Ember Breath** (cone, 1.0s cast, 10s cd): Breathes a short cone of embers.

### Scalebreaker (`drakkari_scalebreaker`, bruiser)

**Tripo art prompt:** Brawny drakonid bruiser with small leathery wings folded on its back, thick red scales and gold horn-tips, a heavy stone-headed hammer, bone and gold armbands, broad chest glowing with inner fire through the scale seams. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Scale Hammer** (melee): A heavy hammer blow at its current target.
- Skill pool:
  - `drakkari_wing_buffet` **Wing Buffet** (selfCircle, 1.2s cast, 12s cd, knockback 480cm): Marks a ring and beats its wings, knocking everyone inside back.
  - `drakkari_dive_charge` **Dive Charge** (charge, 0.9s cast, 14s cd): Marks a line and dives along it on half-open wings.
  - `drakkari_molten_slam` **Molten Slam** (cone, 1.1s cast, 9s cd): Winds up a slam that splashes molten rock in a cone.
  - `drakkari_draconic_fury` **Draconic Fury** (enrage, 1.0s cast): At 35% health: +35% damage and faster attacks.

### Brood Scaleguard (`drakkari_scaleguard`, tank)

**Tripo art prompt:** Heavily armoured dragonkin with overlapping bronze scale plates, a large shield shaped like a dragon's wing, a horned helm with a crest, short sword, thick tail. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Wing-Shield Bash** (melee): A shield bash at its current target.
- Skill pool:
  - `drakkari_dragon_roar` **Dragon Roar** (provoke, 0.5s cast, 16s cd): Taunts nearby champions: for 6s they deal 35% less damage to anything but the Scaleguard.
  - `drakkari_scale_ward` **Scale Ward** (shieldWall, 0.5s cast, 30s cd): Below 50% health its scales harden: 50% less damage for 6s.
  - `drakkari_brood_oath` **Brood Oath** (guard, 0.4s cast, 14s cd): Guards the most injured nearby ally for 8s, taking 40% of its damage.
  - `drakkari_shield_charge` **Shield Charge** (charge, 0.9s cast, 14s cd, knockback 350cm): Marks a short line and charges behind its shield, knocking champions aside.

### Flamecaller (`drakkari_flamecaller`, caster)

**Tripo art prompt:** Slender horned dragonkin pyromancer in charred crimson robes with gold trim, a staff topped with a caged burning coal, flames licking from its open jaws and fingertips, long tail wrapped in cloth. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Fire Bolt** (projectile): Interruptible cast. Hurls a fire bolt in a fixed direction.
- Skill pool:
  - `drakkari_flame_pillar` **Flame Pillar** (targetCircle, 1.2s cast, 10s cd): Marks a circle under its target, then a pillar of fire erupts there.
  - `drakkari_ember_pool` **Ember Pool** (targetCircle, 1.2s cast, 12s cd): Leaves a pool of burning embers under its target for 5s.
  - `drakkari_cauterize` **Cauterize** (healAlly, 2.0s cast, 12s cd): Interruptible 2s cast that restores 20% health to the most injured ally below 60%.
  - `drakkari_searing_breath` **Searing Breath** (cone, 1.2s cast, 11s cd): Breathes a long cone of fire.

### Drakkari Wingshot (`drakkari_wingshot`, ranged)

**Tripo art prompt:** Agile winged dragonkin archer with folded bat-like wings, light bronze scale vest, a heavy crossbow with a dragon-head prow, quiver of glowing ember bolts, crest of small horns. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Ember Bolt** (projectile): Short aim, then fires an ember bolt in a fixed direction.
- Skill pool:
  - `drakkari_fire_rain` **Fire Rain** (targetCircle, 1.0s cast, 10s cd): Marks a circle on its target, then burning bolts rain there.
  - `drakkari_wing_leap` **Wing Leap** (disengage, 10s cd): Leaps away on its wings when a champion reaches melee.
  - `drakkari_incendiary_bolt` **Incendiary Bolt** (targetCircle, 1.0s cast, 12s cd): An incendiary bolt leaves burning ground under its target for 4s.
  - `drakkari_pinning_bolt` **Pinning Bolt** (targetCircle, 0.9s cast, 14s cd, roots 1.5s): Marks a small circle; the bolt pins everyone inside for 1.5s.

### Ember Whelp (`ember_whelp`, special)

**Tripo art prompt:** Small dog-sized dragon whelp walking upright on hind legs, oversized head with stubby horns, little flapping wings, bright orange scales with a glowing belly, smoke puffing from its nostrils. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Whelp Bite** (melee): A snapping bite at its current target.
- Skill pool:
  - `drakkari_whelp_dive` **Whelp Dive** (charge, 0.7s cast, 11s cd): Marks a short line and dives along it.
  - `drakkari_cinder_spit` **Cinder Spit** (cone, 0.8s cast, 9s cd): Spits a small cone of cinders.
  - `drakkari_whelp_frenzy` **Whelp Frenzy** (enrage, 1.0s cast): At 50% health it frenzies: +30% damage and faster attacks.

### Broodmother Vyrsha (`drakkari_broodmother`, warlord)

**Tripo art prompt:** Tall regal dragonkin matriarch with wide ragged wings half-spread, a crown of curling golden horns, flowing robes of scorched silk over scale armour, a staff with a dragon egg glowing inside a gold cage, fire smouldering in her mouth. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Brood Flame** (projectile): Interruptible cast. A gout of dragonfire in a fixed direction.
- Skill pool:
  - `drakkari_hatch_the_brood` **Hatch the Brood** (summon, 1.6s cast, 24s cd, summons 3x ember_whelp): Interruptible cast: three Ember Whelps hatch at her feet.
  - `drakkari_flame_nova` **Flame Nova** (selfCircle, 1.4s cast, 12s cd): Marks a large ring around her, then a nova of fire bursts out.
  - `drakkari_wing_gust` **Wing Gust** (selfCircle, 1.1s cast, 14s cd, knockback 600cm): Marks a ring; a gust of her wings knocks everyone inside back.
  - `drakkari_magma_rain` **Magma Rain** (targetCircle, 1.4s cast, 13s cd): Marks a wide circle; magma rains and burns there for 6s.
  - `drakkari_broodmother_wrath` **Broodmother's Wrath** (enrage, 1.0s cast, always in the kit): At 30% health: +40% damage and faster casting until killed.

### Ashwing the Scorcher (`drakkari_ashwing`, colossus)

**Tripo art prompt:** Massive bipedal fire drake standing upright, huge folded wings like a cloak, thick black-and-crimson scales with molten cracks, a long horned head with smoke pouring from its jaws, heavy clawed arms, tail dragging behind. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Rending Claws** (melee): A rending claw strike at its current target.
- Skill pool:
  - `drakkari_inferno_breath` **Inferno Breath** (cone, 1.4s cast, 10s cd): Winds up a long cone of fire breath. Get out of the cone.
  - `drakkari_ashwing_tail` **Tail Lash** (cone, 1.1s cast, 11s cd, knockback 500cm): Lashes its tail through a wide arc, knocking champions back.
  - `drakkari_ash_fall` **Ash Fall** (targetCircle, 1.3s cast, 13s cd): Marks a wide circle under its target; burning ash falls there for 6s.
  - `drakkari_ashwing_stomp` **Scorching Stomp** (selfCircle, 1.3s cast, 12s cd, knockback 450cm): Marks a ring and stomps, knocking everyone inside back.
  - `drakkari_ashwing_fury` **Molten Fury** (enrage, 1.0s cast, always in the kit): At 30% health its scales melt open: +40% damage and faster attacks.

## The Stoneborn (`stoneborn`)

*Runic golems and constructs of a lost dwarven hold, woken by the Breach's ether and bound to no master.*

- **Origin (player counterpart):** The masterless cousins of the Ether Golem and the constructs of the Deepdelve Miner's forgotten forges. Player races: construct, dwarf; profiles: `ether_golem_tank`, `ether_golem_bruiser`, `ether_golem_support`, `dwarf_miner`.
- **Palette:** base #66666b `[0.4, 0.4, 0.42]`, accent #3f7fe5 `[0.25, 0.5, 0.9]`, secondary #4c3f33, glow #59bfff.
- **Reskin sets (palette variants):** Runic Granite (base #66666b, accent #3f7fe5); Felforged (base #2d2826, accent #59f233); Sunstone (base #a58c66, accent #ffb233).
- **Footsteps:** `golem` (per-unit overrides below). **Ambience cue:** `amb.race.war`. **Voice cue:** `voice.race.growl`.

| Slot | Unit id | Name | Role | Fallback body | Draw per match |
|---|---|---|---|---|---|
| line | `rune_sentinel` | Rune Sentinel | bruiser | `hollow_infantry` | 2 |
| bruiser | `granite_crusher` | Granite Crusher | bruiser | `ironbound_bruiser` | 3 |
| tank | `bastion_golem` | Bastion Golem | tank | `hollow_shieldbearer` | 3 |
| caster | `deepforge_runesmith` | Deepforge Runesmith | caster | `blight_caster` | 3 |
| ranged | `crystal_ballista` | Crystal Ballista | ranged | `barbed_hunter` | 3 |
| special | `ether_mote` | Ether Mote | support | `blight_caster` | 3 |
| warlord | `stoneborn_forgelord` | Forgelord Thrainor | bruiser boss | `gravemaw_pack_leader` | all |
| colossus | `stoneborn_colossus` | Primeval Colossus | tank boss | `hollow_siegebreaker` | all |

### Rune Sentinel (`rune_sentinel`, line)

**Tripo art prompt:** Man-sized stone construct soldier of carved grey granite blocks, glowing blue rune lines along its limbs, one forearm ending in a stone blade, a faceless helm-shaped head with a single rune slit, bronze joint rings. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Stone Blade** (melee): A stone blade strike at its current target.
- Skill pool:
  - `stoneborn_rune_strike` **Rune Strike** (cone, 1.0s cast, 9s cd): A telegraphed sweep of its rune-lit blade.
  - `stoneborn_rune_rush` **Rune Rush** (charge, 0.9s cast, 13s cd): Marks a line and slides along it on glowing runes.
  - `stoneborn_static_pulse` **Static Pulse** (selfCircle, 1.1s cast, 15s cd, silences 1.5s): Marks a ring; a pulse of ether silences everyone inside for 1.5s.

### Granite Crusher (`granite_crusher`, bruiser)

**Tripo art prompt:** Hulking boulder golem with oversized fists of rough granite, a small head sunk between huge rock shoulders, glowing blue cracks across its chest, moss in the crevices, heavy knuckle-dragging stance. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Stone Fist** (melee): A crushing stone fist at its current target.
- Skill pool:
  - `stoneborn_boulder_charge` **Boulder Charge** (charge, 0.9s cast, 14s cd, knockback 350cm): Marks a line and rolls through it, knocking champions aside.
  - `stoneborn_ground_pound` **Ground Pound** (selfCircle, 1.2s cast, 11s cd, slows 3s): Marks a ring and pounds the ground, slowing everyone inside for 3s.
  - `stoneborn_granite_smash` **Granite Smash** (cone, 1.1s cast, 9s cd): Winds up a two-fisted smash in a cone.
  - `stoneborn_overload` **Ether Overload** (enrage, 1.0s cast): At 35% health its core overloads: +35% damage and faster attacks.

### Bastion Golem (`bastion_golem`, tank)

**Tripo art prompt:** Tall monolithic golem shaped like a walking fortress wall, a slab shield carved with a dwarven rune on one arm, crenellated shoulders, deep blue ether core visible through a grille in its chest, thick pillar legs. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Slab Bash** (melee): A slab-shield bash at its current target.
- Skill pool:
  - `stoneborn_runic_taunt` **Runic Challenge** (provoke, 0.5s cast, 16s cd): A rune flares: for 6s nearby champions deal 35% less damage to anything but the Bastion.
  - `stoneborn_stoneskin` **Stoneskin** (shieldWall, 0.5s cast, 30s cd): Below 50% health it petrifies: 50% less damage for 6s.
  - `stoneborn_warding_link` **Warding Link** (guard, 0.4s cast, 14s cd): Links a rune to the most injured ally, taking 40% of its damage for 8s.
  - `stoneborn_tremor` **Tremor** (selfCircle, 1.2s cast, 14s cd, roots 1.5s): Marks a ring; the tremor roots everyone inside for 1.5s.

### Deepforge Runesmith (`deepforge_runesmith`, caster)

**Tripo art prompt:** Stout dwarf-shaped construct of bronze and dark iron with a stone beard carved in braids, a forge-hammer staff crackling with blue runic lightning, glowing furnace belly, rune-etched pauldrons. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Rune Bolt** (projectile): Interruptible cast. A bolt of runic lightning.
- Skill pool:
  - `stoneborn_rune_mine` **Rune Mine** (targetCircle, 2.0s cast, 12s cd): Etches a slow rune under its target; it detonates after a long fuse.
  - `stoneborn_forge_mending` **Forge Mending** (healAlly, 2.0s cast, 12s cd): Interruptible 2s cast that restores 20% health to the most injured ally below 60%.
  - `stoneborn_arc_lattice` **Arc Lattice** (targetCircle, 1.2s cast, 14s cd, silences 2.0s): Marks a circle; a lattice of lightning silences everyone inside for 2s.
  - `stoneborn_molten_slag` **Molten Slag** (targetCircle, 1.2s cast, 12s cd): Pours molten slag under its target that burns for 5s.

### Crystal Ballista (`crystal_ballista`, ranged)

**Tripo art prompt:** Low spider-legged stone construct carrying a crystal launcher on its back like a crossbow, glowing blue crystal shards loaded in a rack, a lens-eye on its front, bronze gears on the leg joints. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Crystal Shard** (projectile): Short aim, then fires a crystal shard in a fixed direction.
- Skill pool:
  - `stoneborn_shard_volley` **Shard Volley** (targetCircle, 1.0s cast, 10s cd): Marks a circle on its target, then a volley of shards lands there.
  - `stoneborn_pinning_shard` **Pinning Shard** (targetCircle, 0.9s cast, 14s cd, roots 1.5s): Marks a small circle; crystals pin everyone inside for 1.5s.
  - `stoneborn_recoil_jump` **Recoil Jump** (disengage, 10s cd): Fires into the ground and recoils away from melee.
  - `stoneborn_piercing_beam` **Piercing Beam** (cone, 1.3s cast, 12s cd): Charges its lens, then fires a long narrow beam.

### Ether Mote (`ether_mote`, special)

**Tripo art prompt:** Floating construct of a glowing blue ether crystal core held in a cage of orbiting stone rings, a few small stone plates hovering around it, trailing sparks, no legs. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Ether Spark** (projectile): Interruptible cast. A spark of raw ether.
- Skill pool:
  - `stoneborn_ether_repair` **Ether Repair** (healAlly, 2.0s cast, 12s cd): Interruptible cast: repairs the most injured ally (20% health).
  - `stoneborn_shield_matrix` **Shield Matrix** (guard, 0.4s cast, 14s cd): Projects a matrix over the most injured ally, taking 40% of its damage for 8s.
  - `stoneborn_overcharge` **Overcharge** (rally, 1.2s cast, 20s cd): Overcharges nearby monsters: +25% damage and attack speed for 8s.
  - `stoneborn_ether_burst` **Ether Burst** (selfCircle, 1.1s cast, 15s cd, silences 1.5s): Marks a ring and bursts, silencing everyone inside for 1.5s.

### Forgelord Thrainor (`stoneborn_forgelord`, warlord)

**Tripo art prompt:** Towering dwarven forge-king construct of blackened iron and bronze, a braided beard of chain and stone, a crown of anvil horns, a massive forge hammer glowing white-hot, furnace chest with a roaring blue fire, runic pauldrons trailing sparks. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Forge Hammer** (melee): A white-hot hammer blow at its current target.
- Skill pool:
  - `stoneborn_forge_sentinels` **Forge Sentinels** (summon, 1.6s cast, 26s cd, summons 2x rune_sentinel): Interruptible cast: two Rune Sentinels step out of the forge.
  - `stoneborn_slag_eruption` **Slag Eruption** (targetCircle, 1.4s cast, 13s cd): Marks a wide circle; molten slag erupts and burns for 6s.
  - `stoneborn_anvil_cleave` **Anvil Cleave** (cone, 1.2s cast, 9s cd): Winds up a heavy 110-degree cleave. Only the tank should stand in front.
  - `stoneborn_runic_shockwave` **Runic Shockwave** (selfCircle, 1.4s cast, 15s cd, silences 2.0s, knockback 450cm): Marks a ring; a runic shockwave knocks back and silences everyone inside for 2s.
  - `stoneborn_forge_fury` **Forge Fury** (enrage, 1.0s cast, always in the kit): At 30% health: +50% damage, faster attacks and movement until killed.

### Primeval Colossus (`stoneborn_colossus`, colossus)

**Tripo art prompt:** Colossal ancient stone giant covered in moss and ruins, a broken tower fused into its back, huge boulder fists, glowing blue ether seams across its body, a cavernous face with glowing eyes, dust falling from its joints. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Boulder Fist** (melee): A crushing boulder fist at its current target.
- Skill pool:
  - `stoneborn_quake` **Quake** (selfCircle, 1.3s cast, 10s cd, knockback 520cm): Marks a ring around itself, then quakes, knocking everyone inside back.
  - `stoneborn_boulder_toss` **Boulder Toss** (targetCircle, 1.4s cast, 12s cd, slows 3s): Hurls a boulder at its target; the circle is crushed and slowed for 3s.
  - `stoneborn_crush` **Colossal Crush** (cone, 1.3s cast, 9s cd): Winds up a two-fisted frontal crush. Only the tank should stand in front.
  - `stoneborn_petrify` **Petrifying Gaze** (targetCircle, 1.4s cast, 15s cd, roots 2.0s): Marks a circle; champions inside are petrified (rooted) for 2s.
  - `stoneborn_colossus_rage` **Awakened Wrath** (enrage, 1.0s cast, always in the kit): At 30% health it fully awakens: +40% damage and faster attacks.

## The Feral Kin (`feral_kin`)

*Beasts driven mad by the Breach: werebears, dire wolves, tusked behemoths and the wild centaurs who ride with them.*

- **Origin (player counterpart):** The maddened wild kin of the Gravewood Bear, the Totemic Behemoth and the Evergrove Centaur. Player races: beast, centaur; profiles: `bear`, `totemic_behemoth`, `evergrove_centaur`.
- **Palette:** base #66472d `[0.4, 0.28, 0.18]`, accent #d8ccb2 `[0.85, 0.8, 0.7]`, secondary #332619, glow #ffcc33.
- **Reskin sets (palette variants):** Umberhide (base #66472d, accent #d8ccb2); Frostpelt (base #bfc6d1, accent #6699e5); Blackmaw (base #1e1919, accent #e53319).
- **Footsteps:** `beast` (per-unit overrides below). **Ambience cue:** `amb.race.grove`. **Voice cue:** `voice.race.growl`.

| Slot | Unit id | Name | Role | Fallback body | Draw per match |
|---|---|---|---|---|---|
| line | `dire_wolf` | Dire Wolf | bruiser | `hollow_infantry` | 2 |
| bruiser | `werebear_mauler` | Werebear Mauler | bruiser | `ironbound_bruiser` | 3 |
| tank | `tusked_behemoth` | Tusked Behemoth | tank | `hollow_shieldbearer` | 3 |
| caster | `feral_shaman` | Antlered Shaman | caster | `blight_caster` | 3 |
| ranged | `wild_outrider` | Wild Outrider | ranged | `barbed_hunter` | 3 |
| special | `bristleback` | Bristleback Boar | swarm | `hollow_infantry` | 2 |
| warlord | `feral_ursoth` | Ursoth, the Elder Bear | bruiser boss | `gravemaw_pack_leader` | all |
| colossus | `feral_mammoth` | Totemic Mammoth | tank boss | `hollow_siegebreaker` | all |

### Dire Wolf (`dire_wolf`, line)

**Tripo art prompt:** Huge shaggy dire wolf, shoulder-high to a man, dark brown fur with a pale mane, scarred muzzle and yellow eyes, bone fetishes tied into its fur (fallback: hunched two-legged wolfman). Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Savage Bite** (melee): A savage bite at its current target.
- Skill pool:
  - `feral_pounce` **Pounce** (charge, 0.7s cast, 11s cd): Marks a short line and pounces along it.
  - `feral_hamstring` **Hamstring** (cone, 0.8s cast, 9s cd, slows 3s): A telegraphed hamstring bite that slows the victims for 3s.
  - `feral_pack_howl` **Pack Howl** (rally, 1.2s cast, 20s cd): Howls: nearby monsters gain +20% damage and attack speed for 8s.

### Werebear Mauler (`werebear_mauler`, bruiser)

**Tripo art prompt:** Towering upright werebear with thick dark brown fur, massive clawed paws, torn remains of leather armour and a broken shackle, root growth and moss across its shoulders, amber eyes, roaring open jaws. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Maul** (melee): A mauling claw swipe at its current target.
- Skill pool:
  - `feral_maul` **Savage Maul** (cone, 1.1s cast, 9s cd): Winds up a two-pawed maul in a cone.
  - `feral_bear_charge` **Bear Charge** (charge, 0.9s cast, 14s cd, knockback 350cm): Marks a line and charges on all fours, knocking champions aside.
  - `feral_thick_hide` **Thick Hide** (shieldWall, 0.5s cast, 30s cd): Below 50% health its hide thickens: 40% less damage for 6s.
  - `feral_rage` **Feral Rage** (enrage, 1.0s cast): At 35% health it rages: +35% damage and faster attacks.

### Tusked Behemoth (`tusked_behemoth`, tank)

**Tripo art prompt:** Massive upright elephant-rhino hybrid warrior with grey leathery hide, curved ivory tusks and a nose horn, carved stone plates tied on as armour, a small ritual totem shield, painted tribal markings, pillar-thick legs. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Tusk Gore** (melee): A goring tusk strike at its current target.
- Skill pool:
  - `feral_trumpet` **Trumpeting Challenge** (provoke, 0.5s cast, 16s cd): Trumpets: for 6s nearby champions deal 35% less damage to anything but the Behemoth.
  - `feral_trample` **Trample** (charge, 0.9s cast, 14s cd, knockback 420cm): Marks a line and tramples through it, knocking champions aside.
  - `feral_herd_guard` **Herd Guard** (guard, 0.4s cast, 14s cd): Guards the most injured nearby ally for 8s, taking 40% of its damage.
  - `feral_earthshaker` **Earthshaker** (selfCircle, 1.2s cast, 14s cd, roots 1.5s): Marks a ring and stamps; everyone inside is rooted for 1.5s.

### Antlered Shaman (`feral_shaman`, caster)

**Tripo art prompt:** Hunched goat-legged beastman shaman with a deer skull mask and large antlers, a cloak of feathers and pelts, a staff of bone and antler hung with bells and charms, glowing yellow eyes. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Spirit Bolt** (projectile): Interruptible cast. A bolt of wild spirit energy.
- Skill pool:
  - `feral_spirit_mending` **Spirit Mending** (healAlly, 2.0s cast, 12s cd): Interruptible 2s cast that restores 20% health to the most injured ally below 60%.
  - `feral_savage_totem` **Savage Chant** (rally, 1.2s cast, 20s cd): Chants: nearby monsters gain +25% damage and attack speed for 8s.
  - `feral_thorn_hex` **Bramble Hex** (targetCircle, 1.2s cast, 14s cd, roots 2.0s): Marks a circle; brambles root everyone inside for 2s.
  - `feral_spirits` **Feral Spirits** (summon, 1.6s cast, 28s cd, summons 2x dire_wolf): Interruptible cast: calls two Dire Wolves to its side.

### Wild Outrider (`wild_outrider`, ranged)

**Tripo art prompt:** Wild centaur archer with a shaggy chestnut horse body, bare painted human torso, antler headdress, a long recurve bow, quiver of feathered arrows, braided mane with beads (fallback: two-legged archer). Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Wild Arrow** (projectile): Short aim, then looses an arrow in a fixed direction.
- Skill pool:
  - `feral_volley` **Volley** (targetCircle, 1.0s cast, 10s cd): Marks a circle on its target, then a volley lands there.
  - `feral_gallop_away` **Gallop Away** (disengage, 10s cd): Gallops away when a champion reaches melee range.
  - `feral_trample_charge` **Trampling Charge** (charge, 0.9s cast, 15s cd, knockback 300cm): Marks a line and gallops through it.
  - `feral_crippling_arrow` **Crippling Arrow** (targetCircle, 0.9s cast, 11s cd, slows 4s): Marks a circle; the arrows slow everyone inside for 4s.

### Bristleback Boar (`bristleback`, special)

**Tripo art prompt:** Knee-high wild boar with a ridge of long quill-like bristles, curved yellow tusks, muddy black hide, tiny furious red eyes (fallback: squat two-legged boarman). Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Gore** (melee): A goring charge at its current target.
- Skill pool:
  - `feral_boar_gore` **Gore Rush** (charge, 0.7s cast, 11s cd): Marks a short line and rushes along it.
  - `feral_bristle_burst` **Bristle Burst** (selfCircle, 1.0s cast, 12s cd): Marks a ring and shakes out its bristles, hitting everyone around it.
  - `feral_boar_frenzy` **Boar Frenzy** (enrage, 1.0s cast): At 50% health it frenzies: +30% damage and faster attacks.

### Ursoth, the Elder Bear (`feral_ursoth`, warlord)

**Tripo art prompt:** Gigantic ancient werebear chieftain with grizzled silver-streaked fur, a mantle of antlers and bones, a massive tree growing from its hunched back, huge scarred claws, one blind white eye and one glowing amber eye. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Elder Claws** (melee): A crushing claw blow at its current target.
- Skill pool:
  - `feral_elder_roar` **Elder Roar** (rally, 1.5s cast, 20s cd): 1.5s roar (gold ring): monsters within 12m gain +25% damage and attack speed for 10s.
  - `feral_rending_maul` **Rending Maul** (cone, 1.2s cast, 9s cd): Winds up a 120-degree maul for heavy damage. Only the tank should stand in front.
  - `feral_ursoth_charge` **Crushing Charge** (charge, 1.0s cast, 14s cd): Marks a line to the farthest champion within 12m and charges through it.
  - `feral_call_of_the_wild` **Call of the Wild** (summon, 1.6s cast, 26s cd, summons 2x dire_wolf): Interruptible cast: two Dire Wolves answer the call.
  - `feral_elder_fury` **Elder Fury** (enrage, 1.0s cast, always in the kit): At 30% health: +50% damage, faster attacks and movement until killed.

### Totemic Mammoth (`feral_mammoth`, colossus)

**Tripo art prompt:** Colossal upright mammoth-behemoth with shaggy brown wool, enormous curling tusks capped in carved stone, a huge ritual totem pole strapped across its back, stone plates tied to its shoulders, glowing yellow war paint. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Tusk Sweep** (melee): A sweeping tusk strike at its current target.
- Skill pool:
  - `feral_mammoth_stomp` **Titan Stomp** (selfCircle, 1.3s cast, 10s cd, knockback 520cm): Marks a ring around itself, then stomps, knocking everyone inside back.
  - `feral_mammoth_trample` **Stampede** (charge, 1.2s cast, 15s cd): Marks a long line to the farthest champion and tramples through it.
  - `feral_tusk_sweep` **Great Tusk Sweep** (cone, 1.3s cast, 9s cd): Winds up a wide frontal tusk sweep. Only the tank should stand in front.
  - `feral_earthquake` **Earthquake** (selfCircle, 1.6s cast, 16s cd, roots 1.5s): Marks a huge ring; the quake roots everyone inside for 1.5s.
  - `feral_mammoth_rage` **Primal Rage** (enrage, 1.0s cast, always in the kit): At 30% health it goes wild: +40% damage and faster attacks.

## The Fallen Order (`fallen_order`)

*Knights and paladins who kept their vows past death and faith. Their light has curdled into something that burns the living.*

- **Origin (player counterpart):** The corrupted mirror of the Iron Warden, the Relic Paladins and the Keeper of the Light. Player races: human; profiles: `knight`, `paladin_righteous`, `paladin_holy`, `keeper_of_light`.
- **Palette:** base #59544c `[0.35, 0.33, 0.3]`, accent #b28c26 `[0.7, 0.55, 0.15]`, secondary #590c14, glow #e5d84c.
- **Reskin sets (palette variants):** Tarnished Oath (base #59544c, accent #b28c26); Bone White (base #ccc6b2, accent #991919); Blackguard (base #19191e, accent #8cd84c).
- **Footsteps:** `plate` (per-unit overrides below). **Ambience cue:** `amb.race.hollow`. **Voice cue:** `voice.race.growl`.

| Slot | Unit id | Name | Role | Fallback body | Draw per match |
|---|---|---|---|---|---|
| line | `fallen_squire` | Fallen Squire | bruiser | `hollow_infantry` | 2 |
| bruiser | `dread_knight` | Dread Knight | bruiser | `ironbound_bruiser` | 3 |
| tank | `oathbreaker_templar` | Oathbreaker Templar | tank | `hollow_shieldbearer` | 3 |
| caster | `blighted_chaplain` | Blighted Chaplain | caster | `blight_caster` | 3 |
| ranged | `fallen_inquisitor_crossbow` | Crossbow Inquisitor | ranged | `barbed_hunter` | 3 |
| special | `flagellant` | Flagellant | swarm | `hollow_infantry` | 2 |
| warlord | `fallen_high_inquisitor` | High Inquisitor Maledict | caster boss | `blight_caster` | all |
| colossus | `fallen_crusader` | The Hollow Crusader | tank boss | `hollow_siegebreaker` | all |

### Fallen Squire (`fallen_squire`, line)

**Tripo art prompt:** Gaunt pale squire in dented tarnished mail and a torn crimson tabard with a defaced sun emblem, open-faced helm, arming sword and a small buckler, hollow glowing gold eyes. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Arming Sword** (melee): A sword stroke at its current target.
- Skill pool:
  - `fallen_oath_strike` **Broken Oath** (cone, 1.0s cast, 9s cd): A telegraphed sweeping stroke.
  - `fallen_shield_rush` **Buckler Rush** (charge, 0.9s cast, 13s cd): Marks a line and rushes along it behind its buckler.
  - `fallen_last_stand` **Last Stand** (shieldWall, 0.5s cast, 30s cd): Below 40% health it braces: 40% less damage for 5s.

### Dread Knight (`dread_knight`, bruiser)

**Tripo art prompt:** Tall knight in blackened fluted full plate with spikes on the pauldrons, a tattered crimson cloak, a horned great-helm with a glowing gold visor slit, a notched two-handed greatsword trailing dark smoke. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Greatsword** (melee): A heavy greatsword stroke at its current target.
- Skill pool:
  - `fallen_dark_cleave` **Dark Cleave** (cone, 1.1s cast, 9s cd): Winds up a smoking greatsword cleave in a cone.
  - `fallen_deathcharge` **Deathcharge** (charge, 0.9s cast, 14s cd): Marks a line and charges through it, hitting everyone in the line.
  - `fallen_unholy_frenzy` **Unholy Frenzy** (enrage, 1.0s cast): At 35% health: +35% damage and faster attacks.
  - `fallen_chain_grasp` **Chains of Penance** (pull, 1.0s cast, 16s cd): Marks a line and hurls a chain that drags a champion to it.

### Oathbreaker Templar (`oathbreaker_templar`, tank)

**Tripo art prompt:** Massive templar in heavy gothic plate gone green with corrosion, a huge tower shield bearing a cracked sun relic that leaks sickly gold light, a flanged mace, a tall plumed helm with a skull-like faceplate. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Relic Bash** (melee): A relic-shield bash at its current target.
- Skill pool:
  - `fallen_judgment_taunt` **Judgment** (provoke, 0.5s cast, 16s cd): Condemns nearby champions: for 6s they deal 35% less damage to anything but the Templar.
  - `fallen_consecrated_wall` **Profane Aegis** (shieldWall, 0.5s cast, 30s cd): Below 50% health: 50% less damage for 6s.
  - `fallen_martyrs_oath` **Martyr's Oath** (guard, 0.4s cast, 14s cd): Guards the most injured nearby ally for 8s, taking 40% of its damage.
  - `fallen_consecrate` **Profane Consecration** (selfCircle, 1.2s cast, 14s cd): Marks a ring; the ground around it burns with corrupt light for 5s.

### Blighted Chaplain (`blighted_chaplain`, caster)

**Tripo art prompt:** Hunched robed chaplain in stained ivory and crimson vestments, a swinging censer on a chain pouring green-gold smoke, a mitre-like hood hiding a withered face with glowing eyes, prayer beads of teeth. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Profane Smite** (projectile): Interruptible cast. A bolt of corrupted light.
- Skill pool:
  - `fallen_profane_light` **Profane Light** (targetCircle, 1.2s cast, 10s cd): Marks a circle under its target, then corrupt light sears it.
  - `fallen_dark_absolution` **Dark Absolution** (healAlly, 2.0s cast, 12s cd): Interruptible 2s cast that restores 20% health to the most injured ally below 60%.
  - `fallen_censer_smoke` **Censer Smoke** (targetCircle, 1.2s cast, 14s cd, silences 2.0s): Marks a circle; choking incense silences everyone inside for 2s.
  - `fallen_hymn_of_wrath` **Hymn of Wrath** (rally, 1.2s cast, 20s cd): Chants: nearby monsters gain +25% damage and attack speed for 8s.

### Crossbow Inquisitor (`fallen_inquisitor_crossbow`, ranged)

**Tripo art prompt:** Lean inquisitor in a wide-brimmed hat and long dark leather coat over a breastplate, a heavy repeating crossbow, bandolier of silver bolts, a sun pendant cracked in half, pale face with glowing gold eyes. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Silver Bolt** (projectile): Short aim, then fires a silver bolt in a fixed direction.
- Skill pool:
  - `fallen_purging_bolts` **Purging Bolts** (targetCircle, 1.0s cast, 10s cd): Marks a circle on its target, then a burst of bolts lands there.
  - `fallen_shackle_bolt` **Shackle Bolt** (targetCircle, 0.9s cast, 14s cd, roots 1.5s): Marks a small circle; chained bolts root everyone inside for 1.5s.
  - `fallen_tactical_retreat` **Tactical Retreat** (disengage, 10s cd): Leaps away when a champion reaches melee range.
  - `fallen_condemn` **Condemn** (targetCircle, 1.1s cast, 15s cd, silences 2.0s): Marks a circle; the condemned are silenced for 2s.

### Flagellant (`flagellant`, special)

**Tripo art prompt:** Emaciated bare-chested zealot in a hooded sackcloth robe, back covered in lash scars, a barbed flail in each hand, rope belt with prayer scrolls, wild fanatical stance. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Barbed Flail** (melee): A frenzied flail lash at its current target.
- Skill pool:
  - `fallen_zealous_frenzy` **Zealous Frenzy** (enrage, 1.0s cast): At 50% health it frenzies: +30% damage and faster attacks.
  - `fallen_zealous_leap` **Zealous Leap** (charge, 0.7s cast, 11s cd): Marks a short line and leaps along it.
  - `fallen_self_mortify` **Mortification** (selfCircle, 1.0s cast, 11s cd): Marks a ring and lashes wildly, hitting everyone around it.

### High Inquisitor Maledict (`fallen_high_inquisitor`, warlord)

**Tripo art prompt:** Tall gaunt high inquisitor in layered crimson and black robes with gold filigree, a towering mitre, chains wrapped around his arms ending in hooks, a staff topped with a burning sun relic leaking green-gold fire, a burning book chained at his hip. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Verdict** (projectile): Interruptible cast. A lance of corrupted light.
- Skill pool:
  - `fallen_chains_of_judgment` **Chains of Judgment** (pull, 1.2s cast, 15s cd): Chains mark a line to the farthest champion and drag them to him.
  - `fallen_mass_silence` **Anathema** (selfCircle, 1.6s cast, 18s cd, silences 3.0s): Marks a large ring; everyone inside is silenced for 3s.
  - `fallen_pyre` **Heretic's Pyre** (targetCircle, 1.4s cast, 13s cd): Marks a wide circle; a pyre of corrupt fire burns there for 6s.
  - `fallen_summon_flagellants` **Call the Penitent** (summon, 1.6s cast, 24s cd, summons 3x flagellant): Interruptible cast: three Flagellants rush to his side.
  - `fallen_inquisitor_zeal` **Righteous Fury** (enrage, 1.0s cast, always in the kit): At 30% health: +40% damage and faster casting until killed.

### The Hollow Crusader (`fallen_crusader`, colossus)

**Tripo art prompt:** Giant armoured crusader three times a man's height, ornate tarnished gold-and-steel plate with a crumbling sun relief on the breastplate, an empty helm with gold fire inside, a greatsword taller than a man, a torn banner on its back. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Crusader's Blade** (melee): A massive greatsword stroke at its current target.
- Skill pool:
  - `fallen_crusader_cleave` **Sundering Verdict** (cone, 1.3s cast, 9s cd): Winds up a wide frontal cleave. Only the tank should stand in front.
  - `fallen_crusade` **Crusade** (charge, 1.2s cast, 15s cd): Marks a line to the farthest champion and charges through it.
  - `fallen_judgment_slam` **Judgment Slam** (selfCircle, 1.3s cast, 11s cd, knockback 500cm): Marks a ring around itself, then slams, knocking everyone inside back.
  - `fallen_crusader_aegis` **Aegis of Ruin** (shieldWall, 0.5s cast, 30s cd): Below 50% health: 40% less damage for 6s.
  - `fallen_crusader_wrath` **Fallen Wrath** (enrage, 1.0s cast, always in the kit): At 30% health: +40% damage and faster attacks until killed.

## The Voidborn (`voidborn`)

*Things from between the stars that crawl through every rift the Breach tears open. They unmake light, sound and flesh.*

- **Origin (player counterpart):** What the Lantern Whisp and the Keeper of the Light hold back, and what the Rift Summoner and the Cinder Arcanist sometimes let through. Player races: spirit, human; profiles: `whisp`, `keeper_of_light`, `summoner`, `wizard`.
- **Palette:** base #1e0f2d `[0.12, 0.06, 0.18]`, accent #bf33d8 `[0.75, 0.2, 0.85]`, secondary #0c0c19, glow #994cff.
- **Reskin sets (palette variants):** Riftblack (base #1e0f2d, accent #bf33d8); Starfall (base #191e4c, accent #66d8ff); Null Pale (base #b2adbf, accent #8c1999).
- **Footsteps:** `whisp` (per-unit overrides below). **Ambience cue:** `amb.race.drowned`. **Voice cue:** `voice.race.growl`.

| Slot | Unit id | Name | Role | Fallback body | Draw per match |
|---|---|---|---|---|---|
| line | `rift_stalker` | Rift Stalker | bruiser | `hollow_infantry` | 2 |
| bruiser | `void_ravager` | Void Ravager | bruiser | `ironbound_bruiser` | 3 |
| tank | `null_warden` | Null Warden | tank | `hollow_shieldbearer` | 3 |
| caster | `rift_weaver` | Rift Weaver | caster | `blight_caster` | 3 |
| ranged | `rift_gazer` | Rift Gazer | ranged | `barbed_hunter` | 3 |
| special | `voidling` | Voidling | swarm | `hollow_infantry` | 2 |
| warlord | `voidborn_herald` | The Rift Herald | caster boss | `blight_caster` | all |
| colossus | `voidborn_devourer` | Devourer of Stars | tank boss | `hollow_siegebreaker` | all |

### Rift Stalker (`rift_stalker`, line)

**Tripo art prompt:** Lanky shadow humanoid with elongated limbs, skin like a starry night sky with drifting violet specks, a featureless head split by a vertical glowing magenta slit, long scythe-like finger blades, crouched flickering stance. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Void Talons** (melee): A slash of void talons at its current target.
- Skill pool:
  - `void_phase_strike` **Phase Strike** (charge, 0.7s cast, 11s cd): Marks a line and phases through it, slashing everyone in the path.
  - `void_rend` **Void Rend** (cone, 0.9s cast, 9s cd, slows 3s): A telegraphed rend that slows everyone hit for 3s.
  - `void_blink` **Blink** (disengage, 10s cd): Blinks away when a champion reaches melee range.

### Void Ravager (`void_ravager`, bruiser)

**Tripo art prompt:** Hulking aberration with chitinous black-violet plates, four arms (two ending in crushing mandible claws), a head that is a gaping ring of teeth around a glowing void, cracks of starlight across its body. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Mandible Claws** (melee): A crushing claw strike at its current target.
- Skill pool:
  - `void_gravity_slam` **Gravity Slam** (selfCircle, 1.2s cast, 11s cd, knockback 450cm): Marks a ring and slams, knocking everyone inside away.
  - `void_rift_charge` **Rift Charge** (charge, 0.9s cast, 14s cd): Marks a line and tears through it, hitting everyone in the line.
  - `void_null_cleave` **Null Cleave** (cone, 1.1s cast, 11s cd, silences 1.5s): Winds up a cleave that silences everyone hit for 1.5s.
  - `void_unravel` **Unravel** (enrage, 1.0s cast): At 35% health it unravels: +35% damage and faster attacks.

### Null Warden (`null_warden`, tank)

**Tripo art prompt:** Tall armoured aberration whose body is a hollow shell of obsidian plates around a swirling black hole core, a round shield made of a floating ring of dark stone, a single huge eye on its chest. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Obsidian Bash** (melee): A shield bash at its current target.
- Skill pool:
  - `void_gaze` **Void Gaze** (provoke, 0.5s cast, 16s cd): Its eye opens: for 6s nearby champions deal 35% less damage to anything but the Warden.
  - `void_event_horizon` **Event Horizon** (shieldWall, 0.5s cast, 30s cd): Below 50% health it bends light around itself: 50% less damage for 6s.
  - `void_tether` **Void Tether** (guard, 0.4s cast, 14s cd): Tethers the most injured ally, taking 40% of its damage for 8s.
  - `void_gravity_well` **Gravity Well** (selfCircle, 1.2s cast, 14s cd, roots 1.5s): Marks a ring; the gravity well roots everyone inside for 1.5s.

### Rift Weaver (`rift_weaver`, caster)

**Tripo art prompt:** Floating robed figure with no face under its hood, only a swirling rift, six thin arms weaving threads of violet light, tattered robes that fade into smoke, glowing glyphs orbiting its head. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Void Bolt** (projectile): Interruptible cast. A bolt of void energy.
- Skill pool:
  - `void_rift` **Void Rift** (targetCircle, 1.2s cast, 12s cd): Tears a rift under its target that drains life for 5s.
  - `void_mind_spike` **Mind Spike** (targetCircle, 1.2s cast, 14s cd, silences 2.0s): Marks a circle; champions inside are silenced for 2s.
  - `void_unmaking` **Unmaking** (cone, 1.2s cast, 11s cd): Unleashes a cone of unmaking in front of it.
  - `void_rift_mending` **Rift Mending** (healAlly, 2.0s cast, 12s cd): Interruptible 2s cast that restores 20% health to the most injured ally below 60%.

### Rift Gazer (`rift_gazer`, ranged)

**Tripo art prompt:** Floating orb-like aberration with one giant central eye and a crown of smaller eyes on writhing stalks, a leathery violet body, trailing tentacles beneath, glowing beam charging in the main eye. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Eye Beam** (projectile): Short aim, then fires an eye beam in a fixed direction.
- Skill pool:
  - `void_disintegrate` **Disintegrate** (cone, 1.3s cast, 12s cd): Charges its eye, then fires a long narrow disintegration beam.
  - `void_orb` **Void Orb** (targetCircle, 1.0s cast, 10s cd): Marks a circle on its target, then a void orb detonates there.
  - `void_warp` **Warp** (disengage, 10s cd): Warps away when a champion reaches melee range.
  - `void_paralyze_gaze` **Paralyzing Gaze** (targetCircle, 1.1s cast, 15s cd, roots 1.5s): Marks a circle; champions inside are paralysed (rooted) for 1.5s.

### Voidling (`voidling`, special)

**Tripo art prompt:** Small scuttling void creature the size of a dog, a round body of black chitin with starlight cracks, many thin legs, a wide mouth of needle teeth, two pinprick magenta eyes (fallback: small hunched two-legged imp). Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Needle Bite** (melee): A needle-toothed bite at its current target.
- Skill pool:
  - `void_voidling_burst` **Void Burst** (selfCircle, 1.0s cast, 12s cd): Marks a ring and bursts with void energy, hitting everyone around it.
  - `void_latch` **Latch** (pull, 1.0s cast, 16s cd): Marks a line and latches onto a distant champion, dragging them in.
  - `void_voidling_frenzy` **Hunger** (enrage, 1.0s cast): At 50% health it frenzies: +30% damage and faster attacks.

### The Rift Herald (`voidborn_herald`, warlord)

**Tripo art prompt:** Towering floating herald of the void, a tall robed silhouette with a halo of shattered black glass, a face that is a spiral galaxy, long trailing sleeves that become tentacles of starlight, a staff that is a floating tear in reality. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Word of Unmaking** (projectile): Interruptible cast. A bolt of void in a fixed direction.
- Skill pool:
  - `void_open_the_rift` **Open the Rift** (summon, 1.6s cast, 24s cd, summons 3x voidling): Interruptible cast: three Voidlings crawl out of a rift beside it.
  - `void_silence_of_stars` **Silence of the Stars** (selfCircle, 1.6s cast, 18s cd, silences 3.0s): Marks a large ring; everyone inside is silenced for 3s.
  - `void_collapsing_star` **Collapsing Star** (targetCircle, 1.4s cast, 13s cd): Marks a wide circle; a collapsing star burns there for 6s.
  - `void_herald_grasp` **Grasp of the Void** (pull, 1.2s cast, 15s cd): A tendril marks a line to the farthest champion and drags them in.
  - `void_herald_ascension` **Ascension** (enrage, 1.0s cast, always in the kit): At 30% health: +40% damage and faster casting until killed.

### Devourer of Stars (`voidborn_devourer`, colossus)

**Tripo art prompt:** Colossal bipedal void leviathan, a hunched titan of black chitin and exposed starry void, a head that splits open into four mandibles around a black hole mouth, long arms ending in claws, rings of floating debris orbiting its body. Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.

- Basic: **Titan Claw** (melee): A crushing claw blow at its current target.
- Skill pool:
  - `void_devour` **Devour** (pull, 1.4s cast, 17s cd): Marks a line to the farthest champion and drags them into its maw.
  - `void_singularity` **Singularity** (selfCircle, 1.5s cast, 15s cd, roots 2.0s): Marks a ring around itself; everyone inside is rooted by gravity for 2s.
  - `void_breath` **Void Breath** (cone, 1.4s cast, 10s cd): Winds up a long cone of void breath. Get out of the cone.
  - `void_titan_slam` **Titan Slam** (selfCircle, 1.3s cast, 11s cd, knockback 520cm): Marks a ring and slams, knocking everyone inside back.
  - `void_devourer_hunger` **Endless Hunger** (enrage, 1.0s cast, always in the kit): At 30% health: +40% damage and faster attacks until killed.
