# New Champions

This page covers five DPS and hybrid champions and the systems that came with them:

- **Gunblade** (Bounty Hunter)
- **Witch Slayer**
- **Huntress** (mounted glaive thrower)
- **Aetheri Artificer**
- **Aetheri Warden**

The systems are the Aetheri **Constructs** skill category and the **Aetheri Remnant** monster race.

## Sources of truth

| What | Where |
|---|---|
| Skill names, numbers, descriptions | `Tools/BuildAbilityDB.py` (`NEW_CHAMPION_SKILLS`), producing `Content/Data/AbilityDatabase.json` |
| Profiles, art bindings, loadouts, grips, draft backgrounds | `Tools/AuthorNewChampions.py` (`--check` exits 1 when stale), writing `ChampionRoster.json`, `ChampionArtBindings.json`, `WeaponLoadouts.json`, `WeaponGrips.json` and `DraftBackgrounds.json` |
| Construct recipes (runtime) | `Source/CiresTeamSurvival/CireTechConstructs.cpp` |
| Aetheri monster race | `Tools/AuthorRaces.py`, producing `Content/Data/Races.json`. See [Races.md](Races.md#the-aetheri-remnant-aetheri) |

Numbers below are base values at rank 1. `{effect}` scales with skill rank on the Ability DB curve. The **AGI** and **INT** terms scale with the champion's primary stat. Each champion has **6 actives, 1 passive and 1 ultimate**. The actives all come from the champion's signature pool.

## Gunblade: Bounty Hunter

AGI, energy. Difficulty 2. Race: human. Basic attack range 950.

He fights at two ranges. The basic attack is a flintlock shot at mid range and switches to the falchion up close. Holy silver punishes undead and void enemies, and bounties pay gold.

| Skill | Kind | Summary |
|---|---|---|
| Silver Shot | active (aim) | Aimed round: 95 + 1.6x AGI; +60% against undead and void. CD 8s. |
| Hex Mark | active (enemy) | Target takes +15% damage for 12s. A kill pays bounty gold (25, elites 60, bosses 150). CD 12s. |
| Powder Flask | active (aim) | Fused flask bursts for 70 + 1x AGI in 2.6m and slows 35% for 2.5s. CD 12s. |
| Blade Flurry | active (self) | Three falchion slashes around him, each 40 + 0.8x AGI. CD 9s. |
| Hunter's Stride | active (aim) | 5.5m dash; the next basic attack within 4s deals +50%. CD 11s. |
| Warding Talisman | active (self) | Takes 30% less damage for 4s and cleanses slows. CD 18s. |
| Price on Every Soul | passive | Killing blows pay +5 gold (x3 elites, x8 bosses) and restore 15 energy. |
| **Collect the Bounty** | ultimate (enemy) | Execution shot: 180 + 3x AGI + 40% of missing health (bonus capped at 400). Non-boss monsters under 20% health die. A kill refunds half the cooldown and pays a marked bounty twice. CD 70s. |

## Witch Slayer

INT, mana. Difficulty 3. Race: human. Basic attack range 900.

An anti-caster who carries a blunderbuss and a spectral blade. His tools are interrupts, silences, purges and banishment.

| Skill | Kind | Summary |
|---|---|---|
| Arcane Blunderbuss | active (cone) | 60-degree cone, 5.5m: 110 + 1.8x INT. Deals +40% to casters and interrupts them (1.5s lockout). CD 8s. |
| Spirit Lantern | active (construct: trap) | A trap that lasts 20s (up to 2). The first enemy within 1.5m sets it off: 60 + 1x INT and a 2s silence in 2.6m. CD 14s. |
| Purge | active (enemy) | Strips every buff and silences for 2.5s; 60 + 1x INT. CD 14s. |
| Banishment | active (enemy) | Exiles the target for 2s: it cannot act or be harmed. On return it takes 80 + 1.5x INT void damage. Bosses are silenced and slowed instead. CD 22s. |
| Witchfinder's Mark | active (enemy) | Reveals the target for 10s; it takes +12% damage, tripled while it casts. CD 10s. |
| Spectral Blade | active (enemy) | Lunge up to 6m: 90 + 1.4x INT. CD 7s. |
| Witchbane | passive | +20% damage (cap 40%) to enemies that are casting, silenced or marked. |
| **Hexbane Judgment** | ultimate (aim) | A 4.5m circle that bursts after 1s: 200 + 3x INT, strips all buffs and silences for 3s. CD 80s. |

## Huntress: Mounted Glaive Thrower

AGI, energy. Difficulty 2. Race: sylvan. Basic attack range 1300.

She rides the sabercat Ashfang. Her glaives bounce between targets, the cat fights at close range, and an owl scouts.

| Skill | Kind | Summary |
|---|---|---|
| Bouncing Glaive | active (enemy) | Hits the target, then bounces to 4 more within 5m, losing 20% per bounce. 80 + 1.5x AGI. CD 8s. |
| Sabercat Pounce | active (aim) | 7m leap; mauls everything in 2.6m for 75 + 1x AGI and slows 40% for 2s. CD 12s. |
| Owl Scout | active (aim) | Reveals and tracks enemies in 4.5m for 8s; they take +10% damage from her. CD 14s. |
| Moonlit Sprint | active (self) | +40% move speed for 4s and cleanses slows. CD 16s. |
| Crescent Volley | active (line) | A 14m line glaive that pierces up to five enemies: 90 + 1.4x AGI. CD 10s. |
| Sabercat Rake | active (cone) | A 70-degree arc of 3.2m in front of her: 70 + 1.2x AGI. CD 7s. |
| Moon Glaive | passive | Glaive throws bounce to 2 more enemies within 4.5m, for 60% and then 36% damage. |
| **Glaive Storm** | ultimate (self) | Whirling glaives for 6s: 45 + 0.5x AGI every 0.5s within 4.8m. CD 75s. |

## Aetheri Artificer

INT, mana. Difficulty 3. Race: aetheri. Basic attack range 1200.

He fights through Constructs. Five of his eight skills place or empower a construct.

| Skill | Kind | Summary |
|---|---|---|
| Photon Turret | construct: turret | Lasts 25s, 240 HP, up to 2. Fires 18 + 0.35x INT bolts every 0.8s within 9.5m. CD 14s. |
| Skitter Swarm | construct: skitter | Three bombs (up to 6 live) race at the nearest enemies and explode for 70 + 0.8x INT in 2m. CD 15s. |
| Arc Mine | construct: trap | Lasts 30s, up to 3. The first enemy within 1.5m detonates it for 110 + 1.2x INT in 2.4m. CD 9s. |
| Disruption Pylon | construct: pylon | Lasts 15s. Enemies in its 4.5m field deal 25% less damage (cap 40%). CD 20s. |
| Phase Lance | active (skillshot) | Energy lance: 100 + 1.8x INT. CD 6s. |
| Overcharge | active (self) | His constructs within 12m: turrets fire +100% faster for 6s, and every construct regains 25% health. CD 20s. |
| Aether Engineering | passive | His constructs have +25% health and last 20% longer. |
| **Warp Obelisk** | ultimate (construct) | A siege obelisk for 15s (600 HP). Fires beams within 13m for 60 + 1x INT, splashing 2m. CD 80s. |

## Aetheri Warden

INT, mana. Difficulty 2. Race: aetheri. Basic attack range 220 (energy halberd). Roles: healer, support and tank.

A field-shaper who places pylons, snares enemies and heals allies.

| Skill | Kind | Summary |
|---|---|---|
| Aegis Pylon | construct: pylon | Lasts 15s. Allies in 4.5m regenerate 2% max HP/s (cap 4%) and take 15% less damage. CD 18s. |
| Haste Pylon | construct: pylon | Lasts 12s. Allies in 4.5m move and attack +25% faster (cap 40%). CD 20s. |
| Gravity Pylon | construct: pylon | Lasts 12s. Enemies in 4.5m are slowed 35% (cap 50%). CD 18s. |
| Stasis Snare | construct: trap | Lasts 30s, up to 3. The first enemy within 1.5m is held in stasis for 1.5s and takes 20 + 0.3x INT. Bosses are slowed instead. CD 14s. |
| Aether Mend | active (ally) | 1s cast: heals an ally or himself for 85 + 2.5x INT. CD 6s. |
| Repulsor Pulse | active (self) | 60 + 1x INT damage and a 30% slow for 2s in 3.5m; monsters are taunted briefly. CD 10s. |
| Resonant Lattice | passive | Allies inside his Aegis Pylon or Nexus take a further 10% less damage (cap 20%). His pylons last 20% longer. |
| **Aether Nexus** | ultimate (construct) | A Nexus for 10s. Allies in its 6.5m field take 40% less damage and regenerate 5% max HP/s (cap 8%). Enemies inside are slowed. CD 85s. |

## Constructs

Constructs are `ACireConstruct` kinds added by `CireTechConstructs`. They are:

- server-authoritative and replicated. `RunExpansionChecks --only network` checks this with `construct_replication=1`.
- destructible
- placed with the ground-aim system
- shown with arcane (energy) runes

Both champions and the Aetheri monster race deploy them. They work in the survival lanes and in the arena.

### Types

| Kind | Behaviour | Champion recipes | Monster recipes |
|---|---|---|---|
| **Turret** | Auto-fires at the nearest enemy in range with a clear line of sight. It ranks traps and skitters 4x less attractive than real units. | Photon Turret (limit 2), Warp Obelisk (limit 1, splash) | `npc_photon_turret` Warp Turret (limit 2) |
| **Trap** | Invisible trigger radius; springs once on the first enemy and is consumed. | Arc Mine (3), Stasis Snare (3), Spirit Lantern (2) | `npc_stasis_mine` (3) |
| **Pylon** | A persistent field that ticks every 0.5s and applies a buff or debuff record (shield, haste, weaken, slow, nexus or empower). | Aegis, Haste, Gravity and Disruption pylons, Aether Nexus (1 each) | `npc_gravity_pylon`, `npc_empower_pylon` (1 each) |
| **Skitter** | A small runner. It races at the nearest enemy and explodes on contact; an armed skitter detonates when it expires. | Skitter Swarm (3 per cast, 6 live) | `npc_skitter` (3 per cast, 6 live) |

### Limits and placement

- **Owner limits.** Each recipe has a live-per-owner limit. Placing one more replaces the **oldest**.
- **Placement.** The construct is ground-aimed at the target point within cast range. `ACireConstruct::ValidatePlacementFor` rejects blocked spots. A multi-unit deploy (skitters) probes a small ring of spots around the aim point, up to six tries each. When nothing fits, the cast reports "No room to place this construct here." and the cast fails.
- **Scaling.** Champion constructs scale with the primary stat, the skill rank and passives: Aether Engineering adds +25% health and +20% lifetime, and Resonant Lattice adds +20% pylon lifetime. Monster constructs scale with the wave's damage multiplier.
- **Overcharge.** Turrets in 12m fire twice as fast for 6s, and every construct in range repairs 25%.

### Counterplay

- **Monsters target constructs.** Monster AI (`MonsterHandleConstructs`) looks for a champion turret or pylon within 6.5m when it has no victim in reach. It walks to the construct and smashes it every 1.8s. Traps and skitters are ignored.
- **Players target enemy constructs.** Players can kill Aetheri monster turrets and pylons, and interrupt the deploy casts. Every Aetheri deploy has an interruptible cast of 1.0 to 1.6s.
- **Pylons resist damage.** Pylons are warded crystal and take 15% less damage. Every other construct takes full damage.
- **Bosses resist stasis traps.** The Stasis Snare and the Stasis Mine slow a boss instead of locking it.
- **Constructs expire.** Every construct has a lifetime.

## The Aetheri Remnant (monster race)

The Remnant are crystal-and-light machine-cultists, the monster counterpart of the two Aetheri champions. The full roster, skills, palette and Tripo prompts are in [Races.md](Races.md#the-aetheri-remnant-aetheri). In short:

| Slot | Unit |
|---|---|
| line | Aetheri Phaseblade |
| bruiser | Aetheri Warframe |
| tank | Prism Bulwark: Empowering Pylon |
| caster | Aetheri Engineer: Deploy Turret, Skitter Swarm, Stasis Mine |
| ranged | Photon Lancer |
| special | Skitter Drone |
| warlord | The Warp Hierarch: Warp Pylons, Gravity Field, Warp In (3 Skitter Drones) |
| colossus | Aetheric Colossus: Shoulder Turrets |

Monster constructs are tinted crimson, violet or ice-blue so that they read as hostile next to the player constructs.

## Art bindings

Two reviews show the current bindings:

- `Tools/RunNewChampionsGallery.py` captures the lineup, close-ups, combat, constructs and an Aetheri wave to `Saved/NewChampionsGallery/<stamp>/`.
- `Tools/RunDraftGallery.py --champions gunblade,witch_slayer,huntress,aetheri_artificer,aetheri_warden` captures champion select.

| Champion | Body | Loadout (props) |
|---|---|---|
| Gunblade | Tripo `CTS_Champ_Gunblade` (ready row, idle arms relaxed at runtime) | falchion (primary), flintlock; basic attack switches pistol/falchion by range |
| Witch Slayer | Tripo `CTS_Champ_WitchSlayer` | arcane blunderbuss (primary), spectral blade on the hip |
| Huntress | Tripo `CTS_Champ_Huntress` **rider** seated on the Quaternius **wolf**, tinted as a sabercat | glaive (released on throw), glaive launcher |
| Aetheri Artificer | Tripo `CTS_Champ_AetheriArtificer` | aether staff |
| Aetheri Warden | Tripo `CTS_Champ_AetheriWarden` | aether halberd |

The pre-Tripo temporary bodies are kept in `AuthorNewChampions.BINDINGS`. They are used again if a Tripo row is no longer `ready`.

- **Icons.** There are 40 procedural ability icons (`/Game/UI/Abilities/T_<skill>`).
- **Buffs and aiming.** Buff registry rows exist for every construct field effect. Every aimed skill has a ground-aim descriptor.
- **Champion select.** All five champions have portraits (`/Game/UI/Draft/Portraits/T_Portrait_<id>`); the mounted Huntress is framed on the rider. Each has a background slot in `DraftBackgrounds.json`. Until a painting exists, it falls back to a role-themed scene: ranger, summoner or dryad, and ether_golem for both Aetheri. The painting prompts are stored in the same file.

## Known limits

- **Sabercat mount.** The Tripo sabercat (`/Game/Tripo/Champions/HuntressSabercat/CTS_Mount_HuntressSabercat`) is rigged but has **no animation clips**. The animated wolf stands in as the mount. Once idle, walk, run and attack clips exist, `final_bindings()` in `AuthorNewChampions.py` must be changed to bind the sabercat; today it always keeps the wolf for a mounted row.
- **No painted backgrounds.** None of the five champion-select backgrounds is painted yet; all use the role-themed fallbacks.
- **Aetheri monster bodies.** The Aetheri monster units use tinted fallback bodies from other races. Races.md holds their Tripo prompts.
- **Placeholder props.** The props (flintlock, falchion, blunderbuss, glaives, staff, halberd) are original prototype meshes authored in `Tools/BuildNewChampionContent.py`, not final art.
- **Balance.** Numbers are first-pass design values and have not been through the Balance Lab.
