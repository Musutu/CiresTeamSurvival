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
- Existing suites (2026-09-25, `Saved/...` in the ability-vfx worktree):
  expansion native + network + replay PASS (`ExpansionChecks/20260925T044605256724Z`),
  combat features + art preview PASS (`CombatChecks/20260925T040723918638Z`, art `20260925T044444542444Z`),
  spell gallery PASS (`SpellGalleryChecks/20260925T044708688543Z`), aura gallery PASS
  (`AuraGalleryChecks/20260925T044745070929Z`), interface smoke PASS (`InterfaceSmoke/20260925T044843563206Z`).
  Combat **telemetry** fails 8 arena/ultimate assertions identically on unmodified `main`
  (`CombatChecks/20260925T040515114451Z`), so it is pre-existing and unrelated to presentation.
  The art-preview ground check now also accepts an area whose boundary is painted by its visible
  presentation follower (`CireSpellPresentation::IsAreaPresented`), since the flat mesh is not drawn under it.
- Captures: BEFORE `Saved/AbilityVFX/before-all-20260925-033426` (legacy, `-CireLegacyVFX`), AFTER
  `Saved/AbilityVFX/after-all-20260925-040849`, before/after sheets `Saved/AbilityVFX/compare-20260925T043613Z`
  (343 sheets, 0 refused casts in either run). Every overview sheet was reviewed.

## Limits

- Everything is procedural geometry (spell core/soft materials and `M_GroundArea`); no Niagara systems
  were authored (not practical from code/Python in this pipeline). Particle counts are bounded per effect.
- Buff/aura looks (enrage, shield wall, taunt...) remain owned by `BuffVisuals.json` / CireAuraVisuals;
  this pass adds the cast wind-up but not new aura art.
- Monster charge/pull lanes use the gameplay-shortened length (target distance + 150 cm), which is the
  true hit lane, not the authored maximum.
- Release sync is exact for skillshots (server warning) and a 0.12 s snap for instant skills; instant
  damage numbers (HUD) are not delayed. Monster cast animation timing is owned by CireMonsterArt.
- After the final capture the active-zone fill was made denser for bright ground (the art-preview
  cobble scene is overexposed, where soft fills wash out); the AFTER sheets predate that tweak.
- In a few monster END frames the unit has already started its next ability (AI resumes after release).
- The audit camera approximates, but is not, the live over-the-shoulder camera; visual acceptance is
  a human call — the harness is evidence, not an art rating.

## Audit table

"Evidence" names the before/after sheet in the comparison folder listed in the final report.

