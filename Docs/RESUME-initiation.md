# RESUME: feat/initiation (Playtest 6, section L)

Worktree `F:\CiresTeamSurvival-agents\cts-initiation` (branched from feat/ability-expansion 0a08be16, main merged), ports 17620-17629.

## Done
1. **Initiation Skill Shop group** (new section `initiation`, orange "INITIATION · TEAMFIGHT ENGAGE" band in the
   periodic table, `CireShopUI`). 12 PvP engage spells, rows in `Content/Data/AbilitiesExpansion.json`
   (`Tools/BuildAbilityExpansion.py`, list `INITIATION`), gameplay in `CireAbilityExpansion.cpp`:
   | Spell | Delivery | What it does |
   |---|---|---|
   | Tidal Ravage | nova 7 m | knock-up + 1.5 s stun (Ravage) |
   | Reverse Polarity | nova 5.5 m | yanks enemies to you + 1.25 s stun |
   | Vacuum Rift | ground circle | after 0.5 s drags everyone to the centre + 0.6 s stun (Vacuum) |
   | Black Hole | ground zone 3 s | continuous pull to the centre, 50% slow, DoT |
   | Chronofield | ground circle | 2 s stun on everything inside (time-stop) |
   | Warpath Charge | charge to target (14 m) | gap-close + 1 s AoE stun on arrival |
   | Titanfall Leap | leap 9 m | knock-up + 1 s stun on landing |
   | Challenger's Roar | nova 4 m | 2.5 s taunt (Berserker's Call) |
   | Hallowed Cage | wall ring 4 m, 3.5 s | 12 wall segments trap enemies (allies pass), slow inside (Kinetic Field) |
   | Upheaval | ground line 10 m | knock-up line + 0.8 s stun |
   | Soul Hook | hook lane 11 m | first enemy struck, dragged to you, 0.5 s stun |
   | Echo Slam | nova 5.5 m | +20% damage per extra enemy caught, 0.5 s stun |
   Each: true telegraph (`DescribeShape`), PRIMARY scaling, Lv15 bonus, A-rated non-swirl Fab signature (recoloured on
   reuse), icon (procedural), sound row, sold to every role-matched champion (outside the expansion pool cap).
   Cast times: none authored; feat/casting-rules' CastRules.json applies when it merges.
2. **Team synergy: Set-up** (`CireInitiation.cpp`): every initiation hit marks the victim `xp_setup` for 3.5 s (x CC
   scale). While Set-up, the initiator's team deals **+15%** damage to it, **+25%** with area damage (zones, novas,
   circles). Readable: debuff visual (overhead crown glyph, chevron ring, embers; `BuffVisuals.expansion.json`), a
   modifier row for the tooltip/icon, and an orange **"SET UP!"** floating callout (new `ECireHitOutcome::SetUp`, always
   shown, at most once per second per target).
3. **Blink Dagger** (Items.json `blink_dagger`, component `phase_shard`, 650 g total, unique): blink up to 12 m toward
   the cursor, 14 s cooldown, disrupted 3 s by enemy champion damage (debuff `xp_blink_locked`). Sold by all three
   merchants (Vendors.json `shared` += `blink`). Icons `T_Item_blink_dagger` / `T_Item_phase_shard`, lightning blink
   VFX out/in, arcane blink sound. Docs/Items.md "Blink Dagger".
4. **Bots**: initiation spells are in every role-matched champion's catalogue, so Skill Shop bots can buy them; bots only
   cast one when 2+ enemies are within range (`BotWantsCast`). **Blink Dagger: bots skip it** (not in any recommended
   build; a bot blink-engage needs target-selection AI).
5. Tests: `CireInitiation::RunSmoke` (84 checks: data, Set-up amp + area amp + callout, vacuum pull, hook, charge, cage,
   bot engage rule, Blink Dagger catalogue/vendors/blink/range cap/cooldown/lockout) + the expansion smoke now covers
   123 abilities.

## Gates (after merging main 44a308df)
- Build: Succeeded.
- Native: PASS (`CIRE_INITIATION_PASS checks=84 spells=12`, `CIRE_ABILITY_EXPANSION_PASS checks=2767`,
  `CIRE_COMBAT_EXPANSION_PASS`) - `Saved/ExpansionChecks/20260928T062224220507Z/report.json` (`--timeout 600`).
- Network smoke: PASS - `Saved/NetworkSmoke/20260928T062634864615Z/report.json`.
- Interface smoke: PASS - `Saved/InterfaceSmoke/20260928T062752216845Z/report.json`.

## Assumptions / questions for Eric
- Set-up numbers (+15% / +25% area, 3.5 s) and the 3 s blink lockout are defaults; tune in `CireInitiation.cpp`
  (constants at the top) - tell me if you want them in a JSON tunable.
- Initiation spells are regular actives (not ultimates) with 18-32 s cooldowns, so a champion can carry one next to its ultimate.
- Blink Dagger stats follow the items rule (primary + flat mana only); 650 g sits between a basic and a mid legendary.
- Summon/construct damage does not disrupt the dagger (only champions), matching Dota's "hero damage".
- "SET UP!" uses the resist sound cue; a dedicated sound can be added to AudioEvents `outcomes`.

## Shared-file edits
- `CireCombatEvents.h/.cpp` (`ECireHitOutcome::SetUp`, text), `CireHUDWow.cpp` (SET UP! always shown, orange).
- `CireItems.h/.cpp` (`ServerUseAt`, Blink precondition + effect), `Rules/CireItemRules.h/.cpp` (`EffectKind::Blink`).
- `CireController.cpp`, `CireHUDActionBars.cpp` (item keys send the cursor aim).
- `CireShopUI.cpp` (Initiation section), `CireSkillShopTests.cpp` (valid sections), `CireBuffs.cpp` (known ids),
  `CireCombatExpansionProbe.cpp` (runs the smoke).
- Data: `Items.json` (+2 items), `Vendors.json` (`shared` += `blink`). Tools: `BuildItemIcons.py` (2 icon designs).
- Next (after feat/casting-rules merges): expansion Construct / Wall / Summon / Cage placement to `CireSkillCasting::PlacementAim`.
