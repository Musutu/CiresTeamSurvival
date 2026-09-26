@echo off
setlocal
if "%~1"=="" (
  echo Usage: JoinLAN.cmd HOST_IP
  pause
  exit /b 1
)
start "Cire's Team Survival LAN Client" "F:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0CiresTeamSurvival.uproject" "%~1" -game -windowed -ResX=1280 -ResY=720 -CireTripoChampions
