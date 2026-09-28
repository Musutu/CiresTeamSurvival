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

## Final pass (main e5d261a7 merged: casting-rules, ability-expansion, waves-modes, pack-formations, arena-flow, blender-rig)
- **Placement ignores clipping:** expansion and initiation Construct, Wall, Summon and Hallowed Cage placement now go
  through `CireSkillCasting::PlacementAim` (casting-rules), like the kit and signature skills.
- **Live-tunable numbers:** `Content/Data/Initiation.json` (console `cire.ReloadInitiation`):
  `setUp.seconds / teamDamageBonus / areaDamageBonus / calloutIntervalSeconds` and
  `blinkDagger.range / cooldown / lockoutSeconds / minDistance`. The Blink Dagger's Items.json `use` block keeps the
  catalogue text; Initiation.json wins at runtime (range, cooldown applied after a use, lockout). `CireInitiation::Tuning()`
  exposes them for the Ability Tuner.
- Cast times: the 12 initiation spells now get their cast times from CastRules.json (CIRE_CAST_RULES_PASS checks=850).

## Gates (final, after merging main e5d261a7)
- Build: Succeeded.
- Native: PASS (`CIRE_INITIATION_PASS checks=85 spells=12`, `CIRE_ABILITY_EXPANSION_PASS checks=2767`,
  `CIRE_CAST_RULES_PASS checks=850`, `CIRE_COMBAT_EXPANSION_PASS`) - `Saved/ExpansionChecks/20260928T064607517926Z/report.json`.
- Network smoke: PASS - `Saved/NetworkSmoke/20260928T065011710309Z/report.json`.
- Interface smoke: PASS - `Saved/InterfaceSmoke/20260928T065136364296Z/report.json`.

## Assumptions / questions for Eric
- Set-up numbers (+15% / +25% area, 3.5 s) and the Blink Dagger numbers (12 m, 14 s, 3 s lockout) are defaults in
  `Content/Data/Initiation.json` (live reload: `cire.ReloadInitiation`).
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
