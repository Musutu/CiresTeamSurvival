# RESUME: pack-usage pass #2 (feat/pack-usage-2)

Worktree `F:\CiresTeamSurvival-agents\cts-pack-usage-2`, branch `feat/pack-usage-2` from main 702f582. Test ports 17460-17469.
Brief: full integration of every purchased Niagara / VFX pack + Fantasy_UI_SFX wiring; quality first, reuse with recolour;
usage % per pack before -> after in `Docs/PackUsage.md`. Weapons / grips / WeaponLoadouts are OFF LIMITS (feat/blender-rig).
Stopped 2026-09-27 on the coordinator's spend limit (wrap-up); everything below "Done" is committed and built.

## Done (checkpoint 1, commit 7cd5819e + wrap-up commit)

- Tooling: `Tools/InventoryFabVFX.py` -> `Art/Fab/FabVFXInventory.json` (538 systems; quality/look fields still EMPTY),
  `Tools/RunFabVFXCatalog.py` + `Source/.../CireFabVFXCatalog.cpp` (`-CireFabVFXCatalog=<list.json>`),
  `Tools/AuditPackUsage.py --before main` (table in `Docs/PackUsage.md`), `Tools/FabMonsterVFXTable.py` (race kits, CC
  tints, hit / kill / event signatures), `Tools/MapFabVFX.py` (dict values with tint / strength, merges all tables).
- Code: `CireFabVFX::Recolor` / `ApplyEntryTint` / `FindKey` (+ `tintStrength`), monster Gather wind-up cast overlay,
  hit / crit signature by target body x attacker weapon (`CireSpellPresentation.cpp`), kill burst on monster death
  (`CireMonsterArt.cpp`), level-up flourish (`CireHUDWow.cpp`), shop close + toast cues (`CireShopUI.cpp`),
  `CireFabVFX::RunTests` extended (tints, hit/kill/level_up keys, every monster ability has a signature, no twin cast look).
- Data: `Content/Data/FabVFX.json` regenerated (543 abilities / 980 slots, 344 monster abilities, recolour de-dup per race
  and per champion); audio `AudioCues.json` / `FabAudioMap.json` (`ui_toast`, `ui_shop_close`, banners, more takes).
- Captures: before galleries `Saved/AbilityVFX/before-packusage2-{champion,voidborn,stoneborn}-*` (PASS);
  catalogue of all 538 systems `Saved/FabVFXCatalog/20260927T042848Z/sheets/*.jpg` (30 sheets, early + late frame).
- Gates on the branch: native PASS (`Saved/AgentLogs/native-packusage2-4.log`); network / interface: see wrap-up logs.

## Not done

- Quality rating of the inventory (A/B/C + look) from the catalogue sheets, and revisiting picks by rating.
- After galleries + before/after comparison sheets (the after runs were stopped by the wrap-up).
- Visual review of the tinted monster kits in game; Eric's question whether the arena portals should get the real recolour.
- Big Pack systems still unused (~60), Shadow_Magic 18, Earth_Spells 8, RealisticBlood decals / dripping / artery kits.

## Next queue

1. Rate `Art/Fab/FabVFXInventory.json` from the sheets; demote C-rated picks in `FabMonsterVFXTable.py` / `FabAbilityVFXTable.py`.
2. `python Tools/RunAbilityVFXGallery.py --set champion --tag after`, `--set voidborn`, `--set stoneborn`; `--compare BEFORE AFTER`.
3. `python Tools/MapFabVFX.py`; native gate; `python Tools/AuditPackUsage.py --before main`; commit.

## Commands

```
Build:   cmd //c "F:\UE_5.8\Engine\Build\BatchFiles\Build.bat CiresTeamSurvivalEditor Win64 Development -Project=<worktree>\CiresTeamSurvival.uproject -WaitMutex -NoHotReloadFromIDE"
Data:    python Tools/MapFabVFX.py ; python Tools/MapFabAudio.py --installed-only ; python Tools/BuildAudioEvents.py
Gates:   python Tools/RunExpansionChecks.py --only native --timeout 240 --port P
         python Tools/RunNetworkSmoke.py --port P --startup-timeout 120 --probe-timeout 120
         python Tools/RunInterfaceSmoke.py --port P --startup-timeout 120 --probe-timeout 120
(python = F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe)
```
