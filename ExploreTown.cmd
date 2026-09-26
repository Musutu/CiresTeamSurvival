@echo off
setlocal
rem Walk the Medieval Kingdom town (the main map) with no waves, bots or packs.
rem   F5 fly/walk (Space up, X down, Shift fast)   F6 mark route point   F7 mark challenge-pack spot
rem   F4 mark vendor spot   Backspace undo   F2 next landmark (Shift+F2 back)   Home other realm
rem Marks are written to Saved\Explore\TownMarks.json (realm-local centimetres). See Docs\CastleTown.md.
if not exist "%~dp0Binaries\Win64\UnrealEditor-CiresTeamSurvival.dll" (
  echo Run Build.cmd first.
  pause
  exit /b 1
)
if not exist "%~dp0Content\CastleTown\Levels\Persistant\PL_CastleTown.umap" (
  echo The Medieval Kingdom pack is not installed in Content\CastleTown ^(it is never committed^).
  echo Add it to the project from Fab, or run Tools\LinkFabContent.py --include-fab from the main checkout.
  pause
  exit /b 1
)
if not exist "%~dp0Content\__ExternalActors__\CastleTown\Levels\SubLevels\SL_Landscape" (
  echo Warning: the pack's external actors are missing ^(Content\__ExternalActors__\CastleTown^); the town will have no terrain.
  echo See Docs\CastleTown.md "Requirements".
)
start "Cire's Team Survival - Explore the Town" "F:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0CiresTeamSurvival.uproject" /Game/Maps/Citadel -game -windowed -ResX=1600 -ResY=900 -NoLiveCoding -CireExplore
