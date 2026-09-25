# Progression: challenge tiers, bonus drops, prep pause (design proposal)

Status: engineering prototype on `feat/progression-shop`, 24 September 2026. Numbers are proposals for
Eric to review; they live in `Content/Data/LootTables.json`. Code: `CireLoot.*` (drops, chests,
distribution, pack schedule, NPC pause), rules in `Rules/CireItemRules.*`. Items and the shop:
[Items.md](Items.md).

## Challenge packs are progression content

Packs sit in three **bays** along each lane. Bay 1 is nearest the town road, bay 3 deep along the route
near the monster gate. Bays unlock over the match instead of all being available from the start, so
teams meet harder packs as the game progresses:

| Round (cycle) | Bay 1 | Bay 2 | Bay 3 |
|---|---|---|---|
| 1 | tier 1 | locked | locked |
| 2 | tier 1 | **tier 2 (new)** | locked |
| 3 | tier 1 | tier 2 | **tier 3, appears when wave 2 spawns** |
| 4-5 | tier 2 | tier 3 | tier 4 |
| 6-7 | tier 3 | tier 4 | tier 5 |
| ... every 2 rounds +1 | | | up to tier 8 |

`packSchedule`: `bays[{bay, unlockRound, unlockWave}]`, `promotionStartRound` (4),
`promotionEveryRounds` (2), `maxTier` (8). A bay's tier is its bay number, +1 at the promotion round
and +1 every N rounds after. Tier scales health and damage through the existing
`ChallengeHealthMultiplier/DamageMultiplier` (and the round), and scales rewards below. Every pack keeps
its Pack Leader (NPC data `leaderFromTier`). Packs still refresh each cycle (Design.md rule) and never
block wave completion.

**Announcements:** a new bay or a tier promotion sends every player a "CHALLENGE OUTPOSTS" toast
("NEW CHALLENGE | Tier 2 outpost has appeared midway along the route"); the HUD's own transition banner
("CHALLENGE UNLOCKED, Challenge Tier N") fires when a higher-tier leader appears in your lane.

## Bonus drops

Killing a **whole pack**, a **Pack Leader** or a **lane boss** (Hollow Siegebreaker) rolls a loot table and
drops a glowing **iron-bound chest** where the last unit died:

