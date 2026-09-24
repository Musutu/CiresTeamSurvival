# Production roadmap and acceptance gates

This is a staged route to the requested AA or near-AAA quality. A first prototype, generated model, source-code scaffold, or working menu does not meet that quality bar by itself. Each gate needs recorded evidence. Items below are planned acceptance criteria unless a build report explicitly marks them passed.

## Delivery gates

| Gate | Concrete deliverable | Acceptance evidence |
|---|---|---|
| 0 — Reproducible foundation | UE 5.8.3 project with versioned gameplay code/data and a documented launch path. | Editor opens; a clean build succeeds on the installed toolchain; no missing modules/assets; packaged Windows build launches. If compilation cannot run, label source uncompiled. |
| 1 — Local combat loop | One playable human placeholder, target selection, basic attacks, three resource bars, enemies, defense goal, defeat/restart. | From fresh launch, kill enemies and lose by deliberate leakage. Range, death, and team checks work. No need for finished art. |
| 2 — Complete local match | Two battlefields, escalating waves, one challenge encounter, town economy, six-slot draft, one arena, portal rewards. | Play a complete match through at least three PvE → town → arena transitions without console intervention; verify all numerical invariants below. |
| 3 — Network proof | Authoritative server with ten player slots and party/team state. | Dedicated server plus clients complete a 5v5 match. Join, disconnect, duplicate requests, latency, packet loss, and reconnect cannot duplicate gold, tomes, skills, rewards, or deaths. |
| 4 — Vertical slice | Three original human champion archetypes, coherent environment, finished combat feedback, Tripo asset pipeline, trinity coverage. | Representative 5v5 playtest using final-quality hero assets and target camera. Tank, healer, and damage choices all contribute. No visible critical rig failures; clear spell and enemy telegraphs. |
| 5 — Content and balance | Broader champion/skill/item catalog, multiple materially different arenas, Single Draft, challenge tiers, tutorial. | Catalog validates all possible offers; every arena has tested fair spawns and navigable exits; repeated blind playtests show learnability and viable choices. |
| 6 — Release candidate | Stable performance, accessibility, onboarding, online service integration, QA, licensing/provenance, packaging. | Performance and stability pass on declared minimum and recommended PCs. Regression suite, full-match soak, crash review, recovery, and external playtests meet a written release threshold. |

The next gate is earned by playable evidence; expanding the roster does not compensate for an unreliable match loop. Art work can proceed beside gameplay only after one asset completes the full import-to-animation test.

## Core behavior acceptance matrix

| ID | Scenario | Passing result |
|---|---|---|
| STAT-01 | INT-primary character gains 20 INT. | Maximum MP gains 600; basic attack gains 20; no unrelated damage multiplier appears. |
| STAT-02 | Gain one level on each primary type. | Primary +2; both off-stats +1; HP/MP/attack interval update from the same formula once. |
| DRAFT-01 | Generate offers before/after owning a passive over many fixed seeds. | Four distinct legal options; one or two passives before ownership, zero after; never more than one learned passive. |
| DRAFT-02 | EXP tome crosses levels 3 and 6 at once. | Two choices become available in order; reconnect/retry does not grant either twice. |
| DRAFT-03 | Reach six acquired skills. | No seventh slot can be created. Initial implementation stops skill unlocks while stat growth continues; future upgrade/replacement behavior needs its own acceptance. |
| PHASE-01 | Clear the third PvE wave by killing or leaking all wave mobs on both sides. | 60-second prep starts only after both sides are clear; no spawns or damage outside combat phases; arena resolution returns everyone to town for 15 seconds before the next cycle. |
| PHASE-02 | Arena elimination, timeout, draw, disconnect, death at transition. | One authoritative result; no stuck player; finite phase duration; next wave resumes with saved pressure. |
| REWARD-01 | Win successive arenas and replay the result callback. | Persistent match bonuses stay at +12% power / +40% loot caps, and each result grants only once. |
| PACK-01 | Pull, leash, wipe, retry, and finish a challenge. | No reward on partial clear/reset; one reward on completion; difficulty and loot match tier. |
| ECON-01 | Spam purchase, submit stale price, fill inventory, buy while away. | Invalid purchase changes nothing; valid transaction debits and grants once; no negative balance. |
| COMBAT-01 | Cast on invalid team, behind obstruction, out of range, during death or cooldown. | Server rejects invalid resolution consistently while UI explains failure. |
| MATCH-01 | One goal reaches zero; both reach zero together. | Correct winner or explicit draw, stopped rewards/combat, consistent result on all clients. |
| NET-01 | Ten clients, representative monster load, 100–150 ms latency and simulated packet loss. | No unauthorized hit/reward or stuck phase; movement and cast feedback remain usable; measured results recorded. |
| PERF-01 | Worst expected wave plus full-party skill effects. | Stable frame and server-tick budgets on the declared target PC, with no persistent actor, particle, or memory growth. |
| ART-01 | Import and animate one generated human. | Correct scale, textures, skeleton, collision, weapon sockets, LODs, and readable silhouette; joint and foot-contact tests pass. |

## Production responsibilities and architecture

Gameplay design owns formulas and tuning tables; engineering owns authoritative state, save/reconnect behavior, automation, and profiling; art owns topology, materials, deformation, animation, and visual consistency; audio/VFX/UI own combat comprehension. Tripo accelerates model creation but does not replace those acceptance responsibilities.

Use a data-driven skill/item/wave catalog, a replicated phase state machine with server timestamps, and explicit transactions for purchases and rewards. Keep combat simulation separate from cosmetic animation/VFX. Store catalog versions and random seeds with diagnostic match records so a failed draft or unfair result can be reproduced.

Epic's [Gameplay Ability System documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-ability-system-for-unreal-engine) describes its abilities, attributes, and gameplay effects. It is a suitable production candidate for this ability-heavy networked game; adopting it is an engineering decision, not a claim that the current scaffold already uses it. Epic's [networking overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-overview-for-unreal-engine) is the reference for the server/client model.

## Measurements before declaring higher production quality

- Establish a minimum PC and collect CPU, GPU, memory, server tick, bandwidth, loading, and frame-time captures there. A provisional 1080p/60-fps target must remain provisional until hardware is named.
- Record wave survival, arena win rate, challenge attempts, draft choices, healing prevented/wasted, damage, currency, and time spent unable to act. Segment by role and skill combination.
- Check that arena winners gain meaningful progression without making the next arena foregone. Compare later win probability after one victory and streaks; tune duration, drop multiplier, and economy accordingly.
- Conduct structured visual QA at gameplay distance and close range: consistent realistic humans, material response, foot placement, readable team affiliation, spell contrast, animation transitions, and frame cost.
- Expand performance testing only with new complexity or unresolved failures. Keep a small regression suite focused on rules, transactions, transitions, and network authority.

No reliable schedule or final budget can be asserted before the first complete 5v5 vertical slice and minimum hardware target exist.
