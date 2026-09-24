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

| Profile ID | Character / selection | Primary role | Hybrid roles | Primary | Basic style | Difficulty |
|---|---|---|---|---|---|---|
| knight | Iron Warden / Crusader Knight | Tank | - | STR | Sword | 1 |
| ranger | Ash Ranger / Waystalker Archer | DPS | - | AGI | Bow | 2 |
| scholar | Veil Scholar / Veiled Healer | Support | - | INT | Arcane | 1 |
| lancer | Dusk Lancer / Skirmisher Lancer | DPS | - | AGI | Lance | 2 |
| summoner | Rift Summoner / Rift Binder | DPS | - | INT | Arcane | 3 |
| bear | Gravewood Bear / Elderhide Guardian | Tank | - | STR | Claws | 1 |
| paladin_righteous | Relic Paladin / Relic Guardian | Tank | - | STR | Flail + shield relic | 2 |
| paladin_holy | Relic Paladin / Relic Mender | Support | - | INT | Flail + shield relic | 2 |
| dwarf_miner | Deepdelve Miner / Deepdelve Warden | Tank | - | STR | Pick (axe fallback) | 1 |
| ether_golem_tank | Ether Golem / Granite Construct | Tank | - | STR | Heavy fists | 1 |
| ether_golem_support | Ether Golem / Verdant Construct | Support | - | INT | Arcane | 2 |
| ether_golem_bruiser | Ether Golem / Felfire Construct | DPS | Tank | STR | Heavy fists | 2 |
| orc_chieftain | Blood-Oath Chieftain / Clan Warlord | Tank | Support | STR | Axe | 2 |
| totemic_behemoth | Totemic Behemoth / Ancestral Behemoth | Tank | - | STR | Massive totem | 3 |
| drakish_footman | Drakish Footman / Dragon-Pact Footman | Tank | - | STR | Sword | 3 |
| wizard | Cinder Arcanist / Battle Mage | DPS | Support | INT | Arcane | 2 |
| troll_berserker_melee | Red-Moon Berserker / Blood Berserker | DPS | - | AGI | Dual axes | 2 |
| troll_berserker_ranged | Red-Moon Berserker / Red-Moon Axe-Thrower | DPS | - | AGI | Thrown dual axes | 2 |
| dryad | Thornweave Dryad / Grove Tender | Support | - | INT | Staff | 2 |
| whisp | Lantern Whisp / Lantern Spirit | Support | - | INT | Arcane | 3 |
| evergrove_centaur | Evergrove Centaur / Spring Warden | Support | DPS | INT | Grove javelin | 2 |
| keeper_of_light | Keeper of the Light / Lantern Keeper | Support | - | INT | Lantern staff | 3 |

Buckets: 8 tanks, 7 DPS and 7 supports by primary role, plus four hybrids that
also appear (flagged HYBRID) in their second role's draft column. The primary
role is the profile's `threatRole` (tank / damage / healer -> Tank / DPS /
Support); every other entry in `roles` is a hybrid role. `healer` and `support`
both map to the Support bucket. Hybrid roles widen the skill pool (see
[RoleDrafts.md](RoleDrafts.md)) but never change threat policy, which follows
the primary role only.

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

Draft-screen presentation fields are optional and validated when present:
`classType` (<= 48 chars, defaults to `variant`), `difficulty` (1..3, default 2)
and `lore` (one line, <= 200 chars). Every current profile authors all three.

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

### Champion draft screen

`ACireHUD::DrawDraftRoster` (Source/CiresTeamSurvival/CireRosterHUD.cpp) is a
DOTA-style full-screen draft:

- **Role columns.** TANK / DPS / SUPPORT, each a grid of portrait tiles. A
  champion appears in its primary column; hybrids are repeated in their other
  column with a HYBRID tag, and every tile carries colored pips for the other
  roles it fills. Each column footer lists its role skill pool (counts,
  role-only skills, cross-role skills, universal skills) straight from the
  native rules tags, so the role -> skills link is visible before picking.
- **Portraits** are bust renders of the real champion meshes (body, weapons,
  scale), generated by `python Tools/RunDraftPortraits.py` and imported as
  `/Game/UI/Draft/Portraits/T_Portrait_<id>` (512 px, mipmapped UI textures).
  Missing textures fall back to a role sigil with initials. Re-run the tool
  after body/art changes; `--ids a,b` limits it, `--import-dir` reimports.
- **Live preview.** Hover (0.12 s settle) or selection shows the actual unit:
  `ACireDraftStage` spawns a local, non-replicated, tick-disabled `ACireHero`,
  binds it with the normal `DraftProfile` path (so ChampionArt applies exactly
  the in-game body, weapons, creature art and idle animation), and renders it
  with a SceneCapture2D (show-only list, map sun/sky/fog excluded) on a stone
  dais under a voussoir arch between two braziers, with key, cool rim, fill and
  fire lights. The unit turns slowly and plays its weapon attack every ~7 s.
  Framing uses the head bone (or tight bone/static bounds for creatures). The
  stage lives 900 m above the map, destroys itself when the screen closes, and
  only exists on clients.
- **Details.** Primary + hybrid role chips, class caption, lore line, primary
  attribute with STR/AGI/INT bars, difficulty pips, basic attack (melee/ranged,
  style, range, period) and the signature kit: six actives, passive and
  ultimate with icon, one-line description, PASSIVE / ULTIMATE / PLANNED tags
  and a full tooltip. The kit note states which role pool(s) the champion
  actually drafts from.
- **Team.** Teammates' picks (portrait, champion, LOCKED IN / BOT PICK /
  PICKING) and a team lock counter. A champion locked by a *human* teammate is
  shown LOCKED and cannot be locked again (also enforced server-side in
  `ServerDraftProfile`); bot picks are shown but never block.
- **Input.** Mouse: click selects, double-click locks. Keyboard: arrows move the
  grid cursor (Left/Right arrive via the controller's existing
  `ChangeDraftRosterPage`), 1-6 select within the focused column, Home returns
  to the first tile, Space locks in. Locking always sends the existing reliable
  `ServerDraftProfile(Id)` RPC. Enter still opens chat.

`DraftRosterIdForSlot` / `DraftRosterPageCount` / `DebugDraftRosterPage` keep
their index mapping for the options gallery and probes.

Verification: `python Tools/RunDraftGallery.py` renders six 1920x1080 states
(tank/DPS/support hovers, hybrid selection, large tank, small spirit) with a
fixture team (human-locked Dryad, two bot picks, one still picking) into
`Saved/DraftGallery/<stamp>` and checks the PNGs; the images still need a visual
review. `--mannequin` omits `-CireTripoChampions`. Without that flag the game
(and therefore the live preview) shows the fallback mannequin bodies, while the
portrait textures always show the Tripo bodies they were rendered from.

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