<!-- audit-table:start -->
| # | Ability | Type (caster) | Before | Problems | Fix | Evidence |
|---|---|---|---|---|---|---|
| 1 | Shield Slam `shield_slam` | champion / unit / holy (knight) | Holy sigil and helix drawn on the target; no link to the caster. | No indication of who struck whom; reads like a heal. | Ground streak caster->target, 75 cm hit ring, holy impact with rays; clip contact on release. | 000_shield_slam.jpg |
| 2 | Iron Guard `iron_guard` | champion / self / holy (knight) | Holy sigil/helix spawned on the SELECTED ENEMY when one was targeted. | Wrong placement: the self buff appeared on the enemy. | Anchored on the caster; personal pulse ring; buff aura (BuffVisuals) unchanged. | 001_iron_guard.jpg |
| 3 | War Cry `war_cry` | champion / circle / steel (knight) | Two amber rings growing to ~230 cm around the caster. | True taunt radius is 850 cm; the rings under-sold the reach by 3.7x. | Ground shockwave that expands exactly to 850 cm and holds the edge; legacy rings kept at the caster. | 002_war_cry.jpg |
| 4 | Cleaving Strike `cleaving_strike` | champion / circle / steel (knight) | Small forged crescent at the target, no area. | Hits every enemy within 320 cm of the caster; nothing showed that disc. | Shockwave to the true 320 cm radius around the caster plus steel sparks/debris impacts. | 003_cleaving_strike.jpg |
| 5 | Second Wind `second_wind` | champion / self / holy (knight) | Holy sigil on the selected unit (enemy when targeted). | Wrong placement; small. | On the caster, personal pulse ring. | 004_second_wind.jpg |
| 6 | Protection Dome `protection_dome` | champion / custom / holy (knight) | Placement preview only; cage trims fine. | Preview had no centre mark. | Aim footprint with animated spot marker; cage unchanged. | 005_protection_dome.jpg |
| 7 | Bastion of Dawn `bastion_of_dawn` | champion / circle / holy (knight) | Large holy sigil at the caster. | Guard reach (650 cm) not shown. | Shockwave to 650 cm, holy flourish kept. | 006_bastion_of_dawn.jpg |
| 8 | Stone Skin `stone_skin` | passive / none / earth (knight) | Passive; basic swing only. | None (passive). | No telegraph by design; basic swing impact improved (sparks/debris). | 007_stone_skin.jpg |
| 9 | Piercing Shot `piercing_shot` | champion / line / steel (ranger) | Thin strip aim; 18 cm projectile nearly invisible; steel cast ring at aim. | Unreadable projectile; point marker. | Lane + arrow (36 cm x 1500 cm); arrow shaft head with 20 cm readability floor, white streak, bursts. | 008_piercing_shot.jpg |
| 10 | Frost Bind `frost_bind` | champion / line / frost (ranger) | Same as Ember Lance with frost crystals at the aim point. | Point marker instead of a line; no warning lane. | Aim lane + arrow; 68 cm x 1200 cm warning lane; crystal spear head; frost impact spikes. | 009_frost_bind.jpg |
| 11 | Shadow Step `shadow_step` | champion / unit / shadow (ranger) | Shadow ring/helix at the target. | No dash read. | Violet ground streak caster->target, hit ring, shadow implosion impact. | 010_shadow_step.jpg |
| 12 | Venom Ground `venom_ground` | champion / circle / poison (ranger) | Flat green disc + thin tubes; amber flat warning. | No timing cue; edge hard to see on terrain. | Soft feathered disc, pulsing border, fill that grows to detonation, spot marker; bubbling poison vapour while active. | 011_venom_ground.jpg |
| 13 | Grave Line `grave_line` | champion / line / shadow (ranger) | Flat line strip, amber warning, shadow ring at the caster. | No direction/arrow, no timing. | Lane with arrowhead + chevrons, progress fill from the caster, detonation front, dissolve. | 012_grave_line.jpg |
| 14 | Spectral Pack `spectral_pack` | champion / circle / spirit (ranger) | Summon circle preview; wolves appear. | Cast refused in the old harness (620 cm aim > 600 cast range). | Placement preview with spot marker; spirit shock ring on arrival. | 013_spectral_pack.jpg |
| 15 | Executioner's Verdict `executioners_verdict` | champion / unit / blood (ranger) | Blood sigil rings on the target. | Weak for an ultimate; no strike read. | Ground streak + hit ring, blood impact with debris, camera kick when near you. | 014_executioners_verdict.jpg |
| 16 | Battle Rhythm `battle_rhythm` | passive / none / steel (ranger) | Passive; bow shot. | Arrow barely visible. | Basic arrow gets a lit head + wake (ranged basics). | 015_battle_rhythm.jpg |
| 17 | Restoring Light `restoring_light` | champion / unit / life (scholar) | Life helix drawn on the selected ENEMY when one was targeted (heal went to self). | Wrong placement. | Re-anchored on the real heal target; ground streak to allies; life rays impact. | 016_restoring_light.jpg |
| 18 | Sanctuary `sanctuary` | champion / circle / holy (scholar) | Holy sigil/helix drawn on the SELECTED ENEMY (heal is around the caster). | Wrong placement; 600 cm heal reach not shown. | Shockwave to 600 cm, holy flourish. | 017_sanctuary.jpg |
| 19 | Purify `purify` | champion / unit / life (scholar) | Life helix on the selected enemy. | Wrong placement. | Re-anchored on the ally / self. | 018_purify.jpg |
| 20 | Chain Spark `chain_spark` | champion / chain / storm (scholar) | Lightning from caster to target; later victims only got a star. | Hops invisible; chain reach (500 cm) unknown. | Bolt to the first target, arcs hop to each further victim, faint 500 cm hop ring. | 019_chain_spark.jpg |
| 21 | Ember Lance `ember_lance` | champion / line / fire (scholar) | Aim: flat green strip. Cast: fire ring + embers at the AIM POINT. Projectile sat unannounced for 0.35 s. | Point marker at the designated spot instead of a line; no enemy-visible telegraph; no direction cue. | Aim lane with arrowhead + travelling chevrons; 0.35 s warning lane of the true 60 cm x 1200 cm corridor that fills; caster flare; fire head/wake; ground glow under the head; burst on end. | 020_ember_lance.jpg |
| 22 | Renewal `renewal` | champion / circle / life (scholar) | Life sigil at caster. | 1000 cm reach not shown. | Shockwave to 1000 cm. | 021_renewal.jpg |
| 23 | Soul Conduit `soul_conduit` | passive / none / steel (scholar) | Passive. | None. | No telegraph by design. | 022_soul_conduit.jpg |
| 24 | Ashen Ward `ashen_square` | champion / square / holy (lancer) | Flat square fill, amber warning. | No timing cue. | Soft square, pulsing border, growing fill, centre marker, detonation. (2026-09-26: now a 280 cm circle, see the shape audit.) | 023_ashen_square.jpg |
| 25 | Oathbound Guardian `oathbound_guardian` | champion / circle / spirit (summoner) | Small placement circle. | - | Spot marker preview; spirit arrival pulse. | 024_oathbound_guardian.jpg |
| 26 | Summoned Wall `summoned_wall` | champion / custom / earth (summoner) | Rectangle preview. | - | Preview with animated border; runic trims unchanged. | 025_summoned_wall.jpg |
| 27 | Cataclysm `cataclysm` | champion / circle / fire (summoner) | Fire ring (~140 cm) at the target. | True radius 550 cm around the target not shown. | Shockwave to 550 cm + fire flourish, camera kick. | 026_cataclysm.jpg |
| 28 | Deep Reserves `deep_reserves` | passive / none / steel (summoner) | Passive. | None. | No telegraph by design. | 027_deep_reserves.jpg |
| 29 | Cinder Cone `cinder_cone` | champion / cone / fire (ether_golem_bruiser) | Flat cone; amber warning; fire ring at the caster. | No direction cue or timing. | Cone with chevrons, progress wedge, detonation front, ember spikes. | 028_cinder_cone.jpg |
| 30 | Blight Sigil `blight_sigil` | champion / custom / poison (summoner) | Flat concave polygon. | Edges hard to see. | Feathered concave fill (ear-clipped), pulsing border, marker, vapour. | 029_blight_sigil.jpg |
| 31 | Last Stand `last_stand` | champion / self / earth (knight) | Earth ring at the selected unit. | Wrong placement when an enemy is selected. | On the caster with a personal pulse. | 030_last_stand.jpg |
| 32 | Challenge of Iron `challenge_of_iron` | champion / circle / steel (knight) | Amber rings ~230 cm. | True 850 cm reach not shown. | Shockwave to 850 cm. | 031_challenge_of_iron.jpg |
| 33 | Seismic Reprisal `seismic_reprisal` | champion / circle / earth (knight) | Flat amber warning then earth ring. | No timing cue. | School-coloured warning with growing fill, detonation spikes. | 032_seismic_reprisal.jpg |
| 34 | Starfall `starfall` | champion / circle / arcane (wizard) | Flat circle, starfall sigil at aim. | No timing cue. | Designated-spot marker kept; growing fill, meteor flourish, detonation front. | 033_starfall.jpg |
| 35 | Spectral Hunt `spectral_hunt` | champion / unit / spirit (ranger) | Spirit wisps. | - | Streak to target; hunters unchanged. | 034_spectral_hunt.jpg |
| 36 | Mass Aegis `mass_aegis` | champion / circle / holy (paladin_holy) | Shield sigil at caster. | 900 cm reach not shown. | Shockwave to 900 cm. | 035_mass_aegis.jpg |
| 37 | Wellspring `wellspring` | champion / unit / nature (dryad) | Nature bloom on the selection. | - | Streak to ally, bloom kept. | 036_wellspring.jpg |
| 38 | basic_sword `basic_sword` | basic / unit / steel (knight) | Crescent on hit. | - | Steel sparks/debris impact. | 037_basic_sword.jpg |
| 39 | basic_bow `basic_bow` | basic / unit / steel (ranger) | Prototype arrow mesh only. | Hard to follow at distance. | Lit arrow head + wake following the real flight. | 038_basic_bow.jpg |
| 40 | basic_lance `basic_lance` | basic / unit / steel (lancer) | Prototype lance mesh only. | Hard to follow. | Lit head + wake. | 039_basic_lance.jpg |
| 41 | basic_arcane `basic_arcane` | basic / unit / arcane (scholar) | Small ember sphere. | Hard to follow. | Arcane orb head with orbiting sparks + wake. | 040_basic_arcane.jpg |
| 42 | Hooked Claws `npc_melee` | drowned_deep unit / tide (abyssal_stalker) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 041_npc_melee.jpg |
| 43 | Riptide Lunge `drowned_stalker_lunge` | drowned_deep line / tide (abyssal_stalker) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 042_drowned_stalker_lunge.jpg |
| 44 | Riptide Rend `drowned_rend` | drowned_deep cone / tide (abyssal_stalker) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 043_drowned_rend.jpg |
| 45 | Undertow Grab `drowned_undertow` | drowned_deep line / tide (abyssal_stalker) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 044_drowned_undertow.jpg |
| 46 | Ink Veil `drowned_ink_veil` | drowned_deep self / tide (abyssal_stalker) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 045_drowned_ink_veil.jpg |
| 47 | Tidal Slam `drowned_tidal_slam` | drowned_deep cone / tide (deepspawn_thrall) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 046_drowned_tidal_slam.jpg |
| 48 | Barnacle Charge `drowned_barnacle_charge` | drowned_deep line / tide (deepspawn_thrall) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 047_drowned_barnacle_charge.jpg |
| 49 | Crushing Undertow `drowned_undertow_stomp` | drowned_deep circle / tide (deepspawn_thrall) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 048_drowned_undertow_stomp.jpg |
| 50 | Brine Frenzy `drowned_brine_frenzy` | drowned_deep self / blood (deepspawn_thrall) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 049_drowned_brine_frenzy.jpg |
| 51 | Abyssal Roar `drowned_abyssal_roar` | drowned_deep circle / void (coralshell_guardian) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 050_drowned_abyssal_roar.jpg |
| 52 | Coral Bulwark `drowned_coral_bulwark` | drowned_deep self / tide (coralshell_guardian) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 051_drowned_coral_bulwark.jpg |
| 53 | Tidewall Oath `drowned_tidewall_oath` | drowned_deep unit / tide (coralshell_guardian) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 052_drowned_tidewall_oath.jpg |
| 54 | Shell Crash `drowned_shell_bash` | drowned_deep cone / tide (coralshell_guardian) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 053_drowned_shell_bash.jpg |
| 55 | Tide Bolt `npc_bolt` | drowned_deep line / tide (tidecaller) | No telegraph at all during the cast bar; small projectile. | Enemy skillshot undodgeable by sight. | Amber lane of the skillshot's true corridor for the whole cast bar (dropped on interrupt), readable head + wake + burst. | 054_npc_bolt.jpg |
| 56 | Drowning Pool `drowned_drowning_pool` | drowned_deep circle / tide (tidecaller) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 055_drowned_drowning_pool.jpg |
| 57 | Whirlpool `drowned_whirlpool` | drowned_deep circle / tide (tidecaller) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 056_drowned_whirlpool.jpg |
| 58 | Brine Mending `drowned_brine_mending` | drowned_deep unit / tide (tidecaller) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 057_drowned_brine_mending.jpg |
| 59 | Crashing Wave `drowned_crashing_wave` | drowned_deep cone / tide (tidecaller) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 058_drowned_crashing_wave.jpg |
| 60 | Riptide `drowned_riptide` | drowned_deep circle / tide (tidecaller) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 059_drowned_riptide.jpg |
| 61 | Barbed Spine `npc_shot` | drowned_deep line / tide (barbspitter) | No telegraph at all during the cast bar; small projectile. | Enemy skillshot undodgeable by sight. | Amber lane of the skillshot's true corridor for the whole cast bar (dropped on interrupt), readable head + wake + burst. | 060_npc_shot.jpg |
| 62 | Ink Spit `drowned_ink_spit` | drowned_deep circle / tide (barbspitter) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 061_drowned_ink_spit.jpg |
| 63 | Spine Volley `drowned_spine_volley` | drowned_deep circle / tide (barbspitter) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 062_drowned_spine_volley.jpg |
| 64 | Jet Retreat `drowned_jet_retreat` | drowned_deep self / tide (barbspitter) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 063_drowned_jet_retreat.jpg |
| 65 | Harpoon Spine `drowned_harpoon_spine` | drowned_deep line / tide (barbspitter) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 064_drowned_harpoon_spine.jpg |
| 66 | Mind Scream `drowned_mind_scream` | drowned_deep circle / tide (mind_leech) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 065_drowned_mind_scream.jpg |
| 67 | Dread Whisper `drowned_dread_whisper` | drowned_deep circle / tide (mind_leech) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 066_drowned_dread_whisper.jpg |
| 68 | Leech Mending `drowned_leech_mending` | drowned_deep unit / void (mind_leech) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 067_drowned_leech_mending.jpg |
| 69 | Abyssal Ward `drowned_abyssal_ward` | drowned_deep unit / void (mind_leech) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 068_drowned_abyssal_ward.jpg |
| 70 | Maddening Gaze `drowned_maddening_gaze` | drowned_deep circle / void (mind_leech) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 069_drowned_maddening_gaze.jpg |
| 71 | Tidal Prophecy `drowned_tidal_prophecy` | drowned_deep circle / tide (drowned_prophet) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 070_drowned_tidal_prophecy.jpg |
| 72 | Call of the Deep `drowned_call_of_the_deep` | drowned_deep self / tide (drowned_prophet) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 071_drowned_call_of_the_deep.jpg |
| 73 | Mind Shatter `drowned_mind_shatter` | drowned_deep circle / tide (drowned_prophet) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 072_drowned_mind_shatter.jpg |
| 74 | Drowning Grasp `drowned_drowning_grasp` | drowned_deep line / tide (drowned_prophet) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 073_drowned_drowning_grasp.jpg |
| 75 | Prophet's Madness `drowned_prophet_madness` | drowned_deep self / void (drowned_prophet) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 074_drowned_prophet_madness.jpg |
| 76 | Tentacle Sweep `drowned_tentacle_sweep` | drowned_deep cone / tide (maw_of_the_deep) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 075_drowned_tentacle_sweep.jpg |
| 77 | Devour `drowned_devour` | drowned_deep line / tide (maw_of_the_deep) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 076_drowned_devour.jpg |
| 78 | Tsunami Slam `drowned_tsunami_slam` | drowned_deep circle / tide (maw_of_the_deep) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 077_drowned_tsunami_slam.jpg |
| 79 | Ink Tide `drowned_ink_tide` | drowned_deep circle / tide (maw_of_the_deep) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 078_drowned_ink_tide.jpg |
| 80 | Leviathan Rage `drowned_leviathan_rage` | drowned_deep self / blood (maw_of_the_deep) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 079_drowned_leviathan_rage.jpg |
| 81 | Thorn Whip `blight_vine_lash` | blightwood cone / nature (vinelasher) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 080_blight_vine_lash.jpg |
| 82 | Strangling Vines `blight_strangling_vines` | blightwood circle / nature (vinelasher) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 081_blight_strangling_vines.jpg |
| 83 | Vine Snare `blight_vine_snare` | blightwood line / nature (vinelasher) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 082_blight_vine_snare.jpg |
| 84 | Briar Rush `blight_briar_rush` | blightwood line / nature (vinelasher) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 083_blight_briar_rush.jpg |
| 85 | Stump Slam `blight_stump_slam` | blightwood cone / poison (sapling_brute) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 084_blight_stump_slam.jpg |
| 86 | Uproot Charge `blight_uproot_charge` | blightwood line / poison (sapling_brute) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 085_blight_uproot_charge.jpg |
| 87 | Splinter Burst `blight_splinter_burst` | blightwood circle / nature (sapling_brute) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 086_blight_splinter_burst.jpg |
| 88 | Sap Frenzy `blight_sap_frenzy` | blightwood self / blood (sapling_brute) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 087_blight_sap_frenzy.jpg |
| 89 | Grove Challenge `blight_grove_challenge` | blightwood circle / nature (barkhide_warden) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 088_blight_grove_challenge.jpg |
| 90 | Rooted Stance `blight_rooted_stance` | blightwood self / poison (barkhide_warden) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 089_blight_rooted_stance.jpg |
| 91 | Barkskin Oath `blight_barkskin_oath` | blightwood unit / poison (barkhide_warden) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 090_blight_barkskin_oath.jpg |
| 92 | Root Quake `blight_root_quake` | blightwood circle / nature (barkhide_warden) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 091_blight_root_quake.jpg |
| 93 | Rot Pool `blight_rot_pool` | blightwood circle / poison (rotbloom_shaman) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 092_blight_rot_pool.jpg |
| 94 | Entangle `blight_entangle` | blightwood circle / nature (rotbloom_shaman) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 093_blight_entangle.jpg |
| 95 | Sap Mending `blight_sap_mending` | blightwood unit / nature (rotbloom_shaman) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 094_blight_sap_mending.jpg |
| 96 | Wither Curse `blight_wither_curse` | blightwood circle / poison (rotbloom_shaman) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 095_blight_wither_curse.jpg |
| 97 | Thorn Burst `blight_thorn_burst` | blightwood cone / nature (rotbloom_shaman) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 096_blight_thorn_burst.jpg |
| 98 | Thorn Volley `blight_thorn_volley` | blightwood circle / nature (thornspitter) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 097_blight_thorn_volley.jpg |
| 99 | Briar Patch `blight_briar_patch` | blightwood circle / nature (thornspitter) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 098_blight_briar_patch.jpg |
| 100 | Seedpod Mortar `blight_seedpod_mortar` | blightwood circle / poison (thornspitter) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 099_blight_seedpod_mortar.jpg |
| 101 | Root Hop `blight_root_hop` | blightwood self / nature (thornspitter) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 100_blight_root_hop.jpg |
| 102 | Spore Cloud `blight_spore_cloud` | blightwood circle / poison (sporeling) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 101_blight_spore_cloud.jpg |
| 103 | Choking Puffball `blight_choking_puff` | blightwood circle / poison (sporeling) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 102_blight_choking_puff.jpg |
| 104 | Mycelial Link `blight_mycelial_link` | blightwood unit / poison (sporeling) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 103_blight_mycelial_link.jpg |
| 105 | Spore Scatter `blight_spore_scatter` | blightwood self / poison (sporeling) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 104_blight_spore_scatter.jpg |
| 106 | Thornstorm `blight_thornstorm` | blightwood circle / nature (withered_matron) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 105_blight_thornstorm.jpg |
| 107 | Strangling Grove `blight_strangling_grove` | blightwood circle / nature (withered_matron) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 106_blight_strangling_grove.jpg |
| 108 | Blight Bloom `blight_blight_bloom` | blightwood circle / poison (withered_matron) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 107_blight_blight_bloom.jpg |
| 109 | Matron's Call `blight_matron_call` | blightwood self / poison (withered_matron) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 108_blight_matron_call.jpg |
| 110 | Rotting Embrace `blight_rotting_embrace` | blightwood line / nature (withered_matron) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 109_blight_rotting_embrace.jpg |
| 111 | Withering Wrath `blight_withering_wrath` | blightwood self / poison (withered_matron) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 110_blight_withering_wrath.jpg |
| 112 | Root Eruption `blight_root_eruption` | blightwood circle / nature (elder_oakheart) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 111_blight_root_eruption.jpg |
| 113 | Oakheart Stomp `blight_oakheart_stomp` | blightwood circle / poison (elder_oakheart) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 112_blight_oakheart_stomp.jpg |
| 114 | Crushing Bough `blight_crushing_bough` | blightwood cone / poison (elder_oakheart) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 113_blight_crushing_bough.jpg |
| 115 | Awaken Saplings `blight_awaken_saplings` | blightwood self / nature (elder_oakheart) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 114_blight_awaken_saplings.jpg |
| 116 | Heartwood Fury `blight_heartwood_fury` | blightwood self / blood (elder_oakheart) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 115_blight_heartwood_fury.jpg |
| 117 | Lunge `npc_infantry_lunge` | hollow line / shadow (hollow_infantry) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 116_npc_infantry_lunge.jpg |
| 118 | Rusted Cleave `hollow_rusted_cleave` | hollow cone / steel (hollow_infantry) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 117_hollow_rusted_cleave.jpg |
| 119 | Deathless Resolve `hollow_deathless` | hollow self / shadow (hollow_infantry) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 118_hollow_deathless.jpg |
| 120 | Brutal Charge `npc_bruiser_charge` | hollow line / shadow (ironbound_bruiser) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 119_npc_bruiser_charge.jpg |
| 121 | Crushing Slam `npc_bruiser_slam` | hollow cone / shadow (ironbound_bruiser) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 120_npc_bruiser_slam.jpg |
| 122 | Iron Stomp `hollow_iron_stomp` | hollow circle / steel (ironbound_bruiser) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 121_hollow_iron_stomp.jpg |
| 123 | Hook Chain `hollow_hook_chain` | hollow line / steel (ironbound_bruiser) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 122_hollow_hook_chain.jpg |
| 124 | Challenging Roar `npc_tank_provoke` | hollow circle / shadow (hollow_shieldbearer) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 123_npc_tank_provoke.jpg |
| 125 | Guardian's Oath `npc_tank_guard` | hollow unit / shadow (hollow_shieldbearer) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 124_npc_tank_guard.jpg |
| 126 | Shield Wall `npc_tank_wall` | hollow self / shadow (hollow_shieldbearer) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 125_npc_tank_wall.jpg |
| 127 | Shield Rush `hollow_shield_rush` | hollow line / steel (hollow_shieldbearer) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 126_hollow_shield_rush.jpg |
| 128 | Shadow Bolt `npc_shadow_bolt` | hollow line / shadow (blight_caster) | No telegraph at all during the cast bar; small projectile. | Enemy skillshot undodgeable by sight. | Amber lane of the skillshot's true corridor for the whole cast bar (dropped on interrupt), readable head + wake + burst. | 127_npc_shadow_bolt.jpg |
| 129 | Dark Mending `npc_caster_mend` | hollow unit / shadow (blight_caster) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 128_npc_caster_mend.jpg |
| 130 | Blight Pool `npc_blight_pool` | hollow circle / shadow (blight_caster) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 129_npc_blight_pool.jpg |
| 131 | Grave Silence `hollow_grave_silence` | hollow circle / shadow (blight_caster) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 130_hollow_grave_silence.jpg |
| 132 | Withering Hex `hollow_withering_hex` | hollow circle / poison (blight_caster) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 131_hollow_withering_hex.jpg |
| 133 | Barbed Shot `npc_barbed_shot` | hollow line / shadow (barbed_hunter) | No telegraph at all during the cast bar; small projectile. | Enemy skillshot undodgeable by sight. | Amber lane of the skillshot's true corridor for the whole cast bar (dropped on interrupt), readable head + wake + burst. | 132_npc_barbed_shot.jpg |
| 134 | Disengage `npc_hunter_disengage` | hollow self / shadow (barbed_hunter) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 133_npc_hunter_disengage.jpg |
| 135 | Rain of Barbs `npc_hunter_volley` | hollow circle / shadow (barbed_hunter) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 134_npc_hunter_volley.jpg |
| 136 | Pinning Net `hollow_pinning_net` | hollow circle / shadow (barbed_hunter) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 135_hollow_pinning_net.jpg |
| 137 | Pounce `hollow_pounce` | hollow line / shadow (grave_hound) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 136_hollow_pounce.jpg |
| 138 | Rending Bite `hollow_rending_bite` | hollow cone / shadow (grave_hound) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 137_hollow_rending_bite.jpg |
| 139 | Pack Howl `hollow_pack_howl` | hollow circle / blood (grave_hound) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 138_hollow_pack_howl.jpg |
| 140 | Rallying Roar `boss_leader_rally` | hollow circle / shadow (gravemaw_pack_leader) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 139_boss_leader_rally.jpg |
| 141 | Sundering Cleave `boss_leader_cleave` | hollow cone / shadow (gravemaw_pack_leader) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 140_boss_leader_cleave.jpg |
| 142 | Brutal Charge `boss_leader_charge` | hollow line / shadow (gravemaw_pack_leader) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 141_boss_leader_charge.jpg |
| 143 | Blood Frenzy `boss_leader_frenzy` | hollow self / shadow (gravemaw_pack_leader) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 142_boss_leader_frenzy.jpg |
| 144 | Bone Hook `hollow_bone_hook` | hollow line / shadow (gravemaw_pack_leader) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 143_hollow_bone_hook.jpg |
| 145 | Siege Stomp `boss_siege_stomp` | hollow circle / shadow (hollow_siegebreaker) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 144_boss_siege_stomp.jpg |
| 146 | Sundering Cleave `boss_siege_cleave` | hollow cone / shadow (hollow_siegebreaker) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 145_boss_siege_cleave.jpg |
| 147 | Rallying Bellow `boss_siege_rally` | hollow circle / shadow (hollow_siegebreaker) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 146_boss_siege_rally.jpg |
| 148 | Siege Fury `boss_siege_fury` | hollow self / shadow (hollow_siegebreaker) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 147_boss_siege_fury.jpg |
| 149 | Rubble Toss `hollow_rubble_toss` | hollow circle / steel (hollow_siegebreaker) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 148_hollow_rubble_toss.jpg |
| 150 | Cleave `ironhide_cleave` | ironhide cone / blood (ironhide_grunt) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 149_ironhide_cleave.jpg |
| 151 | War Charge `ironhide_war_charge` | ironhide line / blood (ironhide_grunt) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 150_ironhide_war_charge.jpg |
| 152 | Bloodlust `ironhide_bloodlust` | ironhide circle / blood (ironhide_grunt) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 151_ironhide_bloodlust.jpg |
| 153 | Gut Punch `ironhide_gut_punch` | ironhide cone / blood (ironhide_grunt) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 152_ironhide_gut_punch.jpg |
| 154 | Whirlwind `ironhide_whirlwind` | ironhide circle / blood (redmoon_ravager) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 153_ironhide_whirlwind.jpg |
| 155 | Berserk `ironhide_berserk` | ironhide self / blood (redmoon_ravager) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 154_ironhide_berserk.jpg |
| 156 | Leaping Axes `ironhide_leaping_axe` | ironhide line / blood (redmoon_ravager) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 155_ironhide_leaping_axe.jpg |
| 157 | Rend `ironhide_rend` | ironhide cone / blood (redmoon_ravager) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 156_ironhide_rend.jpg |
| 158 | Challenging Shout `ironhide_war_cry` | ironhide circle / blood (ironhide_bulwark) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 157_ironhide_war_cry.jpg |
| 159 | Iron Door `ironhide_shield_wall` | ironhide self / blood (ironhide_bulwark) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 158_ironhide_shield_wall.jpg |
| 160 | Bodyguard `ironhide_bodyguard` | ironhide unit / blood (ironhide_bulwark) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 159_ironhide_bodyguard.jpg |
| 161 | Meat Hook `ironhide_chain_hook` | ironhide line / blood (ironhide_bulwark) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 160_ironhide_chain_hook.jpg |
| 162 | Blood Hex `ironhide_blood_hex` | ironhide circle / blood (blood_hexer) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 161_ironhide_blood_hex.jpg |
| 163 | Spirit Mend `ironhide_spirit_mend` | ironhide unit / blood (blood_hexer) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 162_ironhide_spirit_mend.jpg |
| 164 | Bog Curse `ironhide_bog_curse` | ironhide circle / poison (blood_hexer) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 163_ironhide_bog_curse.jpg |
| 165 | Frenzy Ritual `ironhide_frenzy_ritual` | ironhide circle / blood (blood_hexer) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 164_ironhide_frenzy_ritual.jpg |
| 166 | Axe Barrage `ironhide_axe_barrage` | ironhide circle / blood (redmoon_axethrower) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 165_ironhide_axe_barrage.jpg |
| 167 | Hamstring Axe `ironhide_hamstring_axe` | ironhide circle / blood (redmoon_axethrower) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 166_ironhide_hamstring_axe.jpg |
| 168 | Bounding Retreat `ironhide_bounding_retreat` | ironhide self / blood (redmoon_axethrower) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 167_ironhide_bounding_retreat.jpg |
| 169 | Spinning Axe `ironhide_spinning_axe` | ironhide cone / blood (redmoon_axethrower) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 168_ironhide_spinning_axe.jpg |
| 170 | War Drums `ironhide_war_drums` | ironhide circle / blood (ironhide_drummer) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 169_ironhide_war_drums.jpg |
| 171 | Battle Hymn `ironhide_battle_hymn` | ironhide unit / blood (ironhide_drummer) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 170_ironhide_battle_hymn.jpg |
| 172 | Deafening Boom `ironhide_deafening_boom` | ironhide circle / blood (ironhide_drummer) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 171_ironhide_deafening_boom.jpg |
| 173 | Warchief's Roar `ironhide_warchief_roar` | ironhide circle / blood (ironhide_warchief) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 172_ironhide_warchief_roar.jpg |
| 174 | Skull Cleave `ironhide_skull_cleave` | ironhide cone / blood (ironhide_warchief) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 173_ironhide_skull_cleave.jpg |
| 175 | Warpath `ironhide_warpath` | ironhide line / blood (ironhide_warchief) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 174_ironhide_warpath.jpg |
| 176 | Call the Clans `ironhide_call_the_clans` | ironhide self / blood (ironhide_warchief) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 175_ironhide_call_the_clans.jpg |
| 177 | Blood Fury `ironhide_blood_fury` | ironhide self / blood (ironhide_warchief) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 176_ironhide_blood_fury.jpg |
| 178 | Earthsplitter `ironhide_earthsplitter` | ironhide circle / blood (ironhide_juggernaut) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 177_ironhide_earthsplitter.jpg |
| 179 | Trunk Sweep `ironhide_trunk_sweep` | ironhide cone / blood (ironhide_juggernaut) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 178_ironhide_trunk_sweep.jpg |
| 180 | Boulder Hurl `ironhide_boulder_hurl` | ironhide circle / blood (ironhide_juggernaut) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 179_ironhide_boulder_hurl.jpg |
| 181 | Troll Hide `ironhide_thick_hide` | ironhide self / blood (ironhide_juggernaut) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 180_ironhide_thick_hide.jpg |
| 182 | Juggernaut Rage `ironhide_juggernaut_rage` | ironhide self / blood (ironhide_juggernaut) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 181_ironhide_juggernaut_rage.jpg |
| 183 | Spear Lunge `drakkari_spear_lunge` | drakkari line / fire (drakkari_whelpguard) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 182_drakkari_spear_lunge.jpg |
| 184 | Tail Sweep `drakkari_tail_sweep` | drakkari cone / fire (drakkari_whelpguard) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 183_drakkari_tail_sweep.jpg |
| 185 | Ember Breath `drakkari_ember_breath` | drakkari cone / fire (drakkari_whelpguard) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 184_drakkari_ember_breath.jpg |
| 186 | Wing Buffet `drakkari_wing_buffet` | drakkari circle / fire (drakkari_scalebreaker) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 185_drakkari_wing_buffet.jpg |
| 187 | Dive Charge `drakkari_dive_charge` | drakkari line / fire (drakkari_scalebreaker) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 186_drakkari_dive_charge.jpg |
| 188 | Molten Slam `drakkari_molten_slam` | drakkari cone / fire (drakkari_scalebreaker) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 187_drakkari_molten_slam.jpg |
| 189 | Draconic Fury `drakkari_draconic_fury` | drakkari self / fire (drakkari_scalebreaker) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 188_drakkari_draconic_fury.jpg |
| 190 | Dragon Roar `drakkari_dragon_roar` | drakkari circle / fire (drakkari_scaleguard) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 189_drakkari_dragon_roar.jpg |
| 191 | Scale Ward `drakkari_scale_ward` | drakkari self / fire (drakkari_scaleguard) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 190_drakkari_scale_ward.jpg |
| 192 | Brood Oath `drakkari_brood_oath` | drakkari unit / fire (drakkari_scaleguard) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 191_drakkari_brood_oath.jpg |
| 193 | Shield Charge `drakkari_shield_charge` | drakkari line / fire (drakkari_scaleguard) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 192_drakkari_shield_charge.jpg |
| 194 | Flame Pillar `drakkari_flame_pillar` | drakkari circle / fire (drakkari_flamecaller) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 193_drakkari_flame_pillar.jpg |
| 195 | Ember Pool `drakkari_ember_pool` | drakkari circle / fire (drakkari_flamecaller) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 194_drakkari_ember_pool.jpg |
| 196 | Cauterize `drakkari_cauterize` | drakkari unit / fire (drakkari_flamecaller) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 195_drakkari_cauterize.jpg |
| 197 | Searing Breath `drakkari_searing_breath` | drakkari cone / fire (drakkari_flamecaller) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 196_drakkari_searing_breath.jpg |
| 198 | Fire Rain `drakkari_fire_rain` | drakkari circle / fire (drakkari_wingshot) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 197_drakkari_fire_rain.jpg |
| 199 | Wing Leap `drakkari_wing_leap` | drakkari self / fire (drakkari_wingshot) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 198_drakkari_wing_leap.jpg |
| 200 | Incendiary Bolt `drakkari_incendiary_bolt` | drakkari circle / fire (drakkari_wingshot) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 199_drakkari_incendiary_bolt.jpg |
| 201 | Pinning Bolt `drakkari_pinning_bolt` | drakkari circle / fire (drakkari_wingshot) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 200_drakkari_pinning_bolt.jpg |
| 202 | Whelp Dive `drakkari_whelp_dive` | drakkari line / fire (ember_whelp) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 201_drakkari_whelp_dive.jpg |
| 203 | Cinder Spit `drakkari_cinder_spit` | drakkari cone / fire (ember_whelp) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 202_drakkari_cinder_spit.jpg |
| 204 | Whelp Frenzy `drakkari_whelp_frenzy` | drakkari self / fire (ember_whelp) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 203_drakkari_whelp_frenzy.jpg |
| 205 | Hatch the Brood `drakkari_hatch_the_brood` | drakkari self / fire (drakkari_broodmother) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 204_drakkari_hatch_the_brood.jpg |
| 206 | Flame Nova `drakkari_flame_nova` | drakkari circle / fire (drakkari_broodmother) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 205_drakkari_flame_nova.jpg |
| 207 | Wing Gust `drakkari_wing_gust` | drakkari circle / fire (drakkari_broodmother) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 206_drakkari_wing_gust.jpg |
| 208 | Magma Rain `drakkari_magma_rain` | drakkari circle / fire (drakkari_broodmother) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 207_drakkari_magma_rain.jpg |
| 209 | Broodmother's Wrath `drakkari_broodmother_wrath` | drakkari self / fire (drakkari_broodmother) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 208_drakkari_broodmother_wrath.jpg |
| 210 | Inferno Breath `drakkari_inferno_breath` | drakkari cone / fire (drakkari_ashwing) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 209_drakkari_inferno_breath.jpg |
| 211 | Tail Lash `drakkari_ashwing_tail` | drakkari cone / fire (drakkari_ashwing) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 210_drakkari_ashwing_tail.jpg |
| 212 | Ash Fall `drakkari_ash_fall` | drakkari circle / fire (drakkari_ashwing) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 211_drakkari_ash_fall.jpg |
| 213 | Scorching Stomp `drakkari_ashwing_stomp` | drakkari circle / fire (drakkari_ashwing) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 212_drakkari_ashwing_stomp.jpg |
| 214 | Molten Fury `drakkari_ashwing_fury` | drakkari self / fire (drakkari_ashwing) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 213_drakkari_ashwing_fury.jpg |
| 215 | Rune Strike `stoneborn_rune_strike` | stoneborn cone / arcane (rune_sentinel) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 214_stoneborn_rune_strike.jpg |
| 216 | Rune Rush `stoneborn_rune_rush` | stoneborn line / arcane (rune_sentinel) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 215_stoneborn_rune_rush.jpg |
| 217 | Static Pulse `stoneborn_static_pulse` | stoneborn circle / earth (rune_sentinel) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 216_stoneborn_static_pulse.jpg |
| 218 | Boulder Charge `stoneborn_boulder_charge` | stoneborn line / earth (granite_crusher) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 217_stoneborn_boulder_charge.jpg |
| 219 | Ground Pound `stoneborn_ground_pound` | stoneborn circle / earth (granite_crusher) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 218_stoneborn_ground_pound.jpg |
| 220 | Granite Smash `stoneborn_granite_smash` | stoneborn cone / earth (granite_crusher) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 219_stoneborn_granite_smash.jpg |
| 221 | Ether Overload `stoneborn_overload` | stoneborn self / arcane (granite_crusher) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 220_stoneborn_overload.jpg |
| 222 | Runic Challenge `stoneborn_runic_taunt` | stoneborn circle / arcane (bastion_golem) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 221_stoneborn_runic_taunt.jpg |
| 223 | Stoneskin `stoneborn_stoneskin` | stoneborn self / earth (bastion_golem) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 222_stoneborn_stoneskin.jpg |
| 224 | Warding Link `stoneborn_warding_link` | stoneborn unit / arcane (bastion_golem) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 223_stoneborn_warding_link.jpg |
| 225 | Tremor `stoneborn_tremor` | stoneborn circle / earth (bastion_golem) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 224_stoneborn_tremor.jpg |
| 226 | Rune Mine `stoneborn_rune_mine` | stoneborn circle / arcane (deepforge_runesmith) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 225_stoneborn_rune_mine.jpg |
| 227 | Forge Mending `stoneborn_forge_mending` | stoneborn unit / arcane (deepforge_runesmith) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 226_stoneborn_forge_mending.jpg |
| 228 | Arc Lattice `stoneborn_arc_lattice` | stoneborn circle / earth (deepforge_runesmith) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 227_stoneborn_arc_lattice.jpg |
| 229 | Molten Slag `stoneborn_molten_slag` | stoneborn circle / fire (deepforge_runesmith) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 228_stoneborn_molten_slag.jpg |
| 230 | Shard Volley `stoneborn_shard_volley` | stoneborn circle / arcane (crystal_ballista) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 229_stoneborn_shard_volley.jpg |
| 231 | Pinning Shard `stoneborn_pinning_shard` | stoneborn circle / earth (crystal_ballista) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 230_stoneborn_pinning_shard.jpg |
| 232 | Recoil Jump `stoneborn_recoil_jump` | stoneborn self / arcane (crystal_ballista) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 231_stoneborn_recoil_jump.jpg |
| 233 | Piercing Beam `stoneborn_piercing_beam` | stoneborn cone / arcane (crystal_ballista) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 232_stoneborn_piercing_beam.jpg |
| 234 | Ether Repair `stoneborn_ether_repair` | stoneborn unit / arcane (ether_mote) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 233_stoneborn_ether_repair.jpg |
| 235 | Shield Matrix `stoneborn_shield_matrix` | stoneborn unit / earth (ether_mote) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 234_stoneborn_shield_matrix.jpg |
| 236 | Overcharge `stoneborn_overcharge` | stoneborn circle / arcane (ether_mote) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 235_stoneborn_overcharge.jpg |
| 237 | Ether Burst `stoneborn_ether_burst` | stoneborn circle / earth (ether_mote) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 236_stoneborn_ether_burst.jpg |
| 238 | Forge Sentinels `stoneborn_forge_sentinels` | stoneborn self / arcane (stoneborn_forgelord) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 237_stoneborn_forge_sentinels.jpg |
| 239 | Slag Eruption `stoneborn_slag_eruption` | stoneborn circle / fire (stoneborn_forgelord) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 238_stoneborn_slag_eruption.jpg |
| 240 | Anvil Cleave `stoneborn_anvil_cleave` | stoneborn cone / earth (stoneborn_forgelord) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 239_stoneborn_anvil_cleave.jpg |
| 241 | Runic Shockwave `stoneborn_runic_shockwave` | stoneborn circle / earth (stoneborn_forgelord) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 240_stoneborn_runic_shockwave.jpg |
| 242 | Forge Fury `stoneborn_forge_fury` | stoneborn self / fire (stoneborn_forgelord) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 241_stoneborn_forge_fury.jpg |
| 243 | Quake `stoneborn_quake` | stoneborn circle / earth (stoneborn_colossus) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 242_stoneborn_quake.jpg |
| 244 | Boulder Toss `stoneborn_boulder_toss` | stoneborn circle / earth (stoneborn_colossus) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 243_stoneborn_boulder_toss.jpg |
| 245 | Colossal Crush `stoneborn_crush` | stoneborn cone / earth (stoneborn_colossus) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 244_stoneborn_crush.jpg |
| 246 | Petrifying Gaze `stoneborn_petrify` | stoneborn circle / earth (stoneborn_colossus) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 245_stoneborn_petrify.jpg |
| 247 | Awakened Wrath `stoneborn_colossus_rage` | stoneborn self / arcane (stoneborn_colossus) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 246_stoneborn_colossus_rage.jpg |
| 248 | Pounce `feral_pounce` | feral_kin line / nature (dire_wolf) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 247_feral_pounce.jpg |
| 249 | Hamstring `feral_hamstring` | feral_kin cone / nature (dire_wolf) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 248_feral_hamstring.jpg |
| 250 | Pack Howl `feral_pack_howl` | feral_kin circle / blood (dire_wolf) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 249_feral_pack_howl.jpg |
| 251 | Savage Maul `feral_maul` | feral_kin cone / nature (werebear_mauler) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 250_feral_maul.jpg |
| 252 | Bear Charge `feral_bear_charge` | feral_kin line / nature (werebear_mauler) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 251_feral_bear_charge.jpg |
| 253 | Thick Hide `feral_thick_hide` | feral_kin self / nature (werebear_mauler) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 252_feral_thick_hide.jpg |
| 254 | Feral Rage `feral_rage` | feral_kin self / blood (werebear_mauler) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 253_feral_rage.jpg |
| 255 | Trumpeting Challenge `feral_trumpet` | feral_kin circle / nature (tusked_behemoth) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 254_feral_trumpet.jpg |
| 256 | Trample `feral_trample` | feral_kin line / nature (tusked_behemoth) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 255_feral_trample.jpg |
| 257 | Herd Guard `feral_herd_guard` | feral_kin unit / nature (tusked_behemoth) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 256_feral_herd_guard.jpg |
| 258 | Earthshaker `feral_earthshaker` | feral_kin circle / nature (tusked_behemoth) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 257_feral_earthshaker.jpg |
| 259 | Spirit Mending `feral_spirit_mending` | feral_kin unit / nature (feral_shaman) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 258_feral_spirit_mending.jpg |
| 260 | Savage Chant `feral_savage_totem` | feral_kin circle / blood (feral_shaman) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 259_feral_savage_totem.jpg |
| 261 | Bramble Hex `feral_thorn_hex` | feral_kin circle / nature (feral_shaman) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 260_feral_thorn_hex.jpg |
| 262 | Feral Spirits `feral_spirits` | feral_kin self / nature (feral_shaman) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 261_feral_spirits.jpg |
| 263 | Volley `feral_volley` | feral_kin circle / nature (wild_outrider) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 262_feral_volley.jpg |
| 264 | Gallop Away `feral_gallop_away` | feral_kin self / nature (wild_outrider) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 263_feral_gallop_away.jpg |
| 265 | Trampling Charge `feral_trample_charge` | feral_kin line / nature (wild_outrider) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 264_feral_trample_charge.jpg |
| 266 | Crippling Arrow `feral_crippling_arrow` | feral_kin circle / nature (wild_outrider) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 265_feral_crippling_arrow.jpg |
| 267 | Gore Rush `feral_boar_gore` | feral_kin line / nature (bristleback) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 266_feral_boar_gore.jpg |
| 268 | Bristle Burst `feral_bristle_burst` | feral_kin circle / nature (bristleback) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 267_feral_bristle_burst.jpg |
| 269 | Boar Frenzy `feral_boar_frenzy` | feral_kin self / blood (bristleback) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 268_feral_boar_frenzy.jpg |
| 270 | Elder Roar `feral_elder_roar` | feral_kin circle / blood (feral_ursoth) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 269_feral_elder_roar.jpg |
| 271 | Rending Maul `feral_rending_maul` | feral_kin cone / nature (feral_ursoth) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 270_feral_rending_maul.jpg |
| 272 | Crushing Charge `feral_ursoth_charge` | feral_kin line / nature (feral_ursoth) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 271_feral_ursoth_charge.jpg |
| 273 | Call of the Wild `feral_call_of_the_wild` | feral_kin self / nature (feral_ursoth) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 272_feral_call_of_the_wild.jpg |
| 274 | Elder Fury `feral_elder_fury` | feral_kin self / blood (feral_ursoth) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 273_feral_elder_fury.jpg |
| 275 | Titan Stomp `feral_mammoth_stomp` | feral_kin circle / nature (feral_mammoth) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 274_feral_mammoth_stomp.jpg |
| 276 | Stampede `feral_mammoth_trample` | feral_kin line / nature (feral_mammoth) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 275_feral_mammoth_trample.jpg |
| 277 | Great Tusk Sweep `feral_tusk_sweep` | feral_kin cone / nature (feral_mammoth) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 276_feral_tusk_sweep.jpg |
| 278 | Earthquake `feral_earthquake` | feral_kin circle / nature (feral_mammoth) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 277_feral_earthquake.jpg |
| 279 | Primal Rage `feral_mammoth_rage` | feral_kin self / blood (feral_mammoth) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 278_feral_mammoth_rage.jpg |
| 280 | Broken Oath `fallen_oath_strike` | fallen_order cone / shadow (fallen_squire) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 279_fallen_oath_strike.jpg |
| 281 | Buckler Rush `fallen_shield_rush` | fallen_order line / shadow (fallen_squire) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 280_fallen_shield_rush.jpg |
| 282 | Last Stand `fallen_last_stand` | fallen_order self / shadow (fallen_squire) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 281_fallen_last_stand.jpg |
| 283 | Dark Cleave `fallen_dark_cleave` | fallen_order cone / shadow (dread_knight) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 282_fallen_dark_cleave.jpg |
| 284 | Deathcharge `fallen_deathcharge` | fallen_order line / shadow (dread_knight) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 283_fallen_deathcharge.jpg |
| 285 | Unholy Frenzy `fallen_unholy_frenzy` | fallen_order self / shadow (dread_knight) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 284_fallen_unholy_frenzy.jpg |
| 286 | Chains of Penance `fallen_chain_grasp` | fallen_order line / shadow (dread_knight) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 285_fallen_chain_grasp.jpg |
| 287 | Judgment `fallen_judgment_taunt` | fallen_order circle / shadow (oathbreaker_templar) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 286_fallen_judgment_taunt.jpg |
| 288 | Profane Aegis `fallen_consecrated_wall` | fallen_order self / shadow (oathbreaker_templar) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 287_fallen_consecrated_wall.jpg |
| 289 | Martyr's Oath `fallen_martyrs_oath` | fallen_order unit / shadow (oathbreaker_templar) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 288_fallen_martyrs_oath.jpg |
| 290 | Profane Consecration `fallen_consecrate` | fallen_order circle / shadow (oathbreaker_templar) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 289_fallen_consecrate.jpg |
| 291 | Profane Light `fallen_profane_light` | fallen_order circle / shadow (blighted_chaplain) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 290_fallen_profane_light.jpg |
| 292 | Dark Absolution `fallen_dark_absolution` | fallen_order unit / shadow (blighted_chaplain) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 291_fallen_dark_absolution.jpg |
| 293 | Censer Smoke `fallen_censer_smoke` | fallen_order circle / shadow (blighted_chaplain) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 292_fallen_censer_smoke.jpg |
| 294 | Hymn of Wrath `fallen_hymn_of_wrath` | fallen_order circle / shadow (blighted_chaplain) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 293_fallen_hymn_of_wrath.jpg |
| 295 | Purging Bolts `fallen_purging_bolts` | fallen_order circle / shadow (fallen_inquisitor_crossbow) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 294_fallen_purging_bolts.jpg |
| 296 | Shackle Bolt `fallen_shackle_bolt` | fallen_order circle / shadow (fallen_inquisitor_crossbow) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 295_fallen_shackle_bolt.jpg |
| 297 | Tactical Retreat `fallen_tactical_retreat` | fallen_order self / shadow (fallen_inquisitor_crossbow) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 296_fallen_tactical_retreat.jpg |
| 298 | Condemn `fallen_condemn` | fallen_order circle / shadow (fallen_inquisitor_crossbow) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 297_fallen_condemn.jpg |
| 299 | Zealous Frenzy `fallen_zealous_frenzy` | fallen_order self / shadow (flagellant) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 298_fallen_zealous_frenzy.jpg |
| 300 | Zealous Leap `fallen_zealous_leap` | fallen_order line / shadow (flagellant) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 299_fallen_zealous_leap.jpg |
| 301 | Mortification `fallen_self_mortify` | fallen_order circle / shadow (flagellant) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 300_fallen_self_mortify.jpg |
| 302 | Chains of Judgment `fallen_chains_of_judgment` | fallen_order line / shadow (fallen_high_inquisitor) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 301_fallen_chains_of_judgment.jpg |
| 303 | Anathema `fallen_mass_silence` | fallen_order circle / shadow (fallen_high_inquisitor) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 302_fallen_mass_silence.jpg |
| 304 | Heretic's Pyre `fallen_pyre` | fallen_order circle / shadow (fallen_high_inquisitor) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 303_fallen_pyre.jpg |
| 305 | Call the Penitent `fallen_summon_flagellants` | fallen_order self / shadow (fallen_high_inquisitor) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 304_fallen_summon_flagellants.jpg |
| 306 | Righteous Fury `fallen_inquisitor_zeal` | fallen_order self / shadow (fallen_high_inquisitor) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 305_fallen_inquisitor_zeal.jpg |
| 307 | Sundering Verdict `fallen_crusader_cleave` | fallen_order cone / shadow (fallen_crusader) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 306_fallen_crusader_cleave.jpg |
| 308 | Crusade `fallen_crusade` | fallen_order line / shadow (fallen_crusader) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 307_fallen_crusade.jpg |
| 309 | Judgment Slam `fallen_judgment_slam` | fallen_order circle / shadow (fallen_crusader) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 308_fallen_judgment_slam.jpg |
| 310 | Aegis of Ruin `fallen_crusader_aegis` | fallen_order self / shadow (fallen_crusader) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 309_fallen_crusader_aegis.jpg |
| 311 | Fallen Wrath `fallen_crusader_wrath` | fallen_order self / shadow (fallen_crusader) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 310_fallen_crusader_wrath.jpg |
| 312 | Phase Strike `void_phase_strike` | voidborn line / void (rift_stalker) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 311_void_phase_strike.jpg |
| 313 | Void Rend `void_rend` | voidborn cone / void (rift_stalker) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 312_void_rend.jpg |
| 314 | Blink `void_blink` | voidborn self / void (rift_stalker) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 313_void_blink.jpg |
| 315 | Gravity Slam `void_gravity_slam` | voidborn circle / void (void_ravager) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 314_void_gravity_slam.jpg |
| 316 | Rift Charge `void_rift_charge` | voidborn line / void (void_ravager) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 315_void_rift_charge.jpg |
| 317 | Null Cleave `void_null_cleave` | voidborn cone / void (void_ravager) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 316_void_null_cleave.jpg |
| 318 | Unravel `void_unravel` | voidborn self / void (void_ravager) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 317_void_unravel.jpg |
| 319 | Void Gaze `void_gaze` | voidborn circle / void (null_warden) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 318_void_gaze.jpg |
| 320 | Event Horizon `void_event_horizon` | voidborn self / void (null_warden) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 319_void_event_horizon.jpg |
| 321 | Void Tether `void_tether` | voidborn unit / void (null_warden) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 320_void_tether.jpg |
| 322 | Gravity Well `void_gravity_well` | voidborn circle / void (null_warden) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 321_void_gravity_well.jpg |
| 323 | Void Rift `void_rift` | voidborn circle / void (rift_weaver) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 322_void_rift.jpg |
| 324 | Mind Spike `void_mind_spike` | voidborn circle / void (rift_weaver) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 323_void_mind_spike.jpg |
| 325 | Unmaking `void_unmaking` | voidborn cone / void (rift_weaver) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 324_void_unmaking.jpg |
| 326 | Rift Mending `void_rift_mending` | voidborn unit / void (rift_weaver) | Family visual at the target unit. | - | School impact at the ally/target; wind-up at the caster. | 325_void_rift_mending.jpg |
| 327 | Disintegrate `void_disintegrate` | voidborn cone / void (rift_gazer) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 326_void_disintegrate.jpg |
| 328 | Void Orb `void_orb` | voidborn circle / void (rift_gazer) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 327_void_orb.jpg |
| 329 | Warp `void_warp` | voidborn self / void (rift_gazer) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 328_void_warp.jpg |
| 330 | Paralyzing Gaze `void_paralyze_gaze` | voidborn circle / void (rift_gazer) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 329_void_paralyze_gaze.jpg |
| 331 | Void Burst `void_voidling_burst` | voidborn circle / void (voidling) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 330_void_voidling_burst.jpg |
| 332 | Latch `void_latch` | voidborn line / void (voidling) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 331_void_latch.jpg |
| 333 | Hunger `void_voidling_frenzy` | voidborn self / void (voidling) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 332_void_voidling_frenzy.jpg |
| 334 | Open the Rift `void_open_the_rift` | voidborn self / void (voidborn_herald) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 333_void_open_the_rift.jpg |
| 335 | Silence of the Stars `void_silence_of_stars` | voidborn circle / void (voidborn_herald) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 334_void_silence_of_stars.jpg |
| 336 | Collapsing Star `void_collapsing_star` | voidborn circle / void (voidborn_herald) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 335_void_collapsing_star.jpg |
| 337 | Grasp of the Void `void_herald_grasp` | voidborn line / void (voidborn_herald) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 336_void_herald_grasp.jpg |
| 338 | Ascension `void_herald_ascension` | voidborn self / void (voidborn_herald) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 337_void_herald_ascension.jpg |
| 339 | Devour `void_devour` | voidborn line / void (voidborn_devourer) | Flat amber rectangle from the caster. | No arrow/direction or timing cue. | Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length. | 338_void_devour.jpg |
| 340 | Singularity `void_singularity` | voidborn circle / void (voidborn_devourer) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 339_void_singularity.jpg |
| 341 | Void Breath `void_breath` | voidborn cone / void (voidborn_devourer) | Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim. | No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost. | Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim. | 340_void_breath.jpg |
| 342 | Titan Slam `void_titan_slam` | voidborn circle / void (voidborn_devourer) | Flat amber disc; family ring at the victim. | No timing cue; duplicate marker. | Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active. | 341_void_titan_slam.jpg |
| 343 | Endless Hunger `void_devourer_hunger` | voidborn self / void (voidborn_devourer) | Cast cue ring drawn at the victim's feet or the steel crescent. | Wrong place; unclear it is a self buff. | School wind-up motes and rune ring at the caster; aura from BuffVisuals on release. | 342_void_devourer_hunger.jpg |
<!-- audit-table:end -->