- Rarity-coloured light pillar, ground halo and point light (gold common, blue rare, purple epic, orange
  legendary); it drops in with a bounce and shows a world label ("Outpost strongbox, Tier 3, walk over to
  loot, 12 m").
- **Auto-pickup:** any living teammate within 3.2 m opens it (lid swings open, light flares, the chest
  fades after 3.5 s). Chests are visible and relevant only to their lane's team.
- **Nothing is lost to phase changes:** when prep begins every unopened chest is auto-collected
  (`pickup.autoCollectOnPrep`).

Contents per entry (independent rolls): **gold**, **experience** (like an EXP tome), **Tome of
Ascendance** points (primary stat; 3+ points is a Greater Tome), and **items** from pools
(consumables, components, epics, rare legendaries).

| Source | Table by tier/round | Highlights |
|---|---|---|
| Pack completion | tier 1-2 `pack_minor`, 3-4 `pack_major`, 5+ `pack_elite` | 45-150 gold, 120-300 XP, tomes 1-5, consumables to legendaries |
| Pack Leader | tier 1-3 `leader_minor`, 4+ `leader_major` | elixirs, attribute components, epics |
| Lane boss | rounds 1-3 `boss_early`, 4+ `boss_late` | 80-190 gold, 250-400 XP, tomes 1-4, components to legendaries |

Scaling (`scaling`): gold and XP +25% per tier above 1; chances +8% (relative) per tier; both then use
the arena-winner loot bonus (`Loot(team)`, 1.0-1.4). Pack leader and completion roll into **one chest**
when the leader is the last kill.

## Personal loot (default since Eric's playtest, 25 September)

Every loot source (whole pack, Pack Leader, lane boss) rolls **separately for each eligible player on the
lane's team**. Each player gets **their own chest**, which:

- replicates **only to its owner** (owner-only relevancy: other clients never receive the actor) and is
  hidden locally for anyone else; only the owner can open it by walking within 3.2 m;
- keeps the rarity-coloured light pillar, halo and world label ("YOUR personal loot | walk over to open");
- shows on the owner's minimap as a small rarity-coloured chest until opened.

**Eligibility (fair rule):** a teammate is eligible if they **damaged any unit of that pack / the boss**
(pets count for their owner), **or** were **alive within 40 m** of the kill (covers healers and tanks
who did not land the damage). Players who helped and then died stay eligible. Everyone else gets nothing
from that source. (`distribution.eligibleRadius`).

**Rates (keeps team totals the same):** gold and XP entries keep their full chance **per player**, since
everyone already received them under the old shared rule. Tome and item chances are multiplied by
`personalItemFactor / eligiblePlayers` (1/5 with a full team), so the **team's expected tomes and items
equal the old single shared roll** (verified natively for 1 to 5 players). Example, Tier 3 pack
(`pack_major`, tier 3): previously one shared roll gave the team about 0.60 tomes + 0.55 items per
clear; now each of five players rolls 0.12 tomes + 0.11 items, the same team total. Each player's gold
(70-100 x1.5 at tier 3) and XP (200 x1.5, 70%) are their own roll.

**Bots** roll their own personal loot and **auto-loot it instantly** (no chest).

**Presentation:** opening a chest opens a WoW-style **Personal Loot window** (source, "why you got this",
every line with icon, rarity-coloured name, stats and destination: "-> bag 3", "Bags full / unique owned:
+160 gold", "+3 Strength (your primary attribute)"). Items **fly from the window into the bag slot**, gold
floats up and ticks, a rarity sound plays, and each item/tome gets an icon toast. The **Loot log** (key
**L**) lists everything received with time and source. Unopened chests are **auto-collected when prep
begins**, with one merged summary window per player ("Auto-collected at prep: 2 unopened personal
chests").

**Server option:** `distribution.mode: "teamRotation"` restores the previous shared chest where tomes and
items rotate to the teammate with the lowest loot score (off by default).

## Prep phase: NPCs pause

Outside the survival phase (prep, arena, recovery) every monster is **paused**: it does not move, attack
or cast, and **all its timers freeze**: ability cooldowns, rally/guard/shield-wall/enrage buffs, kite and
dash timers, slows, taunts and provokes resume with exactly the remaining time when waves resume
(`CireProgression::PauseNPC/ResumeNPC`, shifting every future timestamp by the pause length). Casts in
progress at the moment of a phase change are still cancelled, because the phase change teleports every
hero and clears telegraphs (existing rule). In practice only challenge packs are alive during prep,
since prep starts once the waves are cleared.

## Arena and phase changes

Items, belt charges, elixir timers (server time), teleport cooldowns, tome points and loot scores live on
the hero and survive every phase change, death, arena teleport and reconnect-as-bot. Phase changes end
shop visits (undo history) and cancel teleport channels; lantern wards are removed when the arena starts.

## Gold economy (Eric's ruling, playtest 2)

Kill gold is data (`LootTables.json -> economy`) and live-editable in **F8 > Economy**:

| Kill | Gold | Wave 1 | Wave 6 |
| --- | --- | --- | --- |
| Normal mob | mob value = `mobBase + mobStep x floor(wave / stepEveryWaves)` (1, +1 every 3 waves) | 1 | 3 |
| Armored unit | 2 x mob value | 2 | 6 |
| Boss | 10 x mob value | 10 | 30 |
| Challenge-pack unit | 10 x mob value | 10 | 30 |
| Pack Leader | 100 x mob value (another 10x) | 100 | 300 |

* Wave kills pay **every teammate** the full bounty; pack kills pay every eligible teammate (helped or
  alive within `distribution.eligibleRadius`). Wave units are valued at the wave they **spawned** in
  (`CireWaveDirector::UnitFlags`), not the wave running when they die; the wave's `rewardMultiplier` applies.
* Every bounty floats as "+3g" over the kill (bigger for bosses/leaders, with a toast).
* Loot-table gold is in mob values too (`lootGoldInMobValues`). Item prices were rescaled x1.2.

### Economy curve

`Tools/EconomyCurve.py` prints expected gold per player by wave (start gold 120; loot bonuses and arena
wins left out) against item and skill prices. With the current data:

| After | Waves only | Waves + every open challenge bay |
| --- | --- | --- |
| Round 1 (wave 5) | ~210 | ~340 |
| Round 2 (wave 10) | ~400 | ~1,310 |
| Round 3 (wave 15) | ~690 | ~3,290 |

Items: basics 120-190, epics ~435, legendaries ~490-1,020 total. A team clearing packs buys a core
legendary in round 2 and a full build around round 4; a team that ignores packs stays thin. Skill
prices are in mob values, so they follow the gold available (below).

## Skill Shop (game mode, default)

The progression mode is a server-authoritative, replicated setting on `ACireGameState::ProgressionMode`
(1 = **Skill Shop**, default; 0 = **Classic Draft**, the old level-up skill offers).

* Command line: `-CireMode=SkillShop` or `-CireMode=Classic`.
* F8 > Economy: SKILL SHOP / CLASSIC DRAFT buttons (host or standalone, before the first wave).
* API for a champion-select picker (`CireSkillShop.h`):
  `IsSkillShopMode(World)`, `ModeName(bSkillShop)`, `SetMode(GameMode, bSkillShop, &Why)` (server, before
  the first wave) and, from a client, `UCireInventory::ServerSetProgressionMode(0|1)`, which only the
  host's own player may use (a remote client's request is ignored; covered by the network test).

In Skill Shop mode:

* The free opening role-skill pick stays. Level-ups then only raise stats (+2 primary, +1 other); no skill offers.
* The shop opens by itself (skills tab) **after every cleared wave** during the wave director's breather
  (`Waves.json breatherSeconds`, 15 s; READY UP inside the shop ends it early), and **when prep begins**.
  It is also open during prep and recovery. `K` toggles it; `B` is the item shop.
* It lists every skill the champion can buy: `CireAbilityDB::PurchasableSkills(profile)` (implemented
  skills), or the role pool for heroes without a profile. Buy new skills or level owned ones with **no cap**.
* Per level, casts use `CireAbilityDB::EffectiveStats(Id, Level)` relative to level 1: more effect
  (damage/healing via the combat pipeline), a slightly higher mana/energy cost and a shorter cooldown. The
  hook is in all five cast paths (`CanPayCast` + `ApplyCastLevel`). Levels are stored per skill in the
  replicated `UCireInventory::SkillRanks`. Abilities missing from the database fall back to
  `SkillShop.json -> scaling`.
* Prices (`SkillShop.json`, in mob values, F8 > Economy): active 15 (+25% per owned active), passive 30,
  ultimate 60, level-up 8 x 1.35^(level-1). At wave 7 (mob value 3): an active costs 45-79 g, a level 24 g+.
* Limits keep kits small until late: 2 active slots at the start, +1 every 3 waves (6 at wave 12); the
  passive slot opens at wave 5, the ultimate at wave 10.
* Bots shop every 2 s while the shop is open: an ultimate when its slot opens, then a primary-role
  active, then a passive, spending at most 60% of their gold on a new skill (reserve for items); with
  full slots they level their lowest active.

### Screen

The Skill Shop follows Eric's target image (`Saved/Reference/skills-target.png`): near-black panel with
gold filigree, corner ornaments and diamond-studded dividers, the SKILLS / POWER LIVES WITHIN title in
Cinzel, and three columns: **Golden scrolls = Active**, **plain parchment = Passive**, **prismatic =
Ultimate**, each with its crest, name, keywords and tagline. Every skill is a scroll card of its tier:
icon in a crest ring, name, school and types, level -> next, effect and cost numbers, and the price on
the lower roll. Hover lifts the scroll and shows `CireAbilityDB::Describe`. Buying or levelling stamps
a wax seal on the scroll (flash + sound), then a small scroll flies to the skill bar. Unaffordable or
locked scrolls are dimmed with a red ribbon (NEED 9g, SLOT AT WAVE 10, SLOTS FULL, BETWEEN WAVES).
The item shop (**Armory**) uses the same framing: recommended build per role first (starter -> core ->
situational), all items under ALL ITEMS as tier-coloured cards, larger text, same buy/sell feedback.

### Playtest 3 update: readability, Ready to Continue, sections, Polymorph

* **Readability:** scroll cards are 150x198 (never shrunk), icons 60 px in the crest ring, 11 pt names,
  high-contrast ink on a cream wash. A card shows only name, level, up to two affinity tags, one key-number
  line and the price; the full level -> next numbers are in the hover tooltip (`CireAbilityDB::Describe`).
  With many skills, rows scroll (mouse wheel or the arrows); verified at 1920x1080, 1600x900 and 1280x720.
* **Sections ("periodic table"):** labelled, coloured, bordered blocks: Offensive: Spell Damage, Offensive:
  Attack Damage, Defensive, Crowd Control (stuns, silences, slows, roots, polymorph), Summons, Constructs,
  Passives, Ultimates. Filter chips show or hide groups (the first click isolates one). Each ability has one
  primary `section` and up to four `effectTags` ("Stun", "Slow", "Heal", "Summon"...), derived by
  `Tools/BuildAbilityDB.py` `classify()` from its CC effects, damage/heal wording, summon and construct data.
  Rows that preset `section` / `categories` / `effectTags` (for example the dodge-roll skills) keep them.
* **Ready to Continue (Skill Shop mode only):** after a cleared wave the next wave **waits until every
  human presses READY TO CONTINUE** (bots auto-ready). The Skill Shop shows each teammate's portrait with a
  check mark and "WAITING FOR 2 / 5 PLAYERS"; the match plate says the same. The AFK safety cap is
  `SkillShop.json -> readyGate.maxSeconds` (180 s, also in F8 > Economy; 0 = none); its countdown appears
  in the last 30 s. The gate is `CireSkillShop::HoldBreather`, called from the survival tick (`CireMatch.cpp`)
  on top of the wave director's breather/ready-up; Classic Draft keeps the timed breather with early ready-up.
* **Polymorph** (`polymorph`, Crowd Control active, arcane, caster DPS and supports, 1.5 s cast, 50 mana,
  20 s cooldown): turns the target into a Chicken, a Piglet or a Frog (random per cast) for 8 s (+ per level,
  capped at 12 s). The critter cannot attack or cast, loses its threat table and wanders slowly; any damage
  breaks it. Lane bosses and Pack Leaders are immune, elites get half, champions (PvP) at most 3 s with the
  crowd-control diminishing returns. It shows a poof cue and the `polymorphed` buff row ("Polymorphed").
  Code: `CirePolymorph.*`. Critters are CC0 Quaternius models (Art/Creatures/Free/PROVENANCE_Critters.md,
  `Tools/ImportPolymorphCritters.py`); the Piglet reuses the imported Pig. Icon id for the 2D art agent:
  `polymorph` (`/Game/UI/Abilities/T_polymorph`); a procedural placeholder is drawn until it exists.

## Decisions for Eric to review

1. Unlock pacing (bay 2 in cycle 2, bay 3 mid-cycle 3, promotions every 2 rounds from round 4, cap 8).
2. Personal loot eligibility (damage or within 40 m) and the 1/eligible item share; need/greed is not implemented.
3. Chest pickup radius 3.2 m and auto-collect at prep.
4. Drop rates: especially legendary chances (8% elite packs, 12% late bosses).
5. Removing the old +stats-for-everyone pack reward (it was very strong).
6. Economy: packs dominate income (a Pack Leader is 100 mob values, paid to every eligible teammate).
   Teams that skip packs stay poor; lower `packLeaderMultiplier` if that is too swingy.
7. Skill prices and slot pacing (above); auto-open only when something is affordable.
8. The breather is the wave director's 15 s; the previous 20 s change in this branch was reverted.

## Verification

`Tests/ItemRulesTests.cpp` (loot determinism and rates, fair rotation, schedule) and the in-engine suite
(`CIRE_PROGRESSION_PASS`: gating per round, deeper bays farther from town, leaders, pause/resume
timers, prep freeze, chest spawn/auto-collect, team-fair distribution, tier tables).
`CIRE_SKILLSHOP_PASS` (`CireSkillShopTests.cpp`, 48 checks): bounties per wave/armored/boss/pack/leader,
mode switch and lock, Classic still offering skills, buy/level (gold, gating, no cap, rejections),
access windows, level-3 casts costing more with a shorter cooldown and dealing more, bots, role builds.
The network probe buys and levels a skill through the Server RPCs on a remote client. The gallery
(`--only gallery`) captures the Skill Shop and Armory at 1920x1080 and 1600x900, the purchase moments,
and checks the shop auto-opens after a cleared wave; `Tools/ComposeShopCompare.py` puts the captures
next to the target image.
