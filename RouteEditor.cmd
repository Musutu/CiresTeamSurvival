@echo off
setlocal
rem dev-route-tools: the MAP LAYOUT EDITOR (Docs/MapLayout.md). A clean edit mode: no waves, monsters, draft, shop,
rem economy, prep timer or combat HUD - only the town, your champion and the setter bar.
rem -CireTown opens the medieval pack town once feat/medieval-kingdom has landed (the procedural town otherwise).
rem Your work autosaves to Saved\MapLayoutDraft.json; APPLY (Ctrl+S) writes Content\Data\MapLayout.json, which every match
rem loads; TEST (Ctrl+T) applies and launches a real match on the layout in a new window. Alt+F5 in a match restarts it on the layout.
if not exist "%~dp0Binaries\Win64\UnrealEditor-CiresTeamSurvival.dll" (
  echo Run Build.cmd first.
  pause
  exit /b 1
)
start "Cire's Team Survival - Map Layout Editor" "F:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0CiresTeamSurvival.uproject" /Game/Maps/Citadel -game -windowed -ResX=1600 -ResY=900 -NoLiveCoding -CireRouteEdit -CireTown -CireNoReplay %*
