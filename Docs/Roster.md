# Playable champion roster

`Content/Data/ChampionRoster.json` contains 22 selectable role profiles from
`C:/Users/Eric/Desktop/Unit Type.txt`, read as Windows-1252 without changing that
source. Its SHA256 is recorded in the JSON. Summoner is retained from the earlier
explicit request. Repeated Paladin entries become two class-selection paths.

Every profile starts with **zero learned skills**. Its six actives, one passive
and one ultimate are thematic draft examples, not an automatic starting kit.
`implemented` means the skill ID already has a gameplay implementation;
`planned` describes proposed mechanics that must not enter the normal draft or
be cast until implemented. Visual recipe support alone does not implement a
skill. Current Knight/Warden, Ranger, Scholar, Lancer and Summoner examples use
existing implemented skills. Shared skills deliberately remain reusable.

| Profile ID | Character / selection | Role | Primary | Basic style |
|---|---|---|---|---|
| knight | Iron Warden / Knight crusader | Tank | STR | Sword |
| ranger | Ash Ranger | Damage | AGI | Bow |
| scholar | Veil Scholar / healing Wizard | Healer | INT | Arcane |
| lancer | Dusk Lancer | Damage | AGI | Lance |
| summoner | Rift Summoner | Damage | INT | Arcane |
| bear | Gravewood Bear | Tank | STR | Claws |
| paladin_righteous | Relic Paladin / Righteous | Tank | STR | Flail + shield relic |
| paladin_holy | Relic Paladin / Holy | Healer | INT | Flail + shield relic |
| dwarf_miner | Deepdelve Miner | Tank | STR | Pick (axe fallback) |
| ether_golem_tank | Ether Golem / granite | Tank | STR | Heavy fists |
| ether_golem_support | Ether Golem / verdant moss | Healer | INT | Arcane |
| ether_golem_bruiser | Ether Golem / green felfire | Damage | STR | Heavy fists |
| orc_chieftain | Blood-Oath Chieftain | Tank | STR | Axe |
| totemic_behemoth | Elephant/rhino hybrid | Tank | STR | Massive totem |
| drakish_footman | Human sword/shield + timed dragon | Tank | STR | Sword |
| wizard | Cinder Arcanist / damage Wizard | Damage | INT | Arcane |
| troll_berserker_melee | Red-Moon Berserker / melee | Damage | AGI | Dual axes |
| troll_berserker_ranged | Red-Moon Berserker / thrown | Damage | AGI | Thrown dual axes |
| dryad | Thornweave Dryad | Healer | INT | Staff |
| whisp | Lantern Whisp | Healer | INT | Arcane |
| evergrove_centaur | Evergrove Centaur | Healer | INT | Grove javelin |
| keeper_of_light | Keeper of the Light | Healer | INT | Lantern staff |

The Drakish proposal explicitly provides a timed dragon transformation, exactly
two cleaving attacks, then a weak fireball aimed at the furthest valid enemy to
gain ranged threat. Its minor burn is not intended as a high-damage spell. This
sequence remains marked planned until its gameplay state machine exists.

Base attributes preserve the current 20 primary / 10 secondary values. No
per-point stat formula or progression cap is changed. Basic range is explicitly
220 cm for melee, 1500 cm for bows/thrown axes, 1300 cm for ranged lances,
and 1200 cm for magic ranged profiles; base attack period is 1.5 s.
The Lancer retains the existing ranged lance implementation. `threatRole` selects
the central tank/damage/healing threat policy; it does not duplicate its numeric
multiplier. A damage golem using tank-shaped prototype art remains a damage role.

`runtimeArchetype` is a compatibility fallback for existing code and geometry.
Gameplay uses the explicit primary, threat role, basic range and attack timing.
Family and variant keep Paladin, Golem, Wizard and
Troll class-selection choices grouped without conflating their roles.

All initial art entries say `prototype_fallback`. Art family/provenance describes
the requested dark-fantasy direction, not a claim that a finished, rigged,
imported or approved asset exists. Generated art requires a separate asset
manifest, visual review and verified import before updating that status.

