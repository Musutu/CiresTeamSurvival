# Playtest 6 (Eric, 2026-09-27) — work breakdown

Raw notes: `playtest-raw.txt` (authoritative wording). Formation reference: `pack-formations.png`
(Blue = tank, Green = healer, Red = DPS: melee / physical ranged / ranged caster).
Each section names the owning branch. Anything ambiguous: pick the sensible default, write it under
"Assumptions" in your branch's RESUME doc, and keep going. Eric reviews at the playtest.

## A. VFX (feat/pack-usage-3) — continues the Fable pack-usage-2 work, now on Opus
- Finish Docs/RESUME-pack-usage.md "Not done" + "Next queue" (A/B/C grading of the 538 systems, demote C picks, after galleries, before/after sheets, tinted monster kits eye-check, remaining unused Big Pack/Shadow/Earth/Blood).
- The greenish-blue circular swirl on-hit effect fires on too many attacks/characters. Find it, keep it for the few that suit it, give the rest varied hits.
- Skill effect sizes −10% across the board; hit effects −20%.
- Open questions to Eric (leave defaults): CC colours (root green, silence magenta, stun gold, slow ice-blue, charm teal, blind navy); arena portals using real recolours (default: yes, do it).

## B. Challenge packs (feat/pack-formations)
- Max pack size 8 (was 6); add a **ranged caster DPS** role option alongside melee and physical ranged.
- Preset formations for pack sizes 3–8 exactly as `pack-formations.png` (positions relative to the pack anchor). Fix pack facing (packs face the path / the approach direction, formation front = tanks).
- **Bug:** Tier 3 and Tier 4 packs can be placed in the route/layout editor but do not register / spawn in playtest after route editing. Fix, with a probe proving T1–T4 all spawn.
- Challenge mob stats: base damage 200 (all roles); Tank base HP 1500; Support caster 50% of tank HP; Melee DPS 75%; Ranged DPS (caster or physical) 65%. Eric scales HP and damage up/down himself: expose global + per-tier multipliers (editor/F8 + data file).

## C. Waves & game modes (feat/waves-modes)
- Armored waves: −50% move speed, slow immunity, 2× stun duration (still path-blockable).
- All wave monsters −20% move speed.
- In-game live wave scale (HP / damage / speed) Eric can adjust while playing.
- Wave monster damage toggle PER wave (on/off). Presets of these settings are saveable.
- Host → **Custom** game type that lists presets as game modes. Ship presets: **Standard** (all waves except armored attack heroes), **Hero TD/PvP** (waves never attack, just path to the castle like armored), **Hybrid** (specific wave packs fight back).
- Wave structure: waves 1–5 = 5 packs of 5 (25 monsters); later waves = 7 packs of 5–7 (35–49). Pack size is a custom/difficulty modifier.
- 25-wave game; waves after 25 are Sudden Death waves (monster HP and damage ×2). 4 PvP rounds (default: after waves 5, 10, 15, 20 — the arena branch consumes this schedule).

## D. Arena flow (feat/arena-flow) — depends on C's schedule; merge after C
- After a PvP wave (5/10/15/20) completes: a portal opens next to EACH player to the designated arena; both teams get a 30 s prep (buy/prepare). Players may enter early; anyone not in at timer end is auto-teleported. Once all are loaded: 7 s countdown banner, then the match starts.
- Killing blow on a player in the arena: +50 g to the killer, win or lose.
- Winning team: +250 g split (50 g each) and a stacking team buff +15% damage to PvE monsters (not PvP). Losing team: stacking −15% PvE damage debuff, no gold.

## E. Bonus loot stages (feat/bonus-loot) — touches wave data; coordinate with C, merge after C
- Bonus loot stage monsters path to the castle keeping their behaviour; the despawn timer does not start until they are attacked, and that time is doubled.
- Bonus stages are random, can replace any wave type except bosses, low frequency; plus other special wave types.
- The stage rolls a tier:
  - Low: current wave gold value ×2 | basic-recipe components | shop consumables (primary-stat tomes or consumables).
  - Mid: wave value ×5 gold | 1 item per player of shop value 350–500 g | one completed **PvP unique** from a new 10-item table the shop cannot sell (mid value, each with a PvP-combat effect).
  - Rare/high: up to 3 chests, each 100 g or a free skill point (skips the next upgrade/spell purchase cost).

## F. Shop anywhere (feat/shop-anywhere)
- Skills and items buyable any time, anywhere. −10% at the matching vendor; +10% surcharge when buying out of town.
- Eric (2026-09-28): the Skill Shop follows the SAME rules: skills buyable any time, anywhere; -10% at the matching vendor (by primary stat/role), +10% out of town, same confirmation dialog.
- Out-of-town purchase shows a confirmation dialog with "Don't show this again", plus Options → a setting to disable confirmation dialogs.

## G. Casting & skill rules (feat/casting-rules)
- Barriers / constructs / summons placement IGNORES clipping (today only a few spots are valid).
- Ground effects are exactly Line / Barrier / Cone / Circle; fix abnormal shapes.
- AoE damage spells get cast times 0.5–3.5 s scaled by impact/damage.
- Projectiles whose animation passes through the target but only hit the first → make them piercing.
- Direct heals: 1.5–3.5 s cast by power. AoE heals: instant or ≤1.5 s, with effect cut accordingly. Healing overall is too strong: nerf.

## H. Bosses & unit spacing (feat/bosses-spacing)
- Bosses 5× bigger (wave bosses, outdoor bosses, pack leaders flagged boss), with collision/nav that still works.
- A distinct large, epic boss health bar (top-of-screen, WoW raid-boss style, themed with the HUD theme).
- All units: tune collision size and melee range slightly up to spread units out and reduce clumping/overlap.

## I. Skill Assignment editor (feat/kit-editor)
- In Champion Select, a dev/editor mode: build each hero's base kit from the whole ability pool, save as a static template per champion, and set the ability effects (VFX/anim sockets) per set so effects sit in the right places for that champion.

## J. Ability pool expansion (feat/ability-expansion)
- More Ultimates, Constructs, Summons, Passives; every remaining unused pack VFX becomes an active spell. Wide pool, many playstyles. Keep Skill Shop periodic-table grouping.

## K. Paragon champions (feat/paragon-champions)
- Eric added the entire Paragon character collection (39 packs, now copied to F:\CiresTeamSurvival\Content\Paragon*; Epic-licensed → LOCAL ONLY, gitignored, never committed).
- Add them all as playable champions. Where a Paragon hero has its own ability animations + effects, take them as-is (retune numbers to this game) as that hero's base kit; also add those abilities to the pool.

## L. Initiation spells + Blink Dagger (feat/initiation) — Eric, 2026-09-28
- Initiation spells: good PvP spells for starting a team fight and enabling team synergies (engage + group CC that allies can follow up on).
- Blink Dagger item.

## M. Hero Creator + Ability Tuner — Eric, 2026-09-28
- Hero Creator (feat/kit-editor): Skill-Shop-style picking of ALL skills (not class-limited), assign to action-bar slots, multiple named presets; kit PROFILES that span all champions and are hardwired to game modes (WavePresets `kitProfile`).
- Ability Tuner (feat/ability-tuner): adjust ability names, effects, durations and numbers live while playing/testing — in the testing build AND the final release — for balancing.
