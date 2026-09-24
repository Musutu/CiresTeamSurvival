@echo off
setlocal
if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    echo Visual Studio C++ Build Tools were not found.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "CIRES_VS=%%i"
if not defined CIRES_VS (
    echo Install the Visual Studio MSVC C++ compiler component.
    exit /b 1
)
call "%CIRES_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W4 /WX /permissive- /O2 /I"..\Source\CiresTeamSurvival\Rules" RulesTests.cpp ..\Source\CiresTeamSurvival\Rules\CiresRules.cpp /Fo"build\\" /Fe"build\CiresRulesTests.exe"
if errorlevel 1 (
    popd
    exit /b 1
)
build\CiresRulesTests.exe
set "CIRES_RESULT=%ERRORLEVEL%"
popd
exit /b %CIRES_RESULT%