## Loading and editing

Edit the JSON directly. `Tools/SeedChampionRoster.py` is the initial authoring
seed and refuses to overwrite existing data unless `--overwrite` is explicit.
It does not alter the user's source file or live Astra library.

`CireChampionRoster::All`, `Find`, `FindByIndex` and `Count` lazily load the file;
`Reload` validates a complete replacement before swapping it. Invalid reloads
retain the last valid roster. An invalid first load returns an empty roster;
the caller should retain the existing five-class selection fallback. Pointers
returned by lookup should not be held across `Reload`.

Schema 1 requires the exact Unreal 5.8.3 profile and provenance. The native parser
rejects unknown fields, duplicate IDs/roles/skill slots, non-finite or out-of-range
numbers, unknown attack styles, missing fields, nonempty starting skills, more
than 64 profiles and files over 512 KB. Each profile requires exactly six active
draft examples plus distinct passive/ultimate IDs. Text lengths and identifiers
are bounded. Skill status and VFX delivery are controlled values; VFX family is
an identifier for visual authors. `RunValidationSmoke` checks valid loading,
transactional rejection and enforcement of empty starting skills.

Run `python Tools/TestChampionRoster.py` for content checks against the actual
native skill pool, role variants, authored Drakish sequence and source hash.

## Runtime integration

`ACireHero::DraftProfile(Id)` selects a validated profile on authority and copies
its gameplay values to replicated fields. The controller exposes the reliable
`ServerDraftProfile(Id)` RPC, bounded to 64 characters, the owning pawn and known
IDs; redrafting is rejected. The existing `Draft(0..4)` keys still map to Knight,
Ranger, Scholar, Lancer and Summoner, independent of JSON order. If the catalogue
cannot load, these five legacy choices remain available.

The roster browser displays six large cards per page in a 3-by-2 grid. The 22
profiles span four pages. Click a card or press its visible 1-6 key; Left/Right,
PageUp/PageDown and the Previous/Next buttons browse pages. The first five retain
their original order on page one, followed by Bear. Each card shows the authored
role, primary, three base attributes and basic attack style, range and period.
No planned skills appear as selectable abilities. The native options gallery
checks every page/key mapping and captures a page with the new role variants.

`PrimaryStat`, `PrimaryAttribute`, `BasicAttackRange`, `BaseAttackSeconds`,
`BasicAttackStyle`, `IsRangedBasicAttack`, `HasChampionRole` and
`DamageThreatMultiplier` are the gameplay accessors. The selected snapshot keeps
client tooltips and server combat consistent even if client JSON differs, and
catalogue reloads do not silently change an existing champion. Health, mana,
primary damage, level gains, gear stat bonuses and attack-speed scaling retain
the existing per-point formulas. Base timing is divided by the existing agility
and battle-rhythm multipliers. Tank damage threat still uses central tuning;
healing threat retains the shared effective-healing policy.

Current profiles author contact attacks at 220 cm and targeted projectiles at
750 cm. The runtime treats ranges above 300 cm as ranged; summoned units use
their own explicit range. Range/primary/body are independent, so the melee Troll
and Holy Paladin use contact attacks and their authored AGI/INT. Style drives the
attack cue and combat label. Bow/lance retain their modeled projectiles; other
ranged styles currently use the existing arcane projectile mesh as a visible
fallback, including the ranged Troll until the axe projectile art is connected.
Body and weapon animation art still use their recorded fallback archetype.

`LoadThematicBuild()` is an authoritative developer-only fixture helper for the
first five profiles. It transactionally verifies all six active, passive and
ultimate IDs against the native pool and their slot kinds before replacing the
build. Shipping builds return false. It has no client RPC, is never called by
normal drafting, and never loads planned skills. `CireChampionProfiles::RunSmoke`
checks all 22 runtime drafts, primary growth/damage, range/timing/style, empty
starting builds, the restricted fixture loader, role-based threat and numeric
draft compatibility. Run it through the root gameplay probe after rebuilding;
Python content checks alone do not prove native or replicated runtime behavior.
