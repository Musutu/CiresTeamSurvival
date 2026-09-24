@echo off
setlocal
call "F:\UE_5.8\Engine\Build\BatchFiles\Build.bat" CiresTeamSurvivalEditor Win64 Development -Project="%~dp0CiresTeamSurvival.uproject" -WaitMutex -NoHotReloadFromIDE
if errorlevel 1 (
  echo Build failed. Review the error above.
  pause
  exit /b 1
)
echo Build complete.
pause
