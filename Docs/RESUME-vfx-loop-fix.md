# RESUME: vfx-loop-fix (branch feat/vfx-loop-fix, ports 18100-18149)

Eric (playtest 2026-09-28): "some Aura or Flame ability is constantly going for certain players ... a constant loop of
fire eruption around the mid section of the characters. This should appear from the ground."

## Investigation
- Playtest log: F:\CiresTeamSurvival\Saved\Logs\CiresTeamSurvival-backup-2026.09.28-12.22.35.log (read only).
  Fire systems loaded in the match: NS_Fire_Magic_Flame1 (x6 loads) and NS_Fire_Magic_Buff (first load at wave 1 start).
- Bots Dusk 2 / Dusk 5 / Ember 2 / Ember 5 bought Cinder Cone + Ashen Ward ("certain players").
  FabVFX.json: cinder_cone.cast = NS_Fire_Magic_Flame1 ("arc rings rising to flame column", switched from Flamethrower by
  pack-usage-3), ashen_square.cast = NS_Fire_Magic_Buff ("flame pillar with ring").
- Cast cues spawn at Hero->GetActorLocation() (capsule centre = midsection); the Fab cast system is attached there.

## Done
- (in progress) loop audit probe: -CireFabLoopAudit (CireFabVFXCatalog.cpp).

## Not done / Next

## Assumptions / questions for Eric

## Shared-file edits

## Gate logs
