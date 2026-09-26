@echo off
setlocal
if not exist "%~dp0Binaries\Win64\UnrealEditor-CiresTeamSurvival.dll" (
  echo Run Build.cmd first.
  pause
  exit /b 1
)
start "Cire's Team Survival" "F:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0CiresTeamSurvival.uproject" /Game/Maps/Citadel -game