## Playtest 2: themed rune telegraphs, heal style, void zones, CC visuals

Eric: "they are just shaded colours and not specific to what they might do... detailed runes in the colour
of the damage type... healing-related things should also be obvious."

- **Rune sets per school** (`CireAbilityRunes.cpp`, `CireAbilityVFX::PaintRunes`): every ground telegraph
  (aim preview, own-team zone, enemy warning, lingering pool, self shock) carries a framed band of school
  glyphs, an inward edge treatment and a centre sigil (inside the designated-spot marker for aimed/warned
  circles). Lines get glyphs marching along the lane; cones along their arc. Everything stays inside the true
  boundary (tested for every set on circle, cone, line and square).

  | School | Glyph | Edge treatment / motif |
  |---|---|---|
  | physical | etched crossed blades in a notched circle (dusty steel/earth tan) | etched ticks, drifting dust |
  | fire | flame glyph with inner tongue, flickering | flame licks |
  | cold | six-armed frost crystal | icy crystal spikes, white rim |
  | earth | angular stone rune split by a crack | jagged cracks running inward |
  | water / tide | two flowing waves | rolling wave scallops |
  | holy | radiant script (ring, rays, cross) | alternating rays of light |
  | shadow | thorny sigil with crescent crown | hooked thorns |
  | void | rift star with orbiting specks | twinkling starfield band |
  | poison | swelling / popping bubbles | bubbles rising off the rim |
  | nature | leaf knot (vesica, midrib, veins) | curling vine with leaf buds |
  | storm | lightning glyph | zigzag arcs re-rolled each beat |
  | arcane | circle with counter-rotating triangles | dashed rim with diamonds |
  | blood / spirit | drop with a cut / spiral wisp | drips / wisps |
  | **heal** | thick green "+" with gold outline | small gold/green "+" shimmering upward |

