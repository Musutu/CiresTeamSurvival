# RESUME: vfx-loop-fix (branch feat/vfx-loop-fix, ports 18100-18149)

Eric (playtest 2026-09-28): "some Aura or Flame ability is constantly going for certain players ... a constant loop of
fire eruption around the mid section of the characters. This should appear from the ground."

## Findings
- Playtest log: F:\CiresTeamSurvival\Saved\Logs\CiresTeamSurvival-backup-2026.09.28-12.22.35.log (read only).
  The fire systems loaded during the match were NS_Fire_Magic_Flame1 (loaded 6 times) and NS_Fire_Magic_Buff (first loaded
  at the start of wave 1).
- The bots Dusk 2, Dusk 5, Ember 2 and Ember 5 bought Cinder Cone and Ashen Ward. These are the "certain players".
- **Cinder Cone** (`cinder_cone`): pack-usage-3 changed its FabVFX cast from Flamethrower to **NS_Fire_Magic_Flame1**
  ("orange arc rings rising to flame column"). That system is a ground eruption. Cast cues are anchored at
  `Hero->GetActorLocation()`, which is the capsule centre, so every cast erupted from the midsection. With 4 bots casting on
  cooldown, it read as a constant loop.
- Actual endless loops (measured by the new `-CireFabLoopAudit`, 415 systems): 16 vendor systems **ignore Deactivate**. These
  are the Big Pack orbs and projectiles, plus NS_HealBeam. When one of them is used as a cast, it stays behind for ever after
  the cast visual releases it. This is the case for miner_lantern, powder_flask and drakkari_pinning_bolt (NS_Fire_Magic_Orb,
  a fire flame at the caster's midsection), and also for whisp_guiding_mote, aether_stasis_mine, disruption_pylon,
  aether_warp_in, photon_turret (area), troll_returning_axes, and the whisp_spirit_tether / blight_sap_mending /
  feral_spirit_mending beams. 110 systems loop on their own (for example Fire_Magic_Aura, State_VFX_Burn1). They were only safe
  because something released them.
- The Fab buff auras were attached at a fixed `(0,0,-88)` below the capsule centre, not at the unit's real capsule bottom.

## Done
- `CireFabVFX` (shared file, additive):
  - Every one-shot spawn is **bounded**. `SpawnAttached(auto-destroy)` and `SpawnAt` release the effect after the role
    lifetime (FabVFX.json `lifetime.cast` 1.6 s / `impact` 2 s / `levelUp` 3 s, or the entry's own `lifetime`).
  - `Release()` now **hard-stops** the effect after `lifetime.releaseFade` (2.5 s) when the vendor system ignores Deactivate.
    This covers buff auras when the buff ends, cast / projectile / zone visuals on EndPlay, and Paragon FX.
  - The tracker is a core ticker on world time; pooled components are dropped from it once they complete or move.
  - Ground anchoring: FabVFX.json has a new `groundAnchored` list (path -> look; 130 curated eruptions, pillars, rune rings,
    spikes and geysers) and an entry override `"anchor": "ground"|"body"`. New helpers: `GroundUnder` (the capsule bottom,
    else a WorldStatic trace) and `FeetOffset`.
- `ACireSpellVisual::UpdateFabVFX`: cast and impact Fab systems that are ground-anchored spawn at the floor under the unit.
  Casts are bounded to max(role lifetime, the visual's remaining duration, which covers channels). Area overlays are never
  time-bounded; they end with their zone.
- The aura Fab overlays (`CireAuraVisuals`) and the level-up flourish (`CireHUDWow`) now attach at the real feet
  (`FeetOffset`). The level-up is also bounded.
- Kit-editor placed casts (`SpawnPlacedCast`) are bounded. Paragon FX end through `CireFabVFX::Release`.
- Tools/MapFabVFX.py keeps the new hand data (`anchorNotes`, `groundAnchored`, `lifetime`).
- Loop audit probe: `-CireFabLoopAudit[=out.json]` (CireFabVFXCatalog.cpp). It needs rendering (Niagara never spawns under
  -nullrhi). The result is in Saved/FabLoopAudit/audit.json (local).
- Native tests: `Source/CiresTeamSurvival/CireVFXLoopTests.cpp` (`CireFabVFX::RunLoopTests`, called from
  `CireFabVFX::RunTests`) cover:
  - the data (lifetimes; Flame1 and Buff are ground-anchored; entry overrides);
  - GroundUnder / FeetOffset;
  - a looping one-shot ends after its lifetime + fade;
  - a persistent overlay is never cut by time and is hard-stopped after Release;
  - a fire stance aura (artillery = NS_Fire_Magic_Buff) sits at the feet and stops after the buff ends;
  - the Cinder Cone cast eruption spawns at the feet (z 25910 vs feet 25908, capsule centre 26000) and stops.
  Headless runs use a forced-active Niagara stand-in (Niagara creates no components without a renderer). The same tests also
  passed live (windowed probe, real components): CIRE_VFX_LOOP_TESTS_PASS checks=24.

## Spells / auras changed
- Ground placement: every cast / impact whose Fab system is in `groundAnchored`. That is 178 ability casts (including
  cinder_cone, ashen_square, drakish_dragon_oath, drakkari_tail_sweep / cinder_spit / ashwing_tail (Flame1),
  golem_ether_furnace, drakkari_brood_oath (Flame2), drakkari_ashwing_fury (Flame3), artillery, drakkari_dragon_roar,
  drakkari_cauterize (Buff), war_cry, cataclysm, ...) and 73 impacts, plus the school sets fire.impact, holy.cast, life.cast and
  poison.cast. All Fab buff auras sit on the real feet.
- Endless loop fixed (hard stop): miner_lantern, powder_flask, drakkari_pinning_bolt, whisp_guiding_mote, aether_stasis_mine,
  disruption_pylon, aether_warp_in, photon_turret, troll_returning_axes, whisp_spirit_tether, blight_sap_mending,
  feral_spirit_mending. Every projectile trail using the 16 deactivate-ignoring systems is also covered.
- Bounded by lifetime: all one-shots (151 cast / impact slots use a system that loops on its own).

## Assumptions / questions for Eric
- Ashen Ward's cast has no Fab cast role: it is a ground-aimed circle, so the procedural telegraph draws it. Its
  NS_Fire_Magic_Buff entry is never played as a cast. The Buff system still shows as the fire stance / buff aura (artillery,
  worldstone_bruiser, buff.fire), now at the feet.
- The ground-anchored list was curated from the inventory "look" texts, because the vendor bounds are fixed and symmetric and
  cannot be measured. Body-centred spheres, slashes and hits are excluded. Eric can move a system either way with
  `"anchor"` on an entry, or by editing `groundAnchored`.
- Pre-existing, not changed: a cast's Fab overlay spawns on Configure, about 0.12 s before the procedural release frame.

## Shared-file edits
- CireFabVFX.h/.cpp (tracker, anchoring, lifetimes, RunTests hook), CireSpellPresentation.cpp/.h (UpdateFabVFX spawn point +
  bound, test accessors), CireAuraVisuals.cpp (1 line: FeetOffset), CireHUDWow.cpp (level-up: FeetOffset + bound),
  CireKitEditor.cpp (SpawnPlacedCast bound), CireParagonChampions.cpp (FX end via Release), CireFabVFXCatalog.cpp (audit),
  Content/Data/FabVFX.json (additive: anchorNotes, groundAnchored, lifetime), Tools/MapFabVFX.py (keep hand keys).

## Gate logs
- native: Saved/ExpansionChecks/20260928T134556838542Z (CIRE_COMBAT_EXPANSION_PASS, CIRE_VFX_LOOP_TESTS_PASS checks=23)
- live native (windowed): Saved/FabLoopAudit/live_native.log (PASS checks=24)
- network: Saved/NetworkSmoke/20260928T140036827536Z (CIRE_NETWORK_SMOKE_PASS)
- interface: Saved/InterfaceSmoke/20260928T135828156625Z (CIRE_INTERFACE_SMOKE_PASS)
- all on main 489f4a22 merged (already up to date)
