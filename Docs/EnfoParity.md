# Enfo reference and parity ledger

The user requested **the most recent Warcraft III Reforged version**. There is no single release line shared by every Enfo fork. Cire's Team Survival therefore has a documented conceptual baseline, not verified 1:1 parity. Use a specific reference map/version and hands-on replay before claiming parity or improvement.

## Current-release findings

| Candidate | Verified dated evidence | How to use it |
|---|---|---|
| ETS Starlight 1.2 beta | The author's [22 August 2026 release announcement on Blizzard's forums](https://us.forums.blizzard.com/en/warcraft3/t/ets-starlight-patch-12-beta-released/38350) explicitly calls it a beta and discusses Reforged. The author clarifies the Mellon / Mellstorm names in the same thread. | Newest dated primary-source Warcraft release announcement found in this research. Use as the current-family candidate, with exact binary and gameplay verification outstanding. |
| Enfo's FFB Edition 2.68 | The [author's Hive listing](https://www.hiveworkshop.com/threads/enfos-ffb-edition-v2-68.225458/) reports 20 May 2026, recommends Warcraft 2.00, and lists ten slots with 3v3/4v4 suggested. | Separate maintained Enfo branch; valuable contemporary feature reference, not a universal latest edition. |
| ETS Starlight 1.2 alpha | The [project's file repository](https://sourceforge.net/projects/ets-starlight/files/) lists the Warcraft `.w3x` dated 5 July 2026. Its 17 July `.SC2Map` is for another game. | Historical build superseded by the author's August beta announcement. Do not mistake a later StarCraft file for the latest Warcraft release. |

The beta announcement describes inventory and ownership fixes, expanded Spellbringer inventory, and targeting/spawn corrections. Those are useful regression concerns for Cire's economy and combat. Its proposed future arena is not evidence of Cire's requested five-minute team arena system already existing in that map. Search indexing also surfaced later beta suffixes, but this pass did not establish a dated authoritative release for those binaries; do not equate index dates with release dates.

FFB's author describes escalating enemy waves, Spellbringer interference, more than thirty heroes with innate and learned spells, tank/damage/support roles, commanders, and bosses. Cire's six acquired skills and no innate kit are deliberate differences. Later FFB changelogs are directed to the author's community, so exact 2.68 balance values are not verified from the listing alone.

## Historical sources inspected on 23 September 2026

- [Enfo's public creator profile](https://drwiki.play.net/User:Enfo) self-identifies the original creator and describes the Warcraft III mod as based on DragonRealms. This is background, not a mechanics specification.
- [MT Team's author-posted Enfo's TS:MT Edition 1.93 page](https://www.hiveworkshop.com/threads/enfos-ts-mt-edition-1-93.81150/) is a usable, explicitly versioned reference, last updated 30 October 2011. The creator description lists support monsters, modes, items, and rematches; its changelog records Spellbringer interactions, a duel arena, creep overflow handling, and fixes to lives and hero revival. The same page's contemporary reviewer describes two creep spawn points, opposing team goals, item purchases, and 100 team lives. That review is eyewitness description, not an extracted ruleset.
- [Enfo's Team Survival 4973 author listing](https://www.hiveworkshop.com/threads/enfos-team-survival-4973.77585/) credits Enfo and Krall and lists eight player slots, while MT 1.93 lists ten. This difference alone prevents treating every edition as one fixed specification.

No protected map source has been extracted or copied. Cire's content should use its own code, names, characters, layouts, audio, visual effects, and asset provenance.

## Comparison and acceptance scope

| Reference concern | Cire's intended treatment | Evidence needed before “parity or better” |
|---|---|---|
| Competing survival defenses | Preserve the central race to survive; user confirmed 5v5. | Full matches on both battlefields; tested leakage and simultaneous defeat. |
| Hero progression and items | Preserve meaningful progression; replace fixed hero kits with randomized six-slot drafts. | Economy curves, item decisions, and support/tank progression tested in real matches. |
| Enemy diversity and pressure | Mixed wave roles plus escalating challenge packs. | Encounter roster, increasing mechanical demands, pathing and overload stress tests. |
| Indirect interference and duel mechanics | Recurring team arena battles are the requested direction. Exact legacy mechanics are not presumed replaced feature-for-feature. | Identify requested legacy edition and decide whether separate interference mechanics are still needed. |
| Draft and alternate modes | Planned Single Draft is exactly one STR/AGI/INT option. | Mode-specific legal picks, disconnect handling, role viability, and full match test. |
| Rematch, revival, difficulty and overflow | Treat as robustness requirements to specify, not incidental polish. | Repeat-match reset, bounded actor counts, death recovery and deterministic difficulty tests. |
| Legacy roster, skills, recipes, wave counts and scaling | Unknown until the specific reference is selected; copying a familiar name is not proof of parity. | A versioned feature inventory with observed values and accepted deliberate departures. |
| New challenge rewards | User-requested addition. | Risk/reward tests while lanes remain active; no reset farming or duplicate grants. |
| Five-minute portal loop and loot boon | User-requested addition; default details in `Design.md`. | Complete transitions, finite PvP, valid winner/draw behavior, bounded snowballing. |
| Realistic humans and third-person trinity combat | User-requested transformation of presentation and interaction. | Imported/animated original heroes, readable party combat, measured performance. |

## How to close the parity gap

Record the selected map's title, edition, file hash, Warcraft version, player count, difficulty, and enabled modes. Observe a full match and capture waves, objectives, revive behavior, rewards, items, hero roles, interference systems, match termination, and rematch. Tag each finding as preserved, intentionally replaced, improved with evidence, deferred, or unknown. Keep user-requested differences visible rather than declaring literal parity despite deliberate changes.

“Better” requires evidence: team comprehension, viable build diversity, fair outcomes, sustained tension, stable performance, and fewer failure modes. Higher polygon counts alone cannot establish it.