- **Heals are unmistakable**: green/gold cross runes on the ground, calm breathing, and "+" motes rising
  around the healed unit (sanctuary, renewal, restoring light, purify, wellspring, second wind, last stand,
  bastion's self heal, monster heal-ally casts). **Buff zones are calm** (slow breathing, no sharp pulse);
  **damage zones are sharp**.
- **Enemy warnings keep amber urgency**: amber fill and rim, school runes inside on a heavy dark backing;
  amber-adjacent schools shift hue so they still read (fire deep red, earth/physical dark umber, holy white-gold).
- **Aim preview** is tinted and runed with the ability's school; invalid aim dims the runes toward red.
- **Void zones** (teleport/portal skills): outer ring = slow (dashed rim, void starfield glyphs, slow double-
  chevrons), inner circle = stun (solid double ring, orbiting stun stars, rift swirl), optional heal "+".
  Radii come from the Ability Database (`CireAbilityDB`, `Abilities.json` `void`: Shadow Step 180 cm stun /
  420 cm slow, the planned portal skills 160/380). Shown **while aiming** (ground-aimed portals in the cursor
  preview; targeted Shadow Step when its action-bar button is hovered with a hostile target, at its landing
  spot 170 cm in front of the target), **on cast** (the authoritative `void_rift` cue from
  `CireCrowdControl::VoidBurst`, exactly at the rift centre, violet for your team) and as **amber monster
  warnings** (voidborn `void_blink`/`void_warp` keep a stub 240/100 zone until they get DB entries).
- **School source**: the Ability Database `school` (physical, fire, cold, earth, tide, holy, shadow, void,
  poison, nature, storm, arcane) and its HEAL type; the built-in `CireAbilityShapes` mapping covers monsters
  and anything the DB does not list. Database changes: Spectral Hunt is void, Wellspring tide.
- **Heal cast times**: timed casts (Restoring Light 1.5 s, Sanctuary 2 s, Purify 1 s, Wellspring 2 s,
  Renewal 2.5 s) show the heal telegraph (true radius for self circles, a heal rune ring on the target
  otherwise) for the whole cast bar with a progress fill, and drop it on cancel/interrupt.
- **CC visuals** on the champion-draft buff ids: `stunned` (daze stars, existing), `silenced` / `npc_silenced`
  sealed-mouth glyph, `healing_cut` / `heal_cut_done` broken green cross over the unit, `interrupted` keeps
  its school-lock runes plus the shatter flash (`npc_interrupted` cue) where the cast circle breaks, root unchanged.
- Budgets: 6144 ground vertices per effect (was 3072), same 64-actor / 8-light caps.
- Tests (in `CireAbilityVFX::RunTests`, 3498 checks): distinct glyph per set, a rune set for every ability,
  heal style for every heal, buffs calm / damage sharp, runes inside every boundary, amber + school runes on
  enemy warnings, every database school resolves and drives the rune set, void zones draw both radii with icons
  in the armed preview, the Shadow Step hover preview, the void_rift cue and as a monster warning, heal casts
  show the heal telegraph during the cast and drop it on cancel, and everything cleans up.
- Evidence: before/after sheets `Saved/AbilityVFX/compare-20260925T064342Z` (32 abilities: previous pass vs runes),
  captures `Saved/AbilityVFX/runes4-all-20260925-064126`, Shadow Step rift `Saved/AbilityVFX/runes5-champion-20260925-064509`.
- Gallery: `python Tools/RunAbilityVFXGallery.py --only <ids> --tag runes` (`--db file.json` previews DB overrides).

### Readability pass (gameplay camera)
Captured from the player rig (`--camera gameplay`: 650 cm boom, FOV 80, pitches -20 and -50). Glyphs now scale
with the telegraph (ring glyph about 23% of the radius; fewer, larger glyphs, at most 10 per ring), the centre
sigil is about 26% of the radius, cone glyphs fill the wedge, lane glyphs are about 42% of the lane width,
edge motifs are thicker and scale with the shape, and the aim fill and rim keep the school hue. Evidence:
`Saved/AbilityVFX/gpb20-all-20260925-065818`, `gpb50-all-20260925-065914`, `gpc50-all-20260925-070043`.
Shadow Step's rift is violet for the caster's team (the earlier gold capture predated the allegiance fix, 45876e0).

### Playtest 3: brightness
- Fills are a subtle translucent tint (about 15-25% at the default intensity); readability comes from the
  crisp rim, the school runes and the edge motion. `CireAbilityVFX::Temper` scales fill alpha by
  intensity x overlap, keeps rims/runes stronger, and hue-caps every colour (fill 0.9, rim 1.2) under the
  bloom threshold. Release flashes of active zones no longer go near-solid.
- Overlap: each area visual counts the zones overlapping it; they share one brightness budget
  (1/sqrt(count) for fills, count^-0.75 for rims/runes). Buff fields such as pylons show runes at 35%.
- Options > Graphics: **Ground telegraph intensity** (0.3-1.0, default 0.6, saved in the profile; superseded 2026-09-26: 0.1-1.0, default 0.3, see below); the aura
  slider is now labelled **Ally / other units' effects**.
- Evidence (gameplay camera): `Saved/AbilityVFX/dim3-all-20260925-133126` including `overlap_town`
  (4 pylon fields + poison pool + fire cone + an enemy cleave on the town road under dusk lighting).
- The Niagara packs Eric is buying will replace much of this procedural art in a later pass.

## Telegraphs pass (2026-09-26): circles only, see-through overlay

Eric: "the ground effect can come in square form, which should be changed to circular" and the overlay is
"great, but about 50% too bright and covering".

- **Circles only.** Ashen Ward (`ashen_square`) was the one authored square ground AoE (a 500 cm square turned
  with the aim). It is now a 280 cm circle (its Ability Database radius), and any "square" ground area in data
  loads or imports as a circle. Rectangles remain only for true line hit shapes (skillshots, charges, pulls) and
  constructs with box collision (Summoned Wall, the protection box).
- **Fab ground overlays** (the area role in `FabVFX.json`) decorate circle zones only, never lines, cones,
  polygons or pylon fields. They are fitted to 92% of the true radius and dimmed with the slider. The list is
  curated: `groundRadius` holds each system's native radius, measured by `Tools/RunSpellGallery.py --fab-ground`.
  `groundExcluded` holds the systems that draw non-circular frames: holy `NS_Light_Magic_AOE1` and
  `NS_Light_Magic_Circle` draw a square frame with diamond corners, and the `NS_AreaBuff` tendrils ignore scale.
  Holy and heal circles therefore keep the procedural rune circle.
- **Terrain.** Zones follow sloped ground on a 9x9 height grid instead of being clipped into a chord.
- **Brightness.** Options > Graphics > **Ground telegraph intensity** is now 0.1-1.0 with a default of **0.3**
  (previously 0.3-1.0, default 0.6). Old profiles are halved on load, so 0.8 becomes 0.4. Fill and rim emissive
  caps drop from 0.9/1.2 to 0.6/0.95, and the rim alpha floor is 0.42. Enemy warnings keep a readable rim even
  at the lowest setting. Dense edge bands are narrower, the pylon minimum fill drops from 0.15 to 0.05, and area
  particles, bloom and the fallback flat mesh all follow the slider.
- **Shape audit** (`CireAbilityVFX::RunTests` section 11, part of `Tools/RunExpansionChecks.py`). The audit
  paints every ability's hit shape with the real painter: Ability Database entries, champion extras, every
  monster archetype skill, the tech constructs, and the summons.
  - It casts 72 rays from the centre and keeps the farthest crossing of any painted triangle edge, so a circle
    reads about 1.00 and a square 1.41.
  - The first probe sampled the farthest *vertex* per angular bin instead. That misread true circles as 1.31
    whenever the rim had fewer vertices than bins.
  - The gate fails on any square ground shape or any circle whose roundness is 1.08 or more.
  - It writes `Saved/ShapeAudit/shape_audit.json`, and `Tools/BuildShapeAudit.py` renders that file into the
    table below.
- Evidence (after, 2026-09-26): champion abilities `Saved/AbilityVFX/after-champion-20260926-071919` (492 captures;
  Ashen Ward is `023_ashen_square_*`), monster set `Saved/AbilityVFX/after-monster-all-20260926-072630` (before:
  `before-monster-all-20260926-031253`), spell gallery `Saved/SpellGallery/20260926-071634` (before: `20260926-031117`),
  Fab ground sheets `Saved/SpellGallery/20260926-071747` (stock vs fitted vs candidates), in-game wave fight
  `Saved/UIWaveCapture/20260926-072939` (before: `20260926-031452`).

## Shape audit (telegraphs, 2026-09-26)

<!-- shape-audit:start -->

547 entries (111 ability active, 36 ability passive, 27 ability ultimate, 4 champion extra, 11 construct, 349 monster, 5 monster construct, 4 summon / construct). Hit shapes: 2 chain, 181 circle, 55 cone, 5 custom, 63 line, 105 none, 88 self, 48 unit. Square ground shapes: **0**; circles not painted round: **0**.

Roundness = largest / smallest painted radius over 72 angular bins (1.00 = perfect circle; a square paints 1.41).

| # | Ability | Group | Status | Hit shape | Size (cm) | Rendered shape | Roundness | School | Fab ground overlay |
|---|---|---|---|---|---|---|---|---|---|
| 1 | Aegis Pylon `aegis_pylon` | ability active | implemented | circle | r 450 | circle (soft edge) | 1.001 | arcane (heal) | none (NS_Light_Magic_Circle: square frame with diamond corners inside its ring (not a circle)) |
| 2 | Aether Mend `aether_mend` | ability active | implemented | unit | - | unit ring (circle) + heal crosses | - | arcane (heal) | none (Fab ground overlays only decorate circle zones) |
| 3 | Arc Mine `arc_mine` | ability active | implemented | circle | r 240 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 4 | Arcane Blunderbuss `arcane_blunderbuss` | ability active | implemented | cone | r 550, 60 deg | cone + chevrons | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 5 | Artillery `artillery` | ability active | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 6 | Ashen Ward `ashen_square` | ability active | implemented | circle | r 280 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 7 | Banishment `banishment` | ability active | implemented | unit | - | ground streak + unit ring (circle) | - | void | none (Fab ground overlays only decorate circle zones) |
| 8 | Rootbreaker Charge `bear_charge` | ability active | planned | none | - | none (passive) + void rings (circles r 380 / 160) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 9 | Ironroot Slumber `bear_hibernate` | ability active | planned | none | - | none (passive) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 10 | Gravewood Maul `bear_maul` | ability active | planned | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 11 | Deepwood Roar `bear_roar` | ability active | planned | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 12 | Totem Bulwark `behemoth_totem_bulwark` | ability active | planned | none | - | none (passive) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 13 | Totem Sweep `behemoth_totem_sweep` | ability active | planned | none | - | none (passive) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 14 | Tuskbreaker `behemoth_tusk_line` | ability active | planned | none | - | none (passive) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 15 | Blade Flurry `blade_flurry` | ability active | implemented | circle | r 420 | circle (soft edge) | 1.001 | steel | none (pack not installed) |
| 16 | Blight Sigil `blight_sigil` | ability active | implemented | custom | 520 x 430 | authored polygon | - | poison | none (Fab ground overlays only decorate circle zones) |
| 17 | Bouncing Glaive `bouncing_glaive` | ability active | implemented | chain | hop r 500 | arcs + hop ring (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 18 | Grove Javelin `centaur_grove_javelin` | ability active | planned | none | - | none (passive) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 19 | Herd Call `centaur_herd_call` | ability active | planned | none | - | none (passive) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 20 | Evergrove Trail `centaur_trailblaze` | ability active | planned | none | - | none (passive) + void rings (circles r 380 / 160) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 21 | Chain Spark `chain_spark` | ability active | implemented | chain | hop r 500 | arcs + hop ring (circle) | - | storm | none (Fab ground overlays only decorate circle zones) |
| 22 | Chieftain Hook `chieftain_axe_hook` | ability active | planned | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 23 | Blood-Oath Banner `chieftain_banner` | ability active | planned | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 24 | Cinder Cone `cinder_cone` | ability active | implemented | cone | r 600, 70 deg | cone + chevrons | - | fire | none (Fab ground overlays only decorate circle zones) |
| 25 | Cleaving Strike `cleaving_strike` | ability active | implemented | circle | r 320 | circle (soft edge) | 1.001 | steel | none (pack not installed) |
| 26 | Crescent Volley `crescent_volley` | ability active | implemented | line | 1400 x 80 | rectangle lane + arrow (projectile corridor) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 27 | Decimating Strike `decimating_strike` | ability active | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 28 | Disruption Pylon `disruption_pylon` | ability active | implemented | circle | r 450 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 29 | Dragon Oath `drakish_dragon_oath` | ability active | planned | none | - | none (passive) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 30 | Scale Guard `drakish_scale_guard` | ability active | planned | none | - | none (passive) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 31 | Wing Rebuke `drakish_wing_rebuke` | ability active | planned | none | - | none (passive) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 32 | Root Snare `dryad_root_snare` | ability active | planned | none | - | none (passive) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 33 | Seed Mend `dryad_seed_mend` | ability active | planned | none | - | none (passive) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 34 | Thornweave `dryad_thorn_line` | ability active | planned | none | - | none (passive) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 35 | Eagle Eye `eagle_eye` | ability active | implemented | none | - | none (passive) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 36 | Ember Lance `ember_lance` | ability active | implemented | line | 1200 x 60 | rectangle lane + arrow (projectile corridor) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 37 | Evasive Stance `evasive_stance` | ability active | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 38 | Frost Bind `frost_bind` | ability active | implemented | line | 1200 x 68 | rectangle lane + arrow (projectile corridor) | - | frost | none (Fab ground overlays only decorate circle zones) |
| 39 | Ether Anchor `golem_ether_anchor` | ability active | planned | none | - | none (passive) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 40 | Ether Furnace `golem_ether_furnace` | ability active | planned | none | - | none (passive) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 41 | Felfire Fist `golem_fel_fist` | ability active | planned | none | - | none (passive) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 42 | Granite Fist `golem_granite_fist` | ability active | planned | none | - | none (passive) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 43 | Living Granite `golem_living_granite` | ability active | planned | none | - | none (passive) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 44 | Verdant Bloom `golem_moss_bloom` | ability active | planned | none | - | none (passive) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 45 | Grave Line `grave_line` | ability active | implemented | line | 900 x 160 | rectangle lane + arrow | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 46 | Gravity Pylon `gravity_pylon` | ability active | implemented | circle | r 450 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 47 | Haste Pylon `haste_pylon` | ability active | implemented | circle | r 450 | circle (soft edge) | 1.001 | arcane (heal) | none (NS_Light_Magic_Circle: square frame with diamond corners inside its ring (not a circle)) |
| 48 | Hex Mark `hex_mark` | ability active | implemented | unit | - | ground streak + unit ring (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 49 | Hunter's Stride `hunters_stride` | ability active | implemented | circle | r 70 | circle (soft edge) | 1.001 | steel | none (pack not installed) |
| 50 | Iron Guard `iron_guard` | ability active | implemented | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 51 | Beacon of Return `keeper_beacon` | ability active | planned | none | - | none (passive) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 52 | Dawn Beam `keeper_dawn_beam` | ability active | planned | none | - | none (passive) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 53 | Lantern Ward `keeper_lantern_ward` | ability active | planned | none | - | none (passive) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 54 | Longshot Stance `longshot` | ability active | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 55 | Construct: Mechanical Tank `mechanical_tank` | ability active | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 56 | Caltrop Mine `mine_layer` | ability active | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 57 | Faultline `miner_faultline` | ability active | planned | none | - | none (passive) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 58 | Deep Lantern `miner_lantern` | ability active | planned | none | - | none (passive) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 59 | Pickfall `miner_pickfall` | ability active | planned | none | - | none (passive) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 60 | Moonlit Sprint `moonlit_sprint` | ability active | implemented | self | - | caster pulse (circle) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 61 | Oathbound Guardian `oathbound_guardian` | ability active | implemented | circle | r 45 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 62 | Overcharge `overcharge` | ability active | implemented | self | - | caster pulse (circle) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 63 | Owl Scout `owl_scout` | ability active | implemented | circle | r 450 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 64 | Merciful Censer `paladin_holy_flail` | ability active | planned | none | - | none (passive) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 65 | Pilgrim Light `paladin_pilgrim_light` | ability active | planned | none | - | none (passive) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 66 | Relic Vow `paladin_relic_vow` | ability active | planned | none | - | none (passive) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 67 | Righteous Flail `paladin_righteous_flail` | ability active | planned | none | - | none (passive) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 68 | Construct: Pavise `pavise` | ability active | implemented | none | - | none (passive) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 69 | Phase Lance `phase_lance` | ability active | implemented | line | 1300 x 60 | rectangle lane + arrow (projectile corridor) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 70 | Photon Turret `photon_turret` | ability active | implemented | circle | r 950 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 71 | Piercing Shot `piercing_shot` | ability active | implemented | line | 1500 x 36 | rectangle lane + arrow (projectile corridor) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 72 | Polymorph `polymorph` | ability active | implemented | none | - | none (passive) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 73 | Powder Flask `powder_flask` | ability active | implemented | circle | r 260 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 74 | Aegis Dome `protection_dome` | ability active | implemented | custom | 480 x 480 | rectangle (true box collision) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 75 | Purge `purge` | ability active | implemented | unit | - | ground streak + unit ring (circle) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 76 | Purify `purify` | ability active | implemented | unit | - | unit ring (circle) + heal crosses | - | holy (heal) | none (Fab ground overlays only decorate circle zones) |
| 77 | Repulsor Pulse `repulsor_pulse` | ability active | implemented | circle | r 350 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 78 | Restoring Light `restoring_light` | ability active | implemented | unit | - | unit ring (circle) + heal crosses | - | holy (heal) | none (Fab ground overlays only decorate circle zones) |
| 79 | Ashfang: Maul `sabercat_maul` | ability active | implemented | unit | - | ground streak + unit ring (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 80 | Ashfang: Pounce `sabercat_pounce` | ability active | implemented | unit | - | ground streak + unit ring (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 81 | Ashfang: Dread Roar `sabercat_roar` | ability active | implemented | self | - | caster pulse (circle) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 82 | Sanctuary `sanctuary` | ability active | implemented | circle | r 600 | circle (soft edge) | 1.001 | holy (heal) | none (NS_Light_Magic_Circle: square frame with diamond corners inside its ring (not a circle)) |
| 83 | Second Wind `second_wind` | ability active | implemented | self | - | caster pulse (circle) + heal crosses | - | nature (heal) | none (Fab ground overlays only decorate circle zones) |
| 84 | Shadow Dance `shadow_dance` | ability active | implemented | none | - | none (passive) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 85 | Shadow Step `shadow_step` | ability active | implemented | unit | - | ground streak + unit ring (circle) + void rings (circles r 420 / 180) | - | void | none (Fab ground overlays only decorate circle zones) |
| 86 | Shield Bash `shield_bash` | ability active | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 87 | Shield Slam `shield_slam` | ability active | implemented | unit | - | ground streak + unit ring (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 88 | Shield Toss `shield_toss` | ability active | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 89 | Shield Tumble `shield_tumble` | ability active | implemented | none | - | none (passive) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 90 | Shield Wall `shield_wall` | ability active | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 91 | Silver Shot `silver_shot` | ability active | implemented | line | 1300 x 56 | rectangle lane + arrow (projectile corridor) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 92 | Skitter Swarm `skitter_swarm` | ability active | implemented | circle | r 180 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 93 | Spectral Blade `spectral_blade` | ability active | implemented | unit | - | ground streak + unit ring (circle) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 94 | Spectral Pack `spectral_pack` | ability active | implemented | circle | r 230 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 95 | Spirit Lantern `spirit_lantern` | ability active | implemented | circle | r 260 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 96 | Stasis Snare `stasis_snare` | ability active | implemented | circle | r 150 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 97 | Runestone Wall `summoned_wall` | ability active | implemented | custom | 70 x 440 | rectangle (true box collision) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 98 | Taunting Tumble `taunting_tumble` | ability active | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 99 | Axe Frenzy `troll_axe_frenzy` | ability active | planned | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 100 | Bloodbound Leap `troll_blood_leap` | ability active | planned | none | - | none (passive) + void rings (circles r 380 / 160) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 101 | Returning Axes `troll_returning_axes` | ability active | planned | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 102 | Twin Throw `troll_twin_throw` | ability active | planned | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 103 | Tumble Strike `tumble_strike` | ability active | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 104 | Venom Ground `venom_ground` | ability active | implemented | circle | r 280 | circle (soft edge) | 1.001 | poison | NS_Posion_Magic_Area1: round, fitted inside the rim (native r 593), dimmed |
| 105 | Venom Tumble `venom_tumble` | ability active | implemented | none | - | none (passive) | - | poison | none (Fab ground overlays only decorate circle zones) |
| 106 | War Cry `war_cry` | ability active | implemented | circle | r 850 | circle (soft edge) | 1.001 | steel | none (pack not installed) |
| 107 | Warding Talisman `warding_talisman` | ability active | implemented | self | - | caster pulse (circle) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 108 | Fey Trail `whisp_fey_trail` | ability active | planned | none | - | none (passive) + void rings (circles r 380 / 160) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 109 | Guiding Mote `whisp_guiding_mote` | ability active | planned | none | - | none (passive) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 110 | Spirit Tether `whisp_spirit_tether` | ability active | planned | none | - | none (passive) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 111 | Witchfinder's Mark `witchfinders_mark` | ability active | implemented | unit | - | ground streak + unit ring (circle) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 112 | Aether Nexus `aether_nexus` | ability ultimate | implemented | circle | r 650 | circle (soft edge) | 1.001 | arcane (heal) | none (NS_Light_Magic_Circle: square frame with diamond corners inside its ring (not a circle)) |
| 113 | Bastion of Dawn `bastion_of_dawn` | ability ultimate | implemented | circle | r 650 | circle (soft edge) | 1.001 | holy (heal) | none (NS_Light_Magic_Circle: square frame with diamond corners inside its ring (not a circle)) |
| 114 | Elder of the Deepwood `bear_colossus` | ability ultimate | planned | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 115 | Ancestral Stampede `behemoth_stampede` | ability ultimate | planned | none | - | none (passive) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 116 | Cataclysm `cataclysm` | ability ultimate | implemented | circle | r 550 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 117 | Spring March `centaur_spring_march` | ability ultimate | planned | none | - | none (passive) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 118 | Challenge of Iron `challenge_of_iron` | ability ultimate | implemented | circle | r 850 | circle (soft edge) | 1.001 | steel | none (pack not installed) |
| 119 | Earthshout `chieftain_earthshout` | ability ultimate | planned | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 120 | Collect the Bounty `collect_the_bounty` | ability ultimate | implemented | unit | - | ground streak + unit ring (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 121 | Ancient Pact `drakish_ancient_pact` | ability ultimate | planned | none | - | none (passive) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 122 | Grove Renewal `dryad_grove_renewal` | ability ultimate | planned | none | - | none (passive) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 123 | Executioner's Verdict `executioners_verdict` | ability ultimate | implemented | unit | - | ground streak + unit ring (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 124 | Glaive Storm `glaive_storm` | ability ultimate | implemented | circle | r 480 | circle (soft edge) | 1.001 | steel | none (pack not installed) |
| 125 | Worldstone Awakened `golem_worldstone` | ability ultimate | planned | none | - | none (passive) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 126 | Hexbane Judgment `hexbane_judgment` | ability ultimate | implemented | circle | r 450 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 127 | Sunrise Vigil `keeper_sunrise` | ability ultimate | planned | none | - | none (passive) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 128 | Last Stand `last_stand` | ability ultimate | implemented | self | - | caster pulse (circle) + heal crosses | - | steel (heal) | none (Fab ground overlays only decorate circle zones) |
| 129 | Mass Aegis `mass_aegis` | ability ultimate | implemented | circle | r 900 | circle (soft edge) | 1.001 | holy (heal) | none (NS_Light_Magic_Circle: square frame with diamond corners inside its ring (not a circle)) |
| 130 | Heart of the Mountain `miner_mountain` | ability ultimate | planned | none | - | none (passive) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 131 | Renewal `renewal` | ability ultimate | implemented | circle | r 1000 | circle (soft edge) | 1.001 | holy (heal) | none (NS_Light_Magic_Circle: square frame with diamond corners inside its ring (not a circle)) |
| 132 | Seismic Reprisal `seismic_reprisal` | ability ultimate | implemented | circle | r 450 | circle (soft edge) | 1.001 | earth | NS_Earth_Spells_Circle: round, fitted inside the rim (native r 622), dimmed |
| 133 | Spectral Hunt `spectral_hunt` | ability ultimate | implemented | unit | - | ground streak + unit ring (circle) | - | void | none (Fab ground overlays only decorate circle zones) |
| 134 | Starfall `starfall` | ability ultimate | implemented | circle | r 500 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 135 | Red Moon Frenzy `troll_red_moon` | ability ultimate | planned | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 136 | Warp Obelisk `warp_obelisk` | ability ultimate | implemented | circle | r 1300 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 137 | Wellspring `wellspring` | ability ultimate | implemented | unit | - | unit ring (circle) + heal crosses | - | tide (heal) | none (Fab ground overlays only decorate circle zones) |
| 138 | Kindred Constellation `whisp_constellation` | ability ultimate | planned | none | - | none (passive) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 139 | Aether Engineering `aether_engineering` | ability passive | implemented | none | - | none (passive) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 140 | Artillery Training `artillery_training` | ability passive | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 141 | Battle Rhythm `battle_rhythm` | ability passive | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 142 | Ancient Hide `bear_ancient_hide` | ability passive | planned | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 143 | Ancestral Weight `behemoth_ancestral_weight` | ability passive | planned | none | - | none (passive) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 144 | Bloodrush `bloodrush` | ability passive | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 145 | Blur `blur_step` | ability passive | implemented | none | - | none (passive) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 146 | Steady Gait `centaur_steady_gait` | ability passive | planned | none | - | none (passive) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 147 | Unbroken Clan `chieftain_courage` | ability passive | planned | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 148 | Deep Reserves `deep_reserves` | ability passive | implemented | none | - | none (passive) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 149 | Ember Memory `drakish_ember_memory` | ability passive | planned | none | - | none (passive) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 150 | Green Covenant `dryad_green_covenant` | ability passive | planned | none | - | none (passive) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 151 | Ember Wake `ember_wake` | ability passive | implemented | none | - | none (passive) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 152 | Executioner `executioner` | ability passive | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 153 | Fleet Recovery `fleet_recovery` | ability passive | implemented | none | - | none (passive) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 154 | Frost Wake `frost_wake` | ability passive | implemented | none | - | none (passive) | - | frost | none (Fab ground overlays only decorate circle zones) |
| 155 | Construct Core `golem_construct_core` | ability passive | planned | none | - | none (passive) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 156 | Hasted Tumble `hasted_tumble` | ability passive | implemented | none | - | none (passive) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 157 | Headshot `headshot` | ability passive | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 158 | Last Light `keeper_last_light` | ability passive | planned | none | - | none (passive) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 159 | Killer Instinct `killer_instinct` | ability passive | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 160 | Orehide `miner_orehide` | ability passive | planned | none | - | none (passive) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 161 | Momentum `momentum` | ability passive | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 162 | Moon Glaive `moon_glaive` | ability passive | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 163 | Price on Every Soul `price_on_every_soul` | ability passive | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 164 | Quickened Mind `quickened_mind` | ability passive | implemented | none | - | none (passive) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 165 | Resonant Lattice `resonant_lattice` | ability passive | implemented | none | - | none (passive) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 166 | Riposte `riposte_roll` | ability passive | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 167 | Slippery `slippery_roll` | ability passive | implemented | none | - | none (passive) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 168 | Soul Conduit `soul_conduit` | ability passive | implemented | none | - | none (passive) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 169 | Stone Skin `stone_skin` | ability passive | implemented | none | - | none (passive) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 170 | Berserker Hunger `troll_hunger` | ability passive | planned | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 171 | Tumbler's Edge `tumblers_edge` | ability passive | implemented | none | - | none (passive) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 172 | Lantern Soul `whisp_lantern_soul` | ability passive | planned | none | - | none (passive) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 173 | Windrunner `windrunner` | ability passive | implemented | none | - | none (passive) | - | storm | none (Fab ground overlays only decorate circle zones) |
| 174 | Witchbane `witchbane` | ability passive | implemented | none | - | none (passive) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 175 | basic_arcane `basic_arcane` | champion extra | implemented | unit | - | ground streak + unit ring (circle) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 176 | basic_bow `basic_bow` | champion extra | implemented | unit | - | ground streak + unit ring (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 177 | basic_lance `basic_lance` | champion extra | implemented | unit | - | ground streak + unit ring (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 178 | basic_sword `basic_sword` | champion extra | implemented | unit | - | ground streak + unit ring (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 179 | Oathbound Guardian `oathbound_guardian` | summon / construct | implemented | circle | r 45 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 180 | Protection Dome `protection_dome` | summon / construct | implemented | custom | 480 x 480 | rectangle (true box collision) | - | holy | none (Fab ground overlays only decorate circle zones) |
| 181 | Spectral Pack `spectral_pack` | summon / construct | implemented | circle | r 230 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 182 | Summoned Wall `summoned_wall` | summon / construct | implemented | custom | 70 x 440 | rectangle (true box collision) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 183 | Aegis Pylon `aegis_pylon (field)` | construct | implemented | circle | r 450 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 184 | Aether Nexus `aether_nexus (field)` | construct | implemented | circle | r 650 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 185 | Arc Mine `arc_mine (field)` | construct | implemented | circle | r 240 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 186 | Disruption Pylon `disruption_pylon (field)` | construct | implemented | circle | r 450 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 187 | Gravity Pylon `gravity_pylon (field)` | construct | implemented | circle | r 450 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 188 | Haste Pylon `haste_pylon (field)` | construct | implemented | circle | r 450 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 189 | Photon Turret `photon_turret (field)` | construct | implemented | circle | r 950 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 190 | Skitter Swarm `skitter_swarm (field)` | construct | implemented | circle | r 200 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 191 | Spirit Lantern `spirit_lantern (field)` | construct | implemented | circle | r 260 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 192 | Stasis Snare `stasis_snare (field)` | construct | implemented | circle | r 150 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 193 | Warp Obelisk `warp_obelisk (field)` | construct | implemented | circle | r 1300 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 194 | Empowering Pylon `npc_empower_pylon (field)` | monster construct | implemented | circle | r 500 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 195 | Gravity Pylon `npc_gravity_pylon (field)` | monster construct | implemented | circle | r 450 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 196 | Warp Turret `npc_photon_turret (field)` | monster construct | implemented | circle | r 900 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 197 | Skitter Bomb `npc_skitter (field)` | monster construct | implemented | circle | r 200 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 198 | Stasis Mine `npc_stasis_mine (field)` | monster construct | implemented | circle | r 170 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 199 | Empowering Pylon `aether_bulwark_pylon` | monster | aetheri | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 200 | Thermal Lance `aether_colossus_beam` | monster | aetheri | cone | r 580, 50 deg | cone + chevrons | - | steel | none (Fab ground overlays only decorate circle zones) |
| 201 | Meltdown `aether_colossus_meltdown` | monster | aetheri | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 202 | Seismic Stomp `aether_colossus_stomp` | monster | aetheri | circle | r 420 | circle (soft edge) | 1.001 | steel | none (pack not installed) |
| 203 | Shoulder Turrets `aether_colossus_turrets` | monster | aetheri | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 204 | Core Burst `aether_core_burst` | monster | aetheri | circle | r 260 | circle (soft edge) | 1.001 | steel | none (pack not installed) |
| 205 | Skitter Swarm `aether_deploy_skitters` | monster | aetheri | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 206 | Deploy Turret `aether_deploy_turret` | monster | aetheri | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 207 | Field Repair `aether_field_repair` | monster | aetheri | unit | - | unit ring (circle) + heal crosses | - | steel (heal) | none (Fab ground overlays only decorate circle zones) |
| 208 | Gravity Field `aether_gravity_field` | monster | aetheri | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 209 | Hardlight Shell `aether_hardlight_shell` | monster | aetheri | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 210 | Final Protocol `aether_hierarch_ascension` | monster | aetheri | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 211 | Link Shield `aether_link_shield` | monster | aetheri | unit | - | ground streak + unit ring (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 212 | Orbital Strike `aether_orbital_strike` | monster | aetheri | circle | r 220 | circle (soft edge) | 1.001 | steel | none (pack not installed) |
| 213 | Overdrive `aether_overdrive` | monster | aetheri | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 214 | Phase Strike `aether_phase_strike` | monster | aetheri | line | 750 x 170 | rectangle lane + arrow | - | steel | none (Fab ground overlays only decorate circle zones) |
| 215 | Photon Beam `aether_photon_beam` | monster | aetheri | cone | r 760, 12 deg | cone + chevrons | - | steel | none (Fab ground overlays only decorate circle zones) |
| 216 | Prism Glare `aether_prism_glare` | monster | aetheri | circle | r 550 | circle (soft edge) | 1.001 | steel | none (pack not installed) |
| 217 | Psi Sweep `aether_psi_sweep` | monster | aetheri | cone | r 300, 110 deg | cone + chevrons | - | steel | none (Fab ground overlays only decorate circle zones) |
| 218 | Psionic Storm `aether_psionic_storm` | monster | aetheri | circle | r 520 | circle (soft edge) | 1.001 | steel | none (pack not installed) |
| 219 | Recall `aether_recall` | monster | aetheri | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 220 | Stasis Mine `aether_stasis_mine` | monster | aetheri | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 221 | Tractor Beam `aether_tractor_beam` | monster | aetheri | line | 1200 x 160 | rectangle lane + arrow | - | steel | none (Fab ground overlays only decorate circle zones) |
| 222 | Unstable Core `aether_unstable_core` | monster | aetheri | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 223 | Reactor Charge `aether_warframe_charge` | monster | aetheri | line | 900 x 170 | rectangle lane + arrow | - | earth | none (Fab ground overlays only decorate circle zones) |
| 224 | Warp In `aether_warp_in` | monster | aetheri | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 225 | Warp Pylons `aether_warp_pylons` | monster | aetheri | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 226 | Warp Slam `aether_warp_slam` | monster | aetheri | circle | r 360 | circle (soft edge) | 1.001 | earth | NS_Earth_Spells_Circle: round, fitted inside the rim (native r 622), dimmed |
| 227 | Warp Step `aether_warp_step` | monster | aetheri | self | - | caster pulse (circle) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 228 | Awaken Saplings `blight_awaken_saplings` | monster | blightwood | self | - | caster pulse (circle) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 229 | Barkskin Oath `blight_barkskin_oath` | monster | blightwood | unit | - | ground streak + unit ring (circle) | - | poison | none (Fab ground overlays only decorate circle zones) |
| 230 | Blight Bloom `blight_blight_bloom` | monster | blightwood | circle | r 300 | circle (soft edge) | 1.001 | poison | NS_Posion_Magic_Area1: round, fitted inside the rim (native r 593), dimmed |
| 231 | Briar Patch `blight_briar_patch` | monster | blightwood | circle | r 220 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 232 | Briar Rush `blight_briar_rush` | monster | blightwood | line | 750 x 170 | rectangle lane + arrow | - | nature | none (Fab ground overlays only decorate circle zones) |
| 233 | Choking Puffball `blight_choking_puff` | monster | blightwood | circle | r 240 | circle (soft edge) | 1.001 | poison | NS_Posion_Magic_Area1: round, fitted inside the rim (native r 593), dimmed |
| 234 | Crushing Bough `blight_crushing_bough` | monster | blightwood | cone | r 440, 115 deg | cone + chevrons | - | poison | none (Fab ground overlays only decorate circle zones) |
| 235 | Entangle `blight_entangle` | monster | blightwood | circle | r 230 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 236 | Grove Challenge `blight_grove_challenge` | monster | blightwood | circle | r 550 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 237 | Heartwood Fury `blight_heartwood_fury` | monster | blightwood | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 238 | Matron's Call `blight_matron_call` | monster | blightwood | self | - | caster pulse (circle) | - | poison | none (Fab ground overlays only decorate circle zones) |
| 239 | Mycelial Link `blight_mycelial_link` | monster | blightwood | unit | - | unit ring (circle) + heal crosses | - | poison (heal) | none (Fab ground overlays only decorate circle zones) |
| 240 | Oakheart Stomp `blight_oakheart_stomp` | monster | blightwood | circle | r 420 | circle (soft edge) | 1.001 | poison | NS_Posion_Magic_Area1: round, fitted inside the rim (native r 593), dimmed |
| 241 | Root Eruption `blight_root_eruption` | monster | blightwood | circle | r 300 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 242 | Root Hop `blight_root_hop` | monster | blightwood | self | - | caster pulse (circle) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 243 | Root Quake `blight_root_quake` | monster | blightwood | circle | r 360 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 244 | Rooted Stance `blight_rooted_stance` | monster | blightwood | self | - | caster pulse (circle) | - | poison | none (Fab ground overlays only decorate circle zones) |
| 245 | Rot Pool `blight_rot_pool` | monster | blightwood | circle | r 220 | circle (soft edge) | 1.001 | poison | NS_Posion_Magic_Area1: round, fitted inside the rim (native r 593), dimmed |
| 246 | Rotting Embrace `blight_rotting_embrace` | monster | blightwood | line | 1200 x 160 | rectangle lane + arrow | - | nature | none (Fab ground overlays only decorate circle zones) |
| 247 | Sap Frenzy `blight_sap_frenzy` | monster | blightwood | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 248 | Sap Mending `blight_sap_mending` | monster | blightwood | unit | - | unit ring (circle) + heal crosses | - | nature (heal) | none (Fab ground overlays only decorate circle zones) |
| 249 | Seedpod Mortar `blight_seedpod_mortar` | monster | blightwood | circle | r 230 | circle (soft edge) | 1.001 | poison | NS_Posion_Magic_Area1: round, fitted inside the rim (native r 593), dimmed |
| 250 | Splinter Burst `blight_splinter_burst` | monster | blightwood | circle | r 340 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 251 | Spore Cloud `blight_spore_cloud` | monster | blightwood | circle | r 260 | circle (soft edge) | 1.001 | poison | NS_Posion_Magic_Area1: round, fitted inside the rim (native r 593), dimmed |
| 252 | Spore Scatter `blight_spore_scatter` | monster | blightwood | self | - | caster pulse (circle) | - | poison | none (Fab ground overlays only decorate circle zones) |
| 253 | Strangling Grove `blight_strangling_grove` | monster | blightwood | circle | r 480 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 254 | Strangling Vines `blight_strangling_vines` | monster | blightwood | circle | r 220 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 255 | Stump Slam `blight_stump_slam` | monster | blightwood | cone | r 360, 80 deg | cone + chevrons | - | poison | none (Fab ground overlays only decorate circle zones) |
| 256 | Thorn Burst `blight_thorn_burst` | monster | blightwood | cone | r 460, 60 deg | cone + chevrons | - | nature | none (Fab ground overlays only decorate circle zones) |
| 257 | Thorn Volley `blight_thorn_volley` | monster | blightwood | circle | r 200 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 258 | Thornstorm `blight_thornstorm` | monster | blightwood | circle | r 320 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 259 | Uproot Charge `blight_uproot_charge` | monster | blightwood | line | 900 x 170 | rectangle lane + arrow | - | poison | none (Fab ground overlays only decorate circle zones) |
| 260 | Thorn Whip `blight_vine_lash` | monster | blightwood | cone | r 340, 85 deg | cone + chevrons | - | nature | none (Fab ground overlays only decorate circle zones) |
| 261 | Vine Snare `blight_vine_snare` | monster | blightwood | line | 850 x 160 | rectangle lane + arrow | - | nature | none (Fab ground overlays only decorate circle zones) |
| 262 | Wither Curse `blight_wither_curse` | monster | blightwood | circle | r 240 | circle (soft edge) | 1.001 | poison | NS_Posion_Magic_Area1: round, fitted inside the rim (native r 593), dimmed |
| 263 | Withering Wrath `blight_withering_wrath` | monster | blightwood | self | - | caster pulse (circle) | - | poison | none (Fab ground overlays only decorate circle zones) |
| 264 | Bone Volley `bone_volley` | monster | hollow | circle | r 200 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 265 | Brutal Charge `boss_leader_charge` | monster | hollow | line | 1250 x 200 | rectangle lane + arrow | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 266 | Sundering Cleave `boss_leader_cleave` | monster | hollow | cone | r 440, 120 deg | cone + chevrons | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 267 | Blood Frenzy `boss_leader_frenzy` | monster | hollow | self | - | caster pulse (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 268 | Rallying Roar `boss_leader_rally` | monster | hollow | circle | r 1200 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 269 | Sundering Cleave `boss_siege_cleave` | monster | hollow | cone | r 420, 110 deg | cone + chevrons | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 270 | Siege Fury `boss_siege_fury` | monster | hollow | self | - | caster pulse (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 271 | Rallying Bellow `boss_siege_rally` | monster | hollow | circle | r 1000 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 272 | Siege Stomp `boss_siege_stomp` | monster | hollow | circle | r 380 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 273 | Horned Cleave `brute_cleave` | monster | ironhide | cone | r 340, 85 deg | cone + chevrons | - | blood | none (Fab ground overlays only decorate circle zones) |
| 274 | Frenzy `brute_frenzy` | monster | ironhide | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 275 | Leap Slam `brute_leap` | monster | ironhide | line | 850 x 180 | rectangle lane + arrow | - | blood | none (Fab ground overlays only decorate circle zones) |
| 276 | Blade Sweep `centaur_sweep` | monster | feral_kin | cone | r 330, 100 deg | cone + chevrons | - | nature | none (Fab ground overlays only decorate circle zones) |
| 277 | Trample `centaur_trample` | monster | feral_kin | line | 900 x 190 | rectangle lane + arrow | - | nature | none (Fab ground overlays only decorate circle zones) |
| 278 | Cinder Breath `drake_breath` | monster | cinder_drake | cone | r 480, 55 deg | cone + chevrons | - | fire | none (Fab ground overlays only decorate circle zones) |
| 279 | Ember Spit `drake_embers` | monster | cinder_drake | circle | r 220 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 280 | Ash Fall `drakkari_ash_fall` | monster | drakkari | circle | r 300 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 281 | Molten Fury `drakkari_ashwing_fury` | monster | drakkari | self | - | caster pulse (circle) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 282 | Scorching Stomp `drakkari_ashwing_stomp` | monster | drakkari | circle | r 400 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 283 | Tail Lash `drakkari_ashwing_tail` | monster | drakkari | cone | r 400, 140 deg | cone + chevrons | - | fire | none (Fab ground overlays only decorate circle zones) |
| 284 | Brood Oath `drakkari_brood_oath` | monster | drakkari | unit | - | ground streak + unit ring (circle) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 285 | Broodmother's Wrath `drakkari_broodmother_wrath` | monster | drakkari | self | - | caster pulse (circle) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 286 | Cauterize `drakkari_cauterize` | monster | drakkari | unit | - | unit ring (circle) + heal crosses | - | fire (heal) | none (Fab ground overlays only decorate circle zones) |
| 287 | Cinder Spit `drakkari_cinder_spit` | monster | drakkari | cone | r 260, 60 deg | cone + chevrons | - | fire | none (Fab ground overlays only decorate circle zones) |
| 288 | Dive Charge `drakkari_dive_charge` | monster | drakkari | line | 900 x 170 | rectangle lane + arrow | - | fire | none (Fab ground overlays only decorate circle zones) |
| 289 | Draconic Fury `drakkari_draconic_fury` | monster | drakkari | self | - | caster pulse (circle) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 290 | Dragon Roar `drakkari_dragon_roar` | monster | drakkari | circle | r 550 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 291 | Ember Breath `drakkari_ember_breath` | monster | drakkari | cone | r 320, 60 deg | cone + chevrons | - | fire | none (Fab ground overlays only decorate circle zones) |
| 292 | Ember Pool `drakkari_ember_pool` | monster | drakkari | circle | r 220 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 293 | Fire Rain `drakkari_fire_rain` | monster | drakkari | circle | r 210 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 294 | Flame Nova `drakkari_flame_nova` | monster | drakkari | circle | r 480 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 295 | Flame Pillar `drakkari_flame_pillar` | monster | drakkari | circle | r 210 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 296 | Hatch the Brood `drakkari_hatch_the_brood` | monster | drakkari | self | - | caster pulse (circle) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 297 | Incendiary Bolt `drakkari_incendiary_bolt` | monster | drakkari | circle | r 190 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 298 | Inferno Breath `drakkari_inferno_breath` | monster | drakkari | cone | r 560, 60 deg | cone + chevrons | - | fire | none (Fab ground overlays only decorate circle zones) |
| 299 | Magma Rain `drakkari_magma_rain` | monster | drakkari | circle | r 320 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 300 | Molten Slam `drakkari_molten_slam` | monster | drakkari | cone | r 360, 80 deg | cone + chevrons | - | fire | none (Fab ground overlays only decorate circle zones) |
| 301 | Pinning Bolt `drakkari_pinning_bolt` | monster | drakkari | circle | r 170 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 302 | Scale Ward `drakkari_scale_ward` | monster | drakkari | self | - | caster pulse (circle) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 303 | Searing Breath `drakkari_searing_breath` | monster | drakkari | cone | r 480, 55 deg | cone + chevrons | - | fire | none (Fab ground overlays only decorate circle zones) |
| 304 | Shield Charge `drakkari_shield_charge` | monster | drakkari | line | 650 x 170 | rectangle lane + arrow | - | fire | none (Fab ground overlays only decorate circle zones) |
| 305 | Spear Lunge `drakkari_spear_lunge` | monster | drakkari | line | 750 x 170 | rectangle lane + arrow | - | fire | none (Fab ground overlays only decorate circle zones) |
| 306 | Tail Sweep `drakkari_tail_sweep` | monster | drakkari | cone | r 260, 130 deg | cone + chevrons | - | fire | none (Fab ground overlays only decorate circle zones) |
| 307 | Whelp Dive `drakkari_whelp_dive` | monster | drakkari | line | 650 x 170 | rectangle lane + arrow | - | fire | none (Fab ground overlays only decorate circle zones) |
| 308 | Whelp Frenzy `drakkari_whelp_frenzy` | monster | drakkari | self | - | caster pulse (circle) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 309 | Wing Buffet `drakkari_wing_buffet` | monster | drakkari | circle | r 340 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 310 | Wing Gust `drakkari_wing_gust` | monster | drakkari | circle | r 420 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 311 | Wing Leap `drakkari_wing_leap` | monster | drakkari | self | - | caster pulse (circle) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 312 | Abyssal Roar `drowned_abyssal_roar` | monster | drowned_deep | circle | r 550 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 313 | Abyssal Ward `drowned_abyssal_ward` | monster | drowned_deep | unit | - | ground streak + unit ring (circle) | - | void | none (Fab ground overlays only decorate circle zones) |
| 314 | Barnacle Charge `drowned_barnacle_charge` | monster | drowned_deep | line | 900 x 170 | rectangle lane + arrow | - | tide | none (Fab ground overlays only decorate circle zones) |
| 315 | Brine Frenzy `drowned_brine_frenzy` | monster | drowned_deep | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 316 | Brine Mending `drowned_brine_mending` | monster | drowned_deep | unit | - | unit ring (circle) + heal crosses | - | tide (heal) | none (Fab ground overlays only decorate circle zones) |
| 317 | Call of the Deep `drowned_call_of_the_deep` | monster | drowned_deep | self | - | caster pulse (circle) | - | tide | none (Fab ground overlays only decorate circle zones) |
| 318 | Coral Bulwark `drowned_coral_bulwark` | monster | drowned_deep | self | - | caster pulse (circle) | - | tide | none (Fab ground overlays only decorate circle zones) |
| 319 | Crashing Wave `drowned_crashing_wave` | monster | drowned_deep | cone | r 520, 70 deg | cone + chevrons | - | tide | none (Fab ground overlays only decorate circle zones) |
| 320 | Devour `drowned_devour` | monster | drowned_deep | line | 1200 x 200 | rectangle lane + arrow | - | tide | none (Fab ground overlays only decorate circle zones) |
| 321 | Dread Whisper `drowned_dread_whisper` | monster | drowned_deep | circle | r 220 | circle (soft edge) | 1.001 | tide | NS_Water_Magic_Area1: round, fitted inside the rim (native r 436), dimmed |
| 322 | Drowning Grasp `drowned_drowning_grasp` | monster | drowned_deep | line | 1200 x 160 | rectangle lane + arrow | - | tide | none (Fab ground overlays only decorate circle zones) |
| 323 | Drowning Pool `drowned_drowning_pool` | monster | drowned_deep | circle | r 220 | circle (soft edge) | 1.001 | tide | NS_Water_Magic_Area1: round, fitted inside the rim (native r 436), dimmed |
| 324 | Harpoon Spine `drowned_harpoon_spine` | monster | drowned_deep | line | 950 x 140 | rectangle lane + arrow | - | tide | none (Fab ground overlays only decorate circle zones) |
| 325 | Ink Spit `drowned_ink_spit` | monster | drowned_deep | circle | r 200 | circle (soft edge) | 1.001 | tide | NS_Water_Magic_Area1: round, fitted inside the rim (native r 436), dimmed |
| 326 | Ink Tide `drowned_ink_tide` | monster | drowned_deep | circle | r 300 | circle (soft edge) | 1.001 | tide | NS_Water_Magic_Area1: round, fitted inside the rim (native r 436), dimmed |
| 327 | Ink Veil `drowned_ink_veil` | monster | drowned_deep | self | - | caster pulse (circle) | - | tide | none (Fab ground overlays only decorate circle zones) |
| 328 | Jet Retreat `drowned_jet_retreat` | monster | drowned_deep | self | - | caster pulse (circle) | - | tide | none (Fab ground overlays only decorate circle zones) |
| 329 | Leech Mending `drowned_leech_mending` | monster | drowned_deep | unit | - | unit ring (circle) + heal crosses | - | void (heal) | none (Fab ground overlays only decorate circle zones) |
| 330 | Leviathan Rage `drowned_leviathan_rage` | monster | drowned_deep | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 331 | Maddening Gaze `drowned_maddening_gaze` | monster | drowned_deep | circle | r 200 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 332 | Mind Scream `drowned_mind_scream` | monster | drowned_deep | circle | r 380 | circle (soft edge) | 1.001 | tide | NS_Water_Magic_Area1: round, fitted inside the rim (native r 436), dimmed |
| 333 | Mind Shatter `drowned_mind_shatter` | monster | drowned_deep | circle | r 520 | circle (soft edge) | 1.001 | tide | NS_Water_Magic_Area1: round, fitted inside the rim (native r 436), dimmed |
| 334 | Prophet's Madness `drowned_prophet_madness` | monster | drowned_deep | self | - | caster pulse (circle) | - | void | none (Fab ground overlays only decorate circle zones) |
| 335 | Riptide Rend `drowned_rend` | monster | drowned_deep | cone | r 320, 90 deg | cone + chevrons | - | tide | none (Fab ground overlays only decorate circle zones) |
| 336 | Riptide `drowned_riptide` | monster | drowned_deep | circle | r 260 | circle (soft edge) | 1.001 | tide | NS_Water_Magic_Area1: round, fitted inside the rim (native r 436), dimmed |
| 337 | Shell Crash `drowned_shell_bash` | monster | drowned_deep | cone | r 280, 70 deg | cone + chevrons | - | tide | none (Fab ground overlays only decorate circle zones) |
| 338 | Spine Volley `drowned_spine_volley` | monster | drowned_deep | circle | r 200 | circle (soft edge) | 1.001 | tide | NS_Water_Magic_Area1: round, fitted inside the rim (native r 436), dimmed |
| 339 | Riptide Lunge `drowned_stalker_lunge` | monster | drowned_deep | line | 750 x 170 | rectangle lane + arrow | - | tide | none (Fab ground overlays only decorate circle zones) |
| 340 | Tentacle Sweep `drowned_tentacle_sweep` | monster | drowned_deep | cone | r 440, 120 deg | cone + chevrons | - | tide | none (Fab ground overlays only decorate circle zones) |
| 341 | Tidal Prophecy `drowned_tidal_prophecy` | monster | drowned_deep | circle | r 320 | circle (soft edge) | 1.001 | tide | NS_Water_Magic_Area1: round, fitted inside the rim (native r 436), dimmed |
| 342 | Tidal Slam `drowned_tidal_slam` | monster | drowned_deep | cone | r 360, 85 deg | cone + chevrons | - | tide | none (Fab ground overlays only decorate circle zones) |
| 343 | Tidewall Oath `drowned_tidewall_oath` | monster | drowned_deep | unit | - | ground streak + unit ring (circle) | - | tide | none (Fab ground overlays only decorate circle zones) |
| 344 | Tsunami Slam `drowned_tsunami_slam` | monster | drowned_deep | circle | r 420 | circle (soft edge) | 1.001 | tide | NS_Water_Magic_Area1: round, fitted inside the rim (native r 436), dimmed |
| 345 | Undertow Grab `drowned_undertow` | monster | drowned_deep | line | 850 x 160 | rectangle lane + arrow | - | tide | none (Fab ground overlays only decorate circle zones) |
| 346 | Crushing Undertow `drowned_undertow_stomp` | monster | drowned_deep | circle | r 360 | circle (soft edge) | 1.001 | tide | NS_Water_Magic_Area1: round, fitted inside the rim (native r 436), dimmed |
| 347 | Whirlpool `drowned_whirlpool` | monster | drowned_deep | circle | r 240 | circle (soft edge) | 1.001 | tide | NS_Water_Magic_Area1: round, fitted inside the rim (native r 436), dimmed |
| 348 | Censer Smoke `fallen_censer_smoke` | monster | fallen_order | circle | r 230 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 349 | Chains of Penance `fallen_chain_grasp` | monster | fallen_order | line | 850 x 160 | rectangle lane + arrow | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 350 | Chains of Judgment `fallen_chains_of_judgment` | monster | fallen_order | line | 1200 x 160 | rectangle lane + arrow | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 351 | Condemn `fallen_condemn` | monster | fallen_order | circle | r 200 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 352 | Profane Consecration `fallen_consecrate` | monster | fallen_order | circle | r 340 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 353 | Profane Aegis `fallen_consecrated_wall` | monster | fallen_order | self | - | caster pulse (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 354 | Crusade `fallen_crusade` | monster | fallen_order | line | 1250 x 220 | rectangle lane + arrow | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 355 | Aegis of Ruin `fallen_crusader_aegis` | monster | fallen_order | self | - | caster pulse (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 356 | Sundering Verdict `fallen_crusader_cleave` | monster | fallen_order | cone | r 440, 115 deg | cone + chevrons | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 357 | Fallen Wrath `fallen_crusader_wrath` | monster | fallen_order | self | - | caster pulse (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 358 | Dark Absolution `fallen_dark_absolution` | monster | fallen_order | unit | - | unit ring (circle) + heal crosses | - | shadow (heal) | none (Fab ground overlays only decorate circle zones) |
| 359 | Dark Cleave `fallen_dark_cleave` | monster | fallen_order | cone | r 360, 85 deg | cone + chevrons | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 360 | Deathcharge `fallen_deathcharge` | monster | fallen_order | line | 900 x 170 | rectangle lane + arrow | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 361 | Hymn of Wrath `fallen_hymn_of_wrath` | monster | fallen_order | circle | r 1000 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 362 | Righteous Fury `fallen_inquisitor_zeal` | monster | fallen_order | self | - | caster pulse (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 363 | Judgment Slam `fallen_judgment_slam` | monster | fallen_order | circle | r 420 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 364 | Judgment `fallen_judgment_taunt` | monster | fallen_order | circle | r 550 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 365 | Last Stand `fallen_last_stand` | monster | fallen_order | self | - | caster pulse (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 366 | Martyr's Oath `fallen_martyrs_oath` | monster | fallen_order | unit | - | ground streak + unit ring (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 367 | Anathema `fallen_mass_silence` | monster | fallen_order | circle | r 520 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 368 | Broken Oath `fallen_oath_strike` | monster | fallen_order | cone | r 300, 90 deg | cone + chevrons | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 369 | Profane Light `fallen_profane_light` | monster | fallen_order | circle | r 210 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 370 | Purging Bolts `fallen_purging_bolts` | monster | fallen_order | circle | r 200 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 371 | Heretic's Pyre `fallen_pyre` | monster | fallen_order | circle | r 320 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 372 | Mortification `fallen_self_mortify` | monster | fallen_order | circle | r 260 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 373 | Shackle Bolt `fallen_shackle_bolt` | monster | fallen_order | circle | r 170 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 374 | Buckler Rush `fallen_shield_rush` | monster | fallen_order | line | 900 x 170 | rectangle lane + arrow | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 375 | Call the Penitent `fallen_summon_flagellants` | monster | fallen_order | self | - | caster pulse (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 376 | Tactical Retreat `fallen_tactical_retreat` | monster | fallen_order | self | - | caster pulse (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 377 | Unholy Frenzy `fallen_unholy_frenzy` | monster | fallen_order | self | - | caster pulse (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 378 | Zealous Frenzy `fallen_zealous_frenzy` | monster | fallen_order | self | - | caster pulse (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 379 | Zealous Leap `fallen_zealous_leap` | monster | fallen_order | line | 650 x 170 | rectangle lane + arrow | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 380 | Bear Charge `feral_bear_charge` | monster | feral_kin | line | 900 x 170 | rectangle lane + arrow | - | nature | none (Fab ground overlays only decorate circle zones) |
| 381 | Boar Frenzy `feral_boar_frenzy` | monster | feral_kin | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 382 | Gore Rush `feral_boar_gore` | monster | feral_kin | line | 650 x 170 | rectangle lane + arrow | - | nature | none (Fab ground overlays only decorate circle zones) |
| 383 | Bristle Burst `feral_bristle_burst` | monster | feral_kin | circle | r 260 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 384 | Call of the Wild `feral_call_of_the_wild` | monster | feral_kin | self | - | caster pulse (circle) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 385 | Crippling Arrow `feral_crippling_arrow` | monster | feral_kin | circle | r 200 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 386 | Earthquake `feral_earthquake` | monster | feral_kin | circle | r 560 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 387 | Earthshaker `feral_earthshaker` | monster | feral_kin | circle | r 360 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 388 | Elder Fury `feral_elder_fury` | monster | feral_kin | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 389 | Elder Roar `feral_elder_roar` | monster | feral_kin | circle | r 1200 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 390 | Gallop Away `feral_gallop_away` | monster | feral_kin | self | - | caster pulse (circle) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 391 | Hamstring `feral_hamstring` | monster | feral_kin | cone | r 260, 70 deg | cone + chevrons | - | nature | none (Fab ground overlays only decorate circle zones) |
| 392 | Herd Guard `feral_herd_guard` | monster | feral_kin | unit | - | ground streak + unit ring (circle) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 393 | Primal Rage `feral_mammoth_rage` | monster | feral_kin | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 394 | Titan Stomp `feral_mammoth_stomp` | monster | feral_kin | circle | r 440 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 395 | Stampede `feral_mammoth_trample` | monster | feral_kin | line | 1250 x 240 | rectangle lane + arrow | - | nature | none (Fab ground overlays only decorate circle zones) |
| 396 | Savage Maul `feral_maul` | monster | feral_kin | cone | r 360, 85 deg | cone + chevrons | - | nature | none (Fab ground overlays only decorate circle zones) |
| 397 | Pack Howl `feral_pack_howl` | monster | feral_kin | circle | r 1000 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 398 | Pounce `feral_pounce` | monster | feral_kin | line | 700 x 170 | rectangle lane + arrow | - | nature | none (Fab ground overlays only decorate circle zones) |
| 399 | Feral Rage `feral_rage` | monster | feral_kin | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 400 | Rending Maul `feral_rending_maul` | monster | feral_kin | cone | r 440, 120 deg | cone + chevrons | - | nature | none (Fab ground overlays only decorate circle zones) |
| 401 | Savage Chant `feral_savage_totem` | monster | feral_kin | circle | r 1000 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 402 | Spirit Mending `feral_spirit_mending` | monster | feral_kin | unit | - | unit ring (circle) + heal crosses | - | nature (heal) | none (Fab ground overlays only decorate circle zones) |
| 403 | Feral Spirits `feral_spirits` | monster | feral_kin | self | - | caster pulse (circle) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 404 | Thick Hide `feral_thick_hide` | monster | feral_kin | self | - | caster pulse (circle) | - | nature | none (Fab ground overlays only decorate circle zones) |
| 405 | Bramble Hex `feral_thorn_hex` | monster | feral_kin | circle | r 220 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 406 | Trample `feral_trample` | monster | feral_kin | line | 900 x 170 | rectangle lane + arrow | - | nature | none (Fab ground overlays only decorate circle zones) |
| 407 | Trampling Charge `feral_trample_charge` | monster | feral_kin | line | 900 x 170 | rectangle lane + arrow | - | nature | none (Fab ground overlays only decorate circle zones) |
| 408 | Trumpeting Challenge `feral_trumpet` | monster | feral_kin | circle | r 550 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 409 | Great Tusk Sweep `feral_tusk_sweep` | monster | feral_kin | cone | r 440, 115 deg | cone + chevrons | - | nature | none (Fab ground overlays only decorate circle zones) |
| 410 | Crushing Charge `feral_ursoth_charge` | monster | feral_kin | line | 1250 x 200 | rectangle lane + arrow | - | nature | none (Fab ground overlays only decorate circle zones) |
| 411 | Volley `feral_volley` | monster | feral_kin | circle | r 200 | circle (soft edge) | 1.001 | nature | none (NS_AreaBuff: tendril ribbons ignore the component scale: 3-4x past the rim even when fitted) |
| 412 | Frozen Hamstring `frostfang_hamstring` | monster | frostfang_alpha | cone | r 260, 70 deg | cone + chevrons | - | steel | none (Fab ground overlays only decorate circle zones) |
| 413 | Winter Howl `frostfang_howl` | monster | frostfang_alpha | circle | r 1000 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 414 | Pounce `frostfang_pounce` | monster | frostfang_alpha | line | 700 x 170 | rectangle lane + arrow | - | steel | none (Fab ground overlays only decorate circle zones) |
| 415 | Diving Pounce `griffon_dive` | monster | storm_griffon | line | 900 x 200 | rectangle lane + arrow | - | steel | none (Fab ground overlays only decorate circle zones) |
| 416 | Talon Frenzy `griffon_frenzy` | monster | storm_griffon | cone | r 340, 90 deg | cone + chevrons | - | steel | none (Fab ground overlays only decorate circle zones) |
| 417 | Bone Hook `hollow_bone_hook` | monster | hollow | line | 1200 x 160 | rectangle lane + arrow | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 418 | Deathless Resolve `hollow_deathless` | monster | hollow | self | - | caster pulse (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 419 | Grave Silence `hollow_grave_silence` | monster | hollow | circle | r 220 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 420 | Hook Chain `hollow_hook_chain` | monster | hollow | line | 850 x 160 | rectangle lane + arrow | - | steel | none (Fab ground overlays only decorate circle zones) |
| 421 | Iron Stomp `hollow_iron_stomp` | monster | hollow | circle | r 340 | circle (soft edge) | 1.001 | steel | none (pack not installed) |
| 422 | Pack Howl `hollow_pack_howl` | monster | hollow | circle | r 1000 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 423 | Pinning Net `hollow_pinning_net` | monster | hollow | circle | r 180 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 424 | Pounce `hollow_pounce` | monster | hollow | line | 650 x 170 | rectangle lane + arrow | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 425 | Rending Bite `hollow_rending_bite` | monster | hollow | cone | r 260, 70 deg | cone + chevrons | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 426 | Rubble Toss `hollow_rubble_toss` | monster | hollow | circle | r 260 | circle (soft edge) | 1.001 | steel | none (pack not installed) |
| 427 | Rusted Cleave `hollow_rusted_cleave` | monster | hollow | cone | r 300, 90 deg | cone + chevrons | - | steel | none (Fab ground overlays only decorate circle zones) |
| 428 | Shield Rush `hollow_shield_rush` | monster | hollow | line | 650 x 170 | rectangle lane + arrow | - | steel | none (Fab ground overlays only decorate circle zones) |
| 429 | Withering Hex `hollow_withering_hex` | monster | hollow | circle | r 240 | circle (soft edge) | 1.001 | poison | NS_Posion_Magic_Area1: round, fitted inside the rim (native r 593), dimmed |
| 430 | Axe Barrage `ironhide_axe_barrage` | monster | ironhide | circle | r 200 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 431 | Battle Hymn `ironhide_battle_hymn` | monster | ironhide | unit | - | unit ring (circle) + heal crosses | - | blood (heal) | none (Fab ground overlays only decorate circle zones) |
| 432 | Berserk `ironhide_berserk` | monster | ironhide | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 433 | Blood Fury `ironhide_blood_fury` | monster | ironhide | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 434 | Blood Hex `ironhide_blood_hex` | monster | ironhide | circle | r 220 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 435 | Bloodlust `ironhide_bloodlust` | monster | ironhide | circle | r 1000 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 436 | Bodyguard `ironhide_bodyguard` | monster | ironhide | unit | - | ground streak + unit ring (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 437 | Bog Curse `ironhide_bog_curse` | monster | ironhide | circle | r 220 | circle (soft edge) | 1.001 | poison | NS_Posion_Magic_Area1: round, fitted inside the rim (native r 593), dimmed |
| 438 | Boulder Hurl `ironhide_boulder_hurl` | monster | ironhide | circle | r 260 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 439 | Bounding Retreat `ironhide_bounding_retreat` | monster | ironhide | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 440 | Call the Clans `ironhide_call_the_clans` | monster | ironhide | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 441 | Meat Hook `ironhide_chain_hook` | monster | ironhide | line | 850 x 160 | rectangle lane + arrow | - | blood | none (Fab ground overlays only decorate circle zones) |
| 442 | Cleave `ironhide_cleave` | monster | ironhide | cone | r 320, 100 deg | cone + chevrons | - | blood | none (Fab ground overlays only decorate circle zones) |
| 443 | Deafening Boom `ironhide_deafening_boom` | monster | ironhide | circle | r 360 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 444 | Earthsplitter `ironhide_earthsplitter` | monster | ironhide | circle | r 420 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 445 | Frenzy Ritual `ironhide_frenzy_ritual` | monster | ironhide | circle | r 1000 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 446 | Gut Punch `ironhide_gut_punch` | monster | ironhide | cone | r 240, 70 deg | cone + chevrons | - | blood | none (Fab ground overlays only decorate circle zones) |
| 447 | Hamstring Axe `ironhide_hamstring_axe` | monster | ironhide | circle | r 170 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 448 | Juggernaut Rage `ironhide_juggernaut_rage` | monster | ironhide | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 449 | Leaping Axes `ironhide_leaping_axe` | monster | ironhide | line | 900 x 170 | rectangle lane + arrow | - | blood | none (Fab ground overlays only decorate circle zones) |
| 450 | Rend `ironhide_rend` | monster | ironhide | cone | r 300, 80 deg | cone + chevrons | - | blood | none (Fab ground overlays only decorate circle zones) |
| 451 | Iron Door `ironhide_shield_wall` | monster | ironhide | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 452 | Skull Cleave `ironhide_skull_cleave` | monster | ironhide | cone | r 440, 120 deg | cone + chevrons | - | blood | none (Fab ground overlays only decorate circle zones) |
| 453 | Spinning Axe `ironhide_spinning_axe` | monster | ironhide | cone | r 600, 25 deg | cone + chevrons | - | blood | none (Fab ground overlays only decorate circle zones) |
| 454 | Spirit Mend `ironhide_spirit_mend` | monster | ironhide | unit | - | unit ring (circle) + heal crosses | - | blood (heal) | none (Fab ground overlays only decorate circle zones) |
| 455 | Troll Hide `ironhide_thick_hide` | monster | ironhide | self | - | caster pulse (circle) | - | blood | none (Fab ground overlays only decorate circle zones) |
| 456 | Trunk Sweep `ironhide_trunk_sweep` | monster | ironhide | cone | r 440, 115 deg | cone + chevrons | - | blood | none (Fab ground overlays only decorate circle zones) |
| 457 | War Charge `ironhide_war_charge` | monster | ironhide | line | 900 x 170 | rectangle lane + arrow | - | blood | none (Fab ground overlays only decorate circle zones) |
| 458 | Challenging Shout `ironhide_war_cry` | monster | ironhide | circle | r 550 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 459 | War Drums `ironhide_war_drums` | monster | ironhide | circle | r 1000 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 460 | Warchief's Roar `ironhide_warchief_roar` | monster | ironhide | circle | r 1200 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 461 | Warpath `ironhide_warpath` | monster | ironhide | line | 1250 x 200 | rectangle lane + arrow | - | blood | none (Fab ground overlays only decorate circle zones) |
| 462 | Whirlwind `ironhide_whirlwind` | monster | ironhide | circle | r 320 | circle (soft edge) | 1.001 | blood | NS_Blood_Magic_Area1: round, fitted inside the rim (native r 649), dimmed |
| 463 | Grave Nova `lich_frost_nova` | monster | fallen_order | circle | r 320 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 464 | Unholy Mending `lich_mend` | monster | fallen_order | unit | - | unit ring (circle) + heal crosses | - | shadow (heal) | none (Fab ground overlays only decorate circle zones) |
| 465 | Soul Rend `lich_soul_rend` | monster | fallen_order | circle | r 210 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 466 | Barbed Shot `npc_barbed_shot` | monster | hollow | line | 1200 x 40 | rectangle lane + arrow (projectile corridor) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 467 | Blight Pool `npc_blight_pool` | monster | hollow | circle | r 220 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 468 | Photon Bolt `npc_bolt` | monster | aetheri | line | 1200 x 64 | rectangle lane + arrow (projectile corridor) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 469 | Brutal Charge `npc_bruiser_charge` | monster | hollow | line | 950 x 170 | rectangle lane + arrow | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 470 | Crushing Slam `npc_bruiser_slam` | monster | hollow | cone | r 360, 80 deg | cone + chevrons | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 471 | Dark Mending `npc_caster_mend` | monster | hollow | unit | - | unit ring (circle) + heal crosses | - | shadow (heal) | none (Fab ground overlays only decorate circle zones) |
| 472 | Disengage `npc_hunter_disengage` | monster | hollow | self | - | caster pulse (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 473 | Rain of Barbs `npc_hunter_volley` | monster | hollow | circle | r 200 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 474 | Lunge `npc_infantry_lunge` | monster | hollow | line | 700 x 140 | rectangle lane + arrow | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 475 | Hooked Claws `npc_melee` | monster | drowned_deep | unit | - | ground streak + unit ring (circle) | - | tide | none (Fab ground overlays only decorate circle zones) |
| 476 | Shadow Bolt `npc_shadow_bolt` | monster | hollow | line | 1200 x 64 | rectangle lane + arrow (projectile corridor) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 477 | Photon Round `npc_shot` | monster | aetheri | line | 1200 x 40 | rectangle lane + arrow (projectile corridor) | - | steel | none (Fab ground overlays only decorate circle zones) |
| 478 | Guardian's Oath `npc_tank_guard` | monster | hollow | unit | - | ground streak + unit ring (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 479 | Challenging Roar `npc_tank_provoke` | monster | hollow | circle | r 550 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 480 | Shield Wall `npc_tank_wall` | monster | hollow | self | - | caster pulse (circle) | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 481 | Grave Grasp `shambler_grasp` | monster | hollow | cone | r 260, 70 deg | cone + chevrons | - | shadow | none (Fab ground overlays only decorate circle zones) |
| 482 | Festering Rot `shambler_rot` | monster | hollow | circle | r 200 | circle (soft edge) | 1.001 | shadow | NS_Shadow_Magic_Area1: round, fitted inside the rim (native r 1350), dimmed |
| 483 | Anvil Cleave `stoneborn_anvil_cleave` | monster | stoneborn | cone | r 440, 110 deg | cone + chevrons | - | earth | none (Fab ground overlays only decorate circle zones) |
| 484 | Arc Lattice `stoneborn_arc_lattice` | monster | stoneborn | circle | r 230 | circle (soft edge) | 1.001 | earth | NS_Earth_Spells_Circle: round, fitted inside the rim (native r 622), dimmed |
| 485 | Boulder Charge `stoneborn_boulder_charge` | monster | stoneborn | line | 900 x 170 | rectangle lane + arrow | - | earth | none (Fab ground overlays only decorate circle zones) |
| 486 | Boulder Toss `stoneborn_boulder_toss` | monster | stoneborn | circle | r 260 | circle (soft edge) | 1.001 | earth | NS_Earth_Spells_Circle: round, fitted inside the rim (native r 622), dimmed |
| 487 | Awakened Wrath `stoneborn_colossus_rage` | monster | stoneborn | self | - | caster pulse (circle) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 488 | Colossal Crush `stoneborn_crush` | monster | stoneborn | cone | r 440, 110 deg | cone + chevrons | - | earth | none (Fab ground overlays only decorate circle zones) |
| 489 | Ether Burst `stoneborn_ether_burst` | monster | stoneborn | circle | r 320 | circle (soft edge) | 1.001 | earth | NS_Earth_Spells_Circle: round, fitted inside the rim (native r 622), dimmed |
| 490 | Ether Repair `stoneborn_ether_repair` | monster | stoneborn | unit | - | unit ring (circle) + heal crosses | - | arcane (heal) | none (Fab ground overlays only decorate circle zones) |
| 491 | Forge Fury `stoneborn_forge_fury` | monster | stoneborn | self | - | caster pulse (circle) | - | fire | none (Fab ground overlays only decorate circle zones) |
| 492 | Forge Mending `stoneborn_forge_mending` | monster | stoneborn | unit | - | unit ring (circle) + heal crosses | - | arcane (heal) | none (Fab ground overlays only decorate circle zones) |
| 493 | Forge Sentinels `stoneborn_forge_sentinels` | monster | stoneborn | self | - | caster pulse (circle) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 494 | Granite Smash `stoneborn_granite_smash` | monster | stoneborn | cone | r 360, 80 deg | cone + chevrons | - | earth | none (Fab ground overlays only decorate circle zones) |
| 495 | Ground Pound `stoneborn_ground_pound` | monster | stoneborn | circle | r 360 | circle (soft edge) | 1.001 | earth | NS_Earth_Spells_Circle: round, fitted inside the rim (native r 622), dimmed |
| 496 | Molten Slag `stoneborn_molten_slag` | monster | stoneborn | circle | r 220 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 497 | Overcharge `stoneborn_overcharge` | monster | stoneborn | circle | r 1000 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 498 | Ether Overload `stoneborn_overload` | monster | stoneborn | self | - | caster pulse (circle) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 499 | Petrifying Gaze `stoneborn_petrify` | monster | stoneborn | circle | r 280 | circle (soft edge) | 1.001 | earth | NS_Earth_Spells_Circle: round, fitted inside the rim (native r 622), dimmed |
| 500 | Piercing Beam `stoneborn_piercing_beam` | monster | stoneborn | cone | r 750, 12 deg | cone + chevrons | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 501 | Pinning Shard `stoneborn_pinning_shard` | monster | stoneborn | circle | r 170 | circle (soft edge) | 1.001 | earth | NS_Earth_Spells_Circle: round, fitted inside the rim (native r 622), dimmed |
| 502 | Quake `stoneborn_quake` | monster | stoneborn | circle | r 440 | circle (soft edge) | 1.001 | earth | NS_Earth_Spells_Circle: round, fitted inside the rim (native r 622), dimmed |
| 503 | Recoil Jump `stoneborn_recoil_jump` | monster | stoneborn | self | - | caster pulse (circle) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 504 | Rune Mine `stoneborn_rune_mine` | monster | stoneborn | circle | r 220 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 505 | Rune Rush `stoneborn_rune_rush` | monster | stoneborn | line | 900 x 170 | rectangle lane + arrow | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 506 | Rune Strike `stoneborn_rune_strike` | monster | stoneborn | cone | r 320, 90 deg | cone + chevrons | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 507 | Runic Shockwave `stoneborn_runic_shockwave` | monster | stoneborn | circle | r 440 | circle (soft edge) | 1.001 | earth | NS_Earth_Spells_Circle: round, fitted inside the rim (native r 622), dimmed |
| 508 | Runic Challenge `stoneborn_runic_taunt` | monster | stoneborn | circle | r 550 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 509 | Shard Volley `stoneborn_shard_volley` | monster | stoneborn | circle | r 200 | circle (soft edge) | 1.001 | arcane | NS_Air_Magic_AOE: round, fitted inside the rim (native r 1235), dimmed |
| 510 | Shield Matrix `stoneborn_shield_matrix` | monster | stoneborn | unit | - | ground streak + unit ring (circle) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 511 | Slag Eruption `stoneborn_slag_eruption` | monster | stoneborn | circle | r 320 | circle (soft edge) | 1.001 | fire | NS_Fire_Magic_AOE: round, fitted inside the rim (native r 668), dimmed |
| 512 | Static Pulse `stoneborn_static_pulse` | monster | stoneborn | circle | r 320 | circle (soft edge) | 1.001 | earth | NS_Earth_Spells_Circle: round, fitted inside the rim (native r 622), dimmed |
| 513 | Stoneskin `stoneborn_stoneskin` | monster | stoneborn | self | - | caster pulse (circle) | - | earth | none (Fab ground overlays only decorate circle zones) |
| 514 | Tremor `stoneborn_tremor` | monster | stoneborn | circle | r 360 | circle (soft edge) | 1.001 | earth | NS_Earth_Spells_Circle: round, fitted inside the rim (native r 622), dimmed |
| 515 | Warding Link `stoneborn_warding_link` | monster | stoneborn | unit | - | ground streak + unit ring (circle) | - | arcane | none (Fab ground overlays only decorate circle zones) |
| 516 | Blink `void_blink` | monster | voidborn | self | - | caster pulse (circle) + void rings (circles r 240 / 100) | - | void | none (Fab ground overlays only decorate circle zones) |
| 517 | Void Breath `void_breath` | monster | voidborn | cone | r 560, 60 deg | cone + chevrons | - | void | none (Fab ground overlays only decorate circle zones) |
| 518 | Collapsing Star `void_collapsing_star` | monster | voidborn | circle | r 320 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 519 | Devour `void_devour` | monster | voidborn | line | 1200 x 220 | rectangle lane + arrow | - | void | none (Fab ground overlays only decorate circle zones) |
| 520 | Endless Hunger `void_devourer_hunger` | monster | voidborn | self | - | caster pulse (circle) | - | void | none (Fab ground overlays only decorate circle zones) |
| 521 | Disintegrate `void_disintegrate` | monster | voidborn | cone | r 750, 12 deg | cone + chevrons | - | void | none (Fab ground overlays only decorate circle zones) |
| 522 | Event Horizon `void_event_horizon` | monster | voidborn | self | - | caster pulse (circle) | - | void | none (Fab ground overlays only decorate circle zones) |
| 523 | Void Gaze `void_gaze` | monster | voidborn | circle | r 550 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 524 | Gravity Slam `void_gravity_slam` | monster | voidborn | circle | r 360 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 525 | Gravity Well `void_gravity_well` | monster | voidborn | circle | r 360 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 526 | Ascension `void_herald_ascension` | monster | voidborn | self | - | caster pulse (circle) | - | void | none (Fab ground overlays only decorate circle zones) |
| 527 | Grasp of the Void `void_herald_grasp` | monster | voidborn | line | 1200 x 160 | rectangle lane + arrow | - | void | none (Fab ground overlays only decorate circle zones) |
| 528 | Latch `void_latch` | monster | voidborn | line | 750 x 160 | rectangle lane + arrow | - | void | none (Fab ground overlays only decorate circle zones) |
| 529 | Mind Spike `void_mind_spike` | monster | voidborn | circle | r 220 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 530 | Null Cleave `void_null_cleave` | monster | voidborn | cone | r 340, 80 deg | cone + chevrons | - | void | none (Fab ground overlays only decorate circle zones) |
| 531 | Open the Rift `void_open_the_rift` | monster | voidborn | self | - | caster pulse (circle) | - | void | none (Fab ground overlays only decorate circle zones) |
| 532 | Void Orb `void_orb` | monster | voidborn | circle | r 210 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 533 | Paralyzing Gaze `void_paralyze_gaze` | monster | voidborn | circle | r 200 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 534 | Phase Strike `void_phase_strike` | monster | voidborn | line | 750 x 170 | rectangle lane + arrow | - | void | none (Fab ground overlays only decorate circle zones) |
| 535 | Void Rend `void_rend` | monster | voidborn | cone | r 300, 85 deg | cone + chevrons | - | void | none (Fab ground overlays only decorate circle zones) |
| 536 | Void Rift `void_rift` | monster | voidborn | circle | r 220 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 537 | Rift Charge `void_rift_charge` | monster | voidborn | line | 900 x 170 | rectangle lane + arrow | - | void | none (Fab ground overlays only decorate circle zones) |
| 538 | Rift Mending `void_rift_mending` | monster | voidborn | unit | - | unit ring (circle) + heal crosses | - | void (heal) | none (Fab ground overlays only decorate circle zones) |
| 539 | Silence of the Stars `void_silence_of_stars` | monster | voidborn | circle | r 520 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 540 | Singularity `void_singularity` | monster | voidborn | circle | r 480 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 541 | Void Tether `void_tether` | monster | voidborn | unit | - | ground streak + unit ring (circle) | - | void | none (Fab ground overlays only decorate circle zones) |
| 542 | Titan Slam `void_titan_slam` | monster | voidborn | circle | r 420 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 543 | Unmaking `void_unmaking` | monster | voidborn | cone | r 480, 55 deg | cone + chevrons | - | void | none (Fab ground overlays only decorate circle zones) |
| 544 | Unravel `void_unravel` | monster | voidborn | self | - | caster pulse (circle) | - | void | none (Fab ground overlays only decorate circle zones) |
| 545 | Void Burst `void_voidling_burst` | monster | voidborn | circle | r 260 | circle (soft edge) | 1.001 | void | NS_Dark_Magic_AOE: round, fitted inside the rim (native r 700), dimmed |
| 546 | Hunger `void_voidling_frenzy` | monster | voidborn | self | - | caster pulse (circle) | - | void | none (Fab ground overlays only decorate circle zones) |
| 547 | Warp `void_warp` | monster | voidborn | self | - | caster pulse (circle) + void rings (circles r 240 / 100) | - | void | none (Fab ground overlays only decorate circle zones) |

<!-- shape-audit:end -->
