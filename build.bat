@echo off
rem Builds the mod for Geometry Dash 2.2091 with the Geode SDK 6.0.0: build\zekrom.sp-creator.geode. It isn't
rem installed: copy it into the game's geode\mods folder.
rem   build.bat        a release build
rem   build.bat smoke  with the smoke test in, for development (tools/run_smoke.ps1)
set SMOKE=OFF
if /i "%~1"=="smoke" set SMOKE=ON
echo Smoke test in the build: %SMOKE%
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set GEODE_SDK=C:\Users\user\geode-sdk-6
set PATH=C:\Users\user\geode-cli-4;%PATH%
cd /d "%~dp0"
rem Eclipse Menu's API headers, if it's installed: the smoke test sets its options.
set ECLIPSE_API=C:\Users\user\GD-2.2091\geode\unzipped\eclipse.eclipse-menu
rem Codegen is built from the bindings clone (GEODE_BINDINGS_REPO_PATH), at the commit the SDK was made with.
set GEODE_BINDINGS_REPO_PATH=C:\dev\geode-bindings-6
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSKIP_BUILDING_CODEGEN=OFF -DGEODE_DONT_INSTALL_MODS=ON -DSPC_SMOKE=%SMOKE% "-DSPC_ECLIPSE_API=%ECLIPSE_API%" || exit /b 1
cmake --build build || exit /b 1
echo Built: %~dp0build\zekrom.sp-creator.geode
