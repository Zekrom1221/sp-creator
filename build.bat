@echo off
rem Builds the mod and installs the .geode into the GD mods folder (setup_geode_mod does the install).
rem   build.bat        a release build
rem   build.bat smoke  with the smoke test in, for development (tools/run_smoke.ps1)
set SMOKE=OFF
if /i "%~1"=="smoke" set SMOKE=ON
echo Smoke test in the build: %SMOKE%
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set GEODE_SDK=C:\Users\User\geode-sdk
set PATH=%LOCALAPPDATA%\Microsoft\WinGet\Packages\GeodeSDK.GeodeCLI_Microsoft.Winget.Source_8wekyb3d8bbwe;%PATH%
cd /d "%~dp0"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSPC_SMOKE=%SMOKE% || exit /b 1
cmake --build build || exit /b 1
