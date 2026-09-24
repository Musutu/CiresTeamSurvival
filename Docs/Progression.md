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

## Team-fair distribution

- **Gold and experience** go to **every** team member (like the existing kill share).
- **Tomes and items are personal** and rotate: each goes to the teammate with the **lowest loot score**
  (value received so far this match: item cost, 60 per tome point); ties break randomly. Items skip
  players whose bag has no room; if nobody has room, the chosen player receives the item's full value
  in gold ("bags full").
- Every teammate gets a toast per line ("Outpost strongbox: Grimoire of Whispers -> you" /
  "-> Dusk 3"), and a coin/chime plays.

This replaces the previous whole-pack reward (flat gold/XP to all plus +1-5 to **all three** stats for
**every** player and a hidden rare-relic CDR bump); the per-kill team share is unchanged.

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

## Decisions for Eric to review

1. Unlock pacing (bay 2 in cycle 2, bay 3 mid-cycle 3, promotions every 2 rounds from round 4, cap 8).
2. Personal-rotation loot versus per-player rolls or need/greed.
3. Chest pickup radius 3.2 m and auto-collect at prep.
4. Drop rates: especially legendary chances (8% elite packs, 12% late bosses).
5. Removing the old +stats-for-everyone pack reward (it was very strong).

## Verification

`Tests/ItemRulesTests.cpp` (loot determinism and rates, fair rotation, schedule) and the in-engine suite
(`CIRE_PROGRESSION_PASS`: gating per round, deeper bays farther from town, leaders, pause/resume
timers, prep freeze, chest spawn/auto-collect, team-fair distribution, tier tables).
