# RESUME: feat/bonus-loot (Playtest 6, section E)

Worktree `F:\CiresTeamSurvival-agents\cts-bonus-loot`, ports 17540-17549. The spec is
`Docs/EricFeedback/2026-09-27/PLAYTEST-6.md` section E. The design for Eric is in `Docs/Items.md`, under
"Bonus Loot Stages and PvP uniques".

## Done
- **Stage replaces a wave.** The roll lives in waves-modes' `CireWaveDirector::RollWaveType`, which calls
  `RollBonusStage` and returns the stage wave. StartWave applies the per-cycle cap, the developer force, and
  `CireBonusStage::Begin`. The earlier temporary hook was removed when feat/waves-modes merged into main.
  - The roll never replaces a boss wave or a wave with a boss row.
  - Chance is `Waves.json bonusWave.replaceChance` 0.08, from `fromWave`, at most `maxPerCycle`.
  - The roll is deterministic per match seed and wave.
  - `ForceNextBonusStage(Mode, tier)` is there for tools and tests.
- **Behaviour** (`CireMonsterExpansion`).
  - Creatures run the route to the castle, never attack and never cost lives.
  - A creature bolts *ahead* along the path, away from champions.
  - The escape clock starts on the first hit (`OnBonusCreatureAttacked`, via `CireWaveDirector::OnMonsterDamaged`).
    It is doubled from 26 to 52 s. Reaching the castle is also an escape.
- **Tiers** (`CireBonusStage.*`, `LootTables.json bonusStage`, reloaded from F8 > Economy).
  - The tier roll is Low 70 / Mid 25 / Rare 5.
  - Each player rolls a personal outcome in the tier:
    - Low: x2 wave gold, 2 components or 2 consumables/tomes.
    - Mid: x5 wave gold, 1 shop item worth 350-500 g, or 1 PvP unique.
    - Rare: up to 3 chests, each 100 g or a free skill point.
  - A lane pays when its hoard is caught or escaped. Payouts scale with the catch share.
  - Chests land at the player's feet. Bots auto-loot.
- **Free skill point.**
  - `LootBundle.SkillPoints`, `LootKind::SkillPoint` (a `skillPoint` loot-table key) and a replicated
    `UCireInventory::FreeSkillPoints`.
  - `CireSkillShop` prices are 0 while a point is held, and buying or levelling spends it.
  - The Skill Shop card says "FREE", and the loot window shows a violet line.
- **PvP uniques** (`Content/Data/PvPUniques.json` plus `CireItemsPvP.*`).
  - 10 legendary, unique, `purchasable:false` items worth 410-450 g.
  - They are merged into the item catalog at load. A bad file only drops the PvP items.
  - Each has one effect that works only champion vs enemy champion. The hooks sit in the CireItems combat hooks and
    `MoveSpeedMultiplier`.
- **Banner and announcement.** "BONUS LOOT STAGE | MID TIER | ...". The client banner reads the tier from the
  replicated announcement.
- **Tests.**
  - The stage runtime block is in `CireMonsterExpansionTests.cpp`.
  - `CireBonusStageTests.cpp` covers the data, rolls, rewards, free point and all 10 PvP effects.
  - Both run inside `RunExpansionChecks --only native`. Log markers: `CIRE_BONUS_LOOT_PASS`, `CIRE_MONSTER_EXPANSION_PASS`.
- **Docs.** `Docs/Items.md` (new section and decision 7) and a note in `Docs/MonsterExpansion.md`.

## Not done / next
- Main (with feat/waves-modes, 13b101ed) is merged in.
- The PvP uniques have no painted icons yet (procedural placeholders), pending the ChatGPT icon pass.
- "Include some other special wave types" (raw notes) is left to feat/waves-modes (section C).
- The F8 wave editor has no field yet for `replaceChance` / `escapeTimerOnHit`; they are JSON-editable and saved by the
  Waves.json writer.

