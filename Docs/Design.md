# Cire's Team Survival — design contract

Status: first playable design, 23 September 2026. This document defines intended behavior; it is not a claim that every system below has been implemented. The user selected **5 versus 5** and requested the most recent Warcraft III Reforged Enfo version as reference. Unreal Engine **5.8.3** was verified locally by the coordinating agent. Final platform, minimum PC specification, match length, content count, and the exact reference binary remain open; current release findings are in `EnfoParity.md`.

The initial source build uses engine mannequins, a battlefield assembled from primitive geometry, and debug combat effects. Arena battles now use a pool of six themed, mirror-symmetric arenas (The Sunlit Fields, The Black Shore, Redrock Canyon, Hornbeam Glade, The Drowned Sanctum, Star Station Hangar); one is picked at random each arena phase, never the same twice in a row, and the original court remains as the fallback (see `Arenas.md`). They are playable prototype spaces, not final AA art. The playable prototype archetypes are Iron Warden (STR 20 / AGI 10 / INT 10), Ash Ranger (10 / 20 / 10), and Veil Scholar (10 / 10 / 20); future original Tripo hero briefs are separate art work.

## Identity and player promise

Ten players defend two separate battlefields against escalating monsters. Each team balances lane defense, optional challenge packs, and preparations for recurring arena battles. Losing the home defense ends the match; winning arena battles improves the next survival period. The combat uses a third-person camera, target selection, movement, cast times, interrupts, and tank / damage / healing cooperation. Visual direction is original, realistic human dark fantasy: believable bodies, worn materials, clear silhouettes, and readable spell effects.

Enfo supplies the survival competition concept. WoW and MOBA combat supply interaction references. TFT supplies the presentation reference for randomized choices. These references do not imply reusing their characters, models, icons, animations, audio, map layouts, or ability names.

## Rules explicitly requested

| System | Contract |
|---|---|
| Win condition | Outlast the opposing team until its defense is overrun. |
| Challenge packs | Optional monster groups along the route toward the wave source. Higher difficulty gives better rewards, including a chance at rare equipment or Greater Stat Tomes. |
| Portal cycle | Clear three PvE waves, spend one minute preparing in town, fight in a random arena, then take a 15-second recovery break before the next PvE cycle. The user replaced the original five-minute timer with this wave-driven flow. |
| Arena rewards | Winning gives a team buff and improves loot. Both PvE and PvP competence matter. |
| Champions | Human, realistic presentation. A champion starts with a basic attack and damage type, without innate combat skills. |
| Skill choices | At each three-level breakpoint, show four randomized skills and choose one. Before a passive is selected, each offer has one or two passives. After one is selected, future offers have none. |
| Skill limit | One innate basic attack, six acquired active abilities, one acquired passive, and one acquired ultimate. The eight learned slots are separate from basic attack. This is the user's latest confirmed build. |
| Level growth | Each gained level adds +2 to the primary attribute and +1 to each other attribute. |
| Attributes | STR grants 25 HP per point; INT grants 30 MP per point; AGI grants 1 percentage point of attack-speed bonus per point. Every primary-attribute point also grants one basic-attack damage. |
| Other resources | HP, MP, Energy, and a separate pure cooldown-reduction stat. No additional flat damage growth simply for gaining a level. |
| Later mode | Single Draft offers one random STR champion, one AGI champion, and one INT champion; the player chooses one. The initial selector contains three fixed prototype archetypes and is not yet the random Single Draft mode. |

## Prototype assumptions requiring playtest confirmation

These are defaults to make development concrete, not extra user requirements. Keep all values in tuning data.

1. The user confirmed **six active abilities plus one passive plus one ultimate**, all acquired, alongside a separate innate basic attack. Movement, targeting, and town recall are controls rather than acquired skills.
2. The first offer arrives at level 3, followed by 6, 9, 12, 15, 18, 21, and 24. Levels 1–2 therefore use basic attacks only. Early waves must be survivable without a healer spell. The implementation stops unlocking skills when all eight learned slots are filled; stats continue to grow. Offering upgrades or replacements at later breakpoints remains a future proposal.
3. A cycle means **three cleared PvE waves**, then 60 seconds of town preparation, then an arena capped at 90 seconds, then 15 seconds of recovery. Three waves and 15 seconds of recovery were confirmed by the user. A wave clears only when its ordinary advancing monsters in both lanes die or leak. Optional challenge packs never block the cycle. There is an eight-second breather between cleared waves and a ten-second initial warmup after the solo draft; recovery ends directly in the next cycle's first spawn.
4. The user confirmed **100 defense lives per team**. A normal monster reaching the city removes one life; a boss removes ten. Both are consumed on leakage, and lives clamp at zero. Reaching zero ends the match immediately. The prototype places one Hollow Siegebreaker with the normal escorts on each cycle's final wave. Its schedule and combat tuning remain playtest defaults. Simultaneous zero in one server update should be a draw.
5. After the third wave clears, players may return to town and use the full preparation minute. Optional challenge packs pause without attacks during preparation, arena, and recovery, and refresh without reward for the next cycle. On arena completion, return players to their own town spawn, restore combat resources, and allow the recovery break before spawning again. No cleanup kill rewards or free removal of unfinished ordinary waves.
6. Arena deaths last until the round ends. A surviving team wins on elimination. At timeout, compare living players, then summed living health percentages; an exact tie is a draw without a winner reward. A future objective can replace timeout scoring if evasive play dominates.
7. The first implementation gives persistent match rewards of +3% team power and +8% loot per arena win, capped at +12% power and +40% loot. The modifier is relative: 5% rare-drop chance at 1.4× becomes 7%, not 45%. Challenge gold and eligible reward chances may use the loot multiplier; guaranteed rewards must remain explicit. These caps require snowball testing, especially because power carries into later arenas. A temporary PvE-only boon is an alternative if testing shows a runaway winner advantage.
8. Energy starts at 100 with recovery independent of attributes. Skill definitions choose mana, energy, or no cost. Cooldown reduction is direct percentage reduction, initially capped at 60%; attack speed does not reduce skill cooldowns.

