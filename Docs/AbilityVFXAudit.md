# Ability VFX audit (ability-vfx)

Eric's brief: improve the existing skill effects, make ground effects visible, test every ability one by
one, and draw line abilities as lines ("I like the targeted projectile's designated spot, but it should
show a line attack instead of a target or point-like identifier, much like a line skill shot").

## Method

`Tools/RunAbilityVFXGallery.py` launches one offscreen game (`-CireAbilityVFXGallery`) that casts
**every ability in isolation** on a clean raised stage (worn-earth lane floor) through the real
gameplay paths, then screenshots six key frames:

| Champion skills (41: 29 roster + 8 role skills + Blight Sigil + passives, and 4 basic attacks) | Monster abilities (302, every race + legacy NPCs) |
|---|---|
| aim (cursor preview at the dummy) / cast / travel / impact / linger / end | telegraph / telegraph late / release / impact / linger / end |
| caster at the origin, hostile dummies downrange (extra dummies for AoE / chains) | the player's champion is the victim; the monster faces it from its cast range; injured ally for heals/guards |

Monsters stay paused until their scripted cast (`CireNPCCombat::DebugStartAbility`, same range, sight,
area and cast-bar rules as the AI) and pause again after the release, and summons/adds are cleared
between abilities. The gameplay camera distance is approximated (about 9 m behind and above the
caster, FOV 80). Pillow composes per-ability sheets and before/after comparisons.

```
python Tools/RunAbilityVFXGallery.py --set all --tag before --legacy   # previous presentation (cire.AbilityVFX 0)
python Tools/RunAbilityVFXGallery.py --set all --tag after
python Tools/RunAbilityVFXGallery.py --compare Saved/AbilityVFX/before-... Saved/AbilityVFX/after-...
python Tools/BuildAbilityVFXAudit.py BEFORE AFTER COMPARE               # regenerates the table below
```

## Findings (legacy presentation)

1. **Line skillshots had no line.** Ember Lance, Frost Bind and Piercing Shot drew their cast cue (fire
   ring, frost crystals, steel ring) *at the aim point*: a point marker where Eric expected a lane. During
   the 0.35 s authored warning the projectile sat at the caster with no telegraph for anyone. The aim
   strip had no direction cue. Piercing Shot's 18 cm projectile was nearly invisible.
2. **Enemy projectile casts had no telegraph at all** (Shadow Bolt, Barbed Shot, Tide Bolt, Barbed Spine):
   only a cast bar; the skillshot could not be read or dodged by sight.
3. **Ground areas were flat stickers**: one translucent colour + a 3 cm rim; the warning was flat amber with
   no timing cue; lines had no arrow; no detonation moment; persistent pools were static.
4. **True radii were never drawn for instant circles**: War Cry's rings grew to ~230 cm while it taunts at
   850 cm; Cleaving Strike (320 cm), Sanctuary (600), Bastion (650), Renewal (1000), Mass Aegis/Challenge
   (900/850) and Cataclysm (550 around the target) showed decorative rings of unrelated size.
5. **Wrong placement**: self/ally casts used the *selected* unit as their position, so Iron Guard,
   Sanctuary, Restoring Light, Purify, Second Wind and Last Stand drew on the enemy you had targeted.
   Monster self-circles and cones drew their cast ring on the victim.
6. **School lost on monsters**: race ability ids matched no family and fell back to the steel crescent.
7. **Chain Spark hops were invisible** after the first bolt; basic ranged attacks (arrow/lance/arcane)
   were hard to follow.
8. **Animation vs release**: cast clips reached contact 0.28 s after the cast was accepted, while
   instant effects appeared on the cast frame and skillshots released at 0.35 s.

## What changed

- One hit-shape descriptor per ability (`CireAbilityShapes`) read from the gameplay data; the aim
  preview, telegraphs and tests use it, so drawn shapes are the true hit shapes (cones cones, circles
  circles, lines lines with their true width and length).
- Telegraph painter (`CireAbilityVFX::PaintTelegraph`): feathered fill, pulsing border on the true
  boundary, progress fill that reaches the edge exactly when the hit resolves, arrowhead + travelling
  chevrons for lines, chevrons for cones, designated-spot marker for circles/squares/custom. Enemy
  and monster warnings are amber; own-team ones school-coloured; monster buff radii (rally) a calm ring.
- Line skillshots: aim lane with arrow; the warning lane of the projectile's true corridor for every
  observer; enemy projectile casts draw the amber lane for the whole cast bar and drop it on interrupt.
  The designated spot remains the aim; no point marker is drawn for line abilities.
- Actives: detonation front + school spikes (colour pulled toward the school so pale data colours no
  longer flash white), persistent ripples and school particles (vapour, embers, ice, wisps, water),
  0.3 s dissolve.
- Projectiles: per-school heads, 20 cm readability floor, wake along the real path, ground glow, burst
  on end; basic ranged attacks get a wake. Impacts: flash, ballistic sparks, debris and school variants,
  ground splash, optional impact camera shake (Options > Graphics, near your champion only).
- Casts: self circles expand a shockwave to the true radius; self buffs stay on the caster; ally spells
  whose selection is hostile fall back to the real heal target; unit spells draw a ground streak;
  Chain Spark arcs between victims with a 500 cm hop ring; monsters wind up at the caster.
- Animation sync: `CireChampionActions` starts the cast clip so its contact frame lands on the release
  (`ReleaseLead`: the skillshot warning, otherwise 0.12 s), and cast/instant-impact cues from that
  champion wait for the same frame.
- A/B switch: `cire.AbilityVFX 0` / `-CireLegacyVFX`.

## Verification

- Build: `CiresTeamSurvivalEditor Win64 Development` succeeds.
- Native suite `CireAbilityVFX::RunTests` (in the combat expansion probe), 2073 checks:
  every champion and monster ability has a hit shape; aim footprint == hit boundary for every
  ground-aimed skill; skillshots are lines of collision diameter x true travel; monster cone/circle/
  line/pull/charge warnings are areas of the true shape (187 cases, cast through the real AI rules),
  painted amber with an arrowhead inside the length for lines; enemy projectile lanes are drawn from the
  caster along the aim at full length/width and removed when the cast is interrupted; line indicators
  span exactly length x width with the arrowhead inside; behavioural radius checks (Cleave 320, War Cry
  850, Cataclysm 550, Chain 500, Sanctuary 600, Bastion 650, Renewal 1000 hit just inside, miss just
  outside); telegraph, projectile and delayed-cue lifecycles clean up; clip contact == cue release for
  every champion skill; vertex budgets, 8-light cap and the 64-actor cap hold.
- Existing suites: see the final report for the run folders (expansion native/network/replay, combat
  checks, spell and aura galleries, interface smoke).

## Limits

- Everything is procedural geometry (spell core/soft materials and `M_GroundArea`); no Niagara systems
  were authored (not practical from code/Python in this pipeline). Particle counts are bounded per effect.
- Buff/aura looks (enrage, shield wall, taunt...) remain owned by `BuffVisuals.json` / CireAuraVisuals;
  this pass adds the cast wind-up but not new aura art.
- Monster charge/pull lanes use the gameplay-shortened length (target distance + 150 cm), which is the
  true hit lane, not the authored maximum.
- Release sync is exact for skillshots (server warning) and a 0.12 s snap for instant skills; instant
  damage numbers (HUD) are not delayed. Monster cast animation timing is owned by CireMonsterArt.
- The audit camera approximates, but is not, the live over-the-shoulder camera; visual acceptance is
  a human call — the harness is evidence, not an art rating.

## Audit table

"Evidence" names the before/after sheet in the comparison folder listed in the final report.

<!-- audit-table:start -->
<!-- audit-table:end -->
