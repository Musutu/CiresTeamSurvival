# RESUME: pack-usage pass #2 (feat/pack-usage-2)

Worktree `F:\CiresTeamSurvival-agents\cts-pack-usage-2`, branch `feat/pack-usage-2` from main 702f582. Test ports 17460-17469.
Brief: full integration of every purchased Niagara / VFX pack (Big_Pack_Magic_VFX, Shadow_Magic, Earth_Spells, Forest_VFX,
FXVarietyPack, State_VFX, RealisticBlood) + Fantasy_UI_SFX wiring; quality first, reuse with recolour; report usage % per
pack before -> after in `Docs/PackUsage.md`. Weapons / grips / WeaponLoadouts are OFF LIMITS (feat/blender-rig).

## Done

- Tooling: `Tools/InventoryFabVFX.py` (inventory + ratings file `Art/Fab/FabVFXInventory.json`), `Tools/RunFabVFXCatalog.py`
  + `CireFabVFXCatalog.cpp` (`-CireFabVFXCatalog=<list.json>`: every system alone, early/late frames, contact sheets),
  `Tools/AuditPackUsage.py` (before -> after table, `--before main`), `Tools/FabMonsterVFXTable.py` (race kits, CC tints,
  hit / kill / event signatures), `Tools/MapFabVFX.py` extended (dict values with tint / strength; monster + hit + kill +
  event tables merged into `FabVFX.json` `abilities`).
- Code: `CireFabVFX::Recolor` / `ApplyEntryTint` / `FindKey` (+ `tintStrength` parse), monster Gather wind-up gets a cast
  overlay, hit / crit signature lookup by target body layer x attacker weapon (`CireSpellPresentation.cpp UpdateFabVFX`),
  kill burst on `UCireMonsterArt::MulticastDeath`, level-up flourish in `CireHUDWow.cpp`, shop close + toast cues
  (`CireShopUI.cpp`), tests in `CireFabVFX::RunTests` (tints valid, hit/kill/level_up keys present, every monster ability
  has a signature, no two abilities of one unit share a cast look).
- Data: `Content/Data/FabVFX.json` regenerated (533 abilities / 961 slots incl. 334 monster abilities, tints); champion
  per-champion duplicate looks resolved by recolour in `Tools/FabAbilityVFXTable.py`; audio: `Tools/MapFabAudio.py` +
  `Tools/BuildAudioEvents.py` (`ui_toast`, `ui_shop_close`, banners, more takes) -> `AudioCues.json`, `FabAudioMap.json`.
- Before galleries captured: `Saved/AbilityVFX/before-packusage2-champion-*`, `-voidborn-*`, `-stoneborn-*` (all PASS).

## In progress

- Catalogue render (`Tools/RunFabVFXCatalog.py`, log `Saved/AgentLogs/fabvfx-catalog.log`) -> then rate systems
  (quality A/B/C + look) in `Art/Fab/FabVFXInventory.json` from the sheets `Saved/FabVFXCatalog/<stamp>/sheets/`.
- Native gate re-run after the race-level de-dup fix (`Saved/AgentLogs/native-packusage2-3.log`).

## Next queue

1. Native gate green -> commit checkpoint 1 (tools + data + code).
2. Rate the inventory from the catalogue sheets; revisit picks rated C in `FabMonsterVFXTable.py` / `FabAbilityVFXTable.py`.
3. After galleries: `RunAbilityVFXGallery.py --set champion --tag after-packusage2`, `--set voidborn`, `--set stoneborn`,
   then `--compare BEFORE AFTER` -> note paths in `Docs/PackUsage.md` verification block.
4. Full gates (native + RunNetworkSmoke --port 17463 + RunInterfaceSmoke --port 17464, both `--startup-timeout 120
   --probe-timeout 120`), `Tools/AuditPackUsage.py --before main` (rewrites the doc table), final commit.

## Commands

```
Build:   cmd //c "F:\UE_5.8\Engine\Build\BatchFiles\Build.bat CiresTeamSurvivalEditor Win64 Development -Project=F:\CiresTeamSurvival-agents\cts-pack-usage-2\CiresTeamSurvival.uproject -WaitMutex -NoHotReloadFromIDE"
Data:    python Tools/MapFabVFX.py ; python Tools/MapFabAudio.py --installed-only ; python Tools/BuildAudioEvents.py
Audit:   python Tools/AuditPackUsage.py --before main
Gates:   python Tools/RunExpansionChecks.py --only native --timeout 240 --port 1746x
         python Tools/RunNetworkSmoke.py --port 1746x --startup-timeout 120 --probe-timeout 120
         python Tools/RunInterfaceSmoke.py --port 1746x --startup-timeout 120 --probe-timeout 120
(python = F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe)
```

## Last green gate

- Build: `Saved/AgentLogs/build-packusage2-1.log` Result: Succeeded (2026-09-27).
- Native: pending (run 3).