## Attribute arithmetic

Use a single source of truth for base values, level gains, equipment, tomes, and temporary modifiers. If `L` starts at 1, apply `L - 1` level increments; a level-up must never apply twice after reconnect or replication.

```text
Attribute = ChampionBase + LevelGrowth + Equipment + PermanentMatchTomes
MaxHP = BaseHP + 25 * STR
MaxMP = BaseMP + 30 * INT
BasicAttackDamage = BaseAttackDamage + PrimaryAttribute
AttackPeriod = BaseAttackPeriod / (1 + AGI / 100 + OtherAttackSpeedBonus)
EffectiveCooldown = BaseCooldown * (1 - Clamp(CDR, 0, 0.60))
```

Temporary buffs apply in a documented later modifier stage. Armor and resistances affect resolved damage, not the displayed primary-stat contribution. A champion with INT as primary gains +20 basic damage and +600 MP from 20 INT; STR and AGI still contribute their own benefits. Damage/healing coefficients belong to each skill, not to an undocumented universal level multiplier. Design attack-speed and stat limits after performance and combat testing, preserving the requested per-point relationship below those limits.

## Skill draft and combat behavior

The server creates four distinct, legal options from a versioned skill catalog, records the offered IDs, and accepts one selection once. Filter owned skills, incompatible slot use, and passive eligibility before sampling. Every offer before passive selection contains exactly one or two passives; after selection, it contains zero. A passive counts toward the six-slot cap. Invalid or stale choices are rejected without spending currency or changing the offer. If a catalog cannot supply four legal options, report a content validation failure rather than silently offering duplicates or violating passive limits.

To protect the trinity, the proposed offer builder reserves one active choice compatible with the selected role; remaining choices are randomized. A tank needs threat/mitigation, a healer needs recovery/support, and damage dealers need pressure/control. These are offer affinities rather than free starting skills. Provide distinct PvE and PvP coefficients for healing, taunts, crowd control, and interrupts. Taunts affect monster targeting; do not force an enemy player's camera or input. PvP control needs a shared diminishing-return policy before competitive testing.

Target selection, range, line of sight, team, resource cost, cooldown, death state, and phase are checked by the server. Client prediction may improve feedback, but cannot award hits or loot. The minimum readable interface includes target health, party health/resources, cast and interrupt indicators, six active positions with separate passive and ultimate positions, basic attack, phase status, both defense-life totals, and an unmistakable portal warning.

## Waves, challenges, and economy

Start with a single route per team and one symmetric test arena. A wave table mixes advancing melee monsters with ranged/support enemies; later waves add recognizable mechanics rather than only increasing health. Challenge packs occupy side pockets along the advance toward the spawn. They keep their threat at any distance (no leash, Eric's ruling) and reset only when every threat holder is dead; they have clear difficulty tiers, reward only a completed server-recorded encounter, and cannot be farmed by repeatedly disengaging. Running a challenge costs time and defensive coverage.

For the first balance pass, share wave XP/currency across the team so healing and tanking progress. Challenge rewards use personal rolls with equal eligible participation; detailed distribution remains a tuning decision. Town purchases are atomic: validate phase, proximity, price, inventory, and prerequisites, then debit and grant once. EXP tomes must process every crossed skill breakpoint. Stat tomes are permanent for the current match, while account power progression is outside this design.

## Open decisions with material impact

- Which binary within the latest Reforged Enfo family defines the parity target? ETS Starlight has a recent beta and FFB is a separate maintained branch; see `EnfoParity.md`.
- Are later breakpoints skill upgrades, replacements, or no new choices? Current implementation stops unlocks at eight acquired skills (six active, one passive, one ultimate); upgrade/replacement is a future proposal.
- Which minimum hardware, target resolution, and frame rate should constrain the art and monster count?
- How many champions and arenas constitute the first public version? One arena proves the loop; a large roster is a separate content milestone.

These choices should be settled through a playable build and short decisions, without blocking the independent construction of the match loop.