## Assumptions / questions for Eric
1. **"Maintain their behaviour" = the treasure behaviour.** Creatures never attack and bolt from champions, but they
   now always head for the castle. If you meant "fight like the wave they replaced", that's a one-line switch in
   `CireMonsterExpansion::TickSpecial`.
2. **One outcome per player per stage**, rolled inside the tier. We don't grant all three lines of a tier at once.
3. **Mid PvP unique is per player** (personal-loot ruling), not one item per team.
4. **Wave value** = the gold the replaced wave's kills would have paid each player.
5. **Catch share.** Gold and Rare chest count scale with the share caught. Item outcomes need 50% caught, otherwise
   the tier's gold is paid.
6. **Kill bounty** for stage creatures went from 4 to 1 mob value, because the stage chest is the reward.
7. **The old breather bonus wave** is kept (`chance` 0 = off) for tools and tests.
8. **PvP effect numbers** (all in the JSON): +12% dmg, -12% taken, 40% heal cut 3 s, 1 s silence / 15 s, +20% under 35%,
   -60% first spell / 20 s, 25% barrier under 30% / 60 s, 15% reflect, 1.5 s slow / 4 s, +15% speed 2 s.
   Lifesteal was avoided per the items ruling.

## Shared-file edits (keep small on merge)
- `CireWaves.cpp`:
  - `FRuntime::ForceStage`.
  - `RollWaveType`: waves-modes' hook, now filled in.
  - StartWave: the cap, the force and `Begin` around the `RollWaveType` call. `QueueWave` gets `bBonusStage`.
  - Announcement.
  - `RollBonusStage` / `ForceNextBonusStage`.
  - `CireBonusStage::Tick` in `TickSurvival`.
  - `OnBonusCreatureAttacked` in `OnMonsterDamaged`.
  - Breather announcement text.
- `CireWaves.h`: `FCireBonusWaveRules` gains `ReplaceChance` and `bEscapeTimerOnHit`, and the defaults change
  (Chance 0, EscapeSeconds 52). Two declarations are added.
- `CireWaveData.cpp`: equality, clamps, JSON read/write for the 2 new keys, and the comment string.
- `Content/Data/Waves.json`, `bonusWave` block: chance 0, replaceChance 0.08, escapeTimerOnHit, escapeSeconds 52,
  bounty 1.
- `Content/Data/LootTables.json`: new `bonusStage` block (additive).
- `CireItems.h/.cpp`:
  - `FreeSkillPoints` UPROPERTY + DOREPLIFETIME.
  - PvP merge in `Reload`.
  - 5 one-line PvP hook calls.
- `CireSkillShop.cpp`: free point in `BuyPrice` / `LevelPrice` / `BuyBlocker` / `Buy` / `LevelUp`.
- `CireShopUI.cpp`: "FREE" price label and the skill-point loot line.
- `Rules/CireItemRules.h/.cpp`: `LootKind::SkillPoint`, `LootBundle::SkillPoints`.

## Gate logs
All three gates were run on 2026-09-28, after merging main (with feat/waves-modes, 13b101ed). Build: Result: Succeeded
(`Saved/build7.log`).

- **Native: PASS.** `Saved/ExpansionChecks/20260928T063337316835Z/report.json`.
  - `CIRE_BONUS_LOOT_PASS checks=95 pvp_uniques=10`.
  - `CIRE_MONSTER_EXPANSION_PASS checks=492`.
  - `CIRE_COMBAT_EXPANSION_PASS`.
- **Network: PASS.** `Saved/NetworkSmoke/20260928T063649850879Z/report.json`.
- **Interface: PASS.** `Saved/InterfaceSmoke/20260928T063816118011Z/report.json`.

Environmental notes:
- Earlier "native probe timed out after 240 s", network and interface timeouts came from the cold asset-registry scan
  (Paragon, then Polyphoria). A single native run with `--timeout 900` warmed the cache, and later runs finished in time.
