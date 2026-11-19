@echo off
rem Builds Silly Season with MSVC (Visual Studio 2022 or later, "Desktop development with C++").
rem   build.bat                  -> native\silly_season.dll (same as "build.bat dll")
rem   build.bat test [n]         -> the DLL, then runs sim_test, grid_test, history_test and plugin_test (n: extra
rem                              sim_test argument)
rem   build.bat quick            -> the same with sim_test's careers 3 seasons long: the quick loop while working
rem   build.bat tests [n]        -> the four tests, without the DLL
rem   build.bat sim [n]          -> only sim_test; "grid", "history" and "plugin" likewise (a quick loop)
rem   build.bat plugin [what]    -> only plugin_test: plugin.cpp run through whole careers against a fake game
rem                              (tests\fake_game.hpp). what: "bench" times the handlers, "quiet" hides the addon's log
rem   build.bat preview <dir> [en|es] [rounds] -> draws the F7 hub, the cards and every overlay into SVG files
rem   build.bat preview bench [en|es] [rounds]  -> 1.1.4: times drawing every F7 page (ms and draw calls a frame)
rem   build.bat bench [careers] [seasons] -> times the engine (rounds, season turn, save, load) and the save's size,
rem                              against the previous run (build\bench_last.json)
rem   build.bat texts [careers] [seasons] -> every text the market writes over many careers, into build\texts.json
rem                              (tools\texts.py reads it: gaps, ids in sight, lengths, kinds that repeat themselves)
rem   build.bat benchexe <tree> <out.exe> -> only builds the bench from a source tree (its src, include and tests\bench.cpp):
rem                              tools\bench_ab.py uses it to time an older commit against the working tree
rem Every source of a run compiles once and in parallel (/MP); the engine (market, text, names) is shared by the
rem DLL and the tests instead of being compiled again for each.
rem The DLL is linked with its PDB (build\<name>.pdb, never packaged): /PDBALTPATH:%%_PDB%% keeps this PC's path out of
rem the DLL. tools\crash_lab.py stores each version's PDB and names our frames in crash reports.
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo No se encuentra vswhere.exe: instala Visual Studio con "Desarrollo para el escritorio con C++".
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio no tiene las herramientas de C++ x64 instaladas.
    exit /b 1
)
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not exist build mkdir build
set FLAGS=/nologo /std:c++latest /EHsc /O2 /MT /W3 /Zc:preprocessor /Zc:__cplusplus /utf-8 /permissive- /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /Z7 /I include /Fobuild\
rem the addon's version, from the manifest, for the "ready" line it logs (a player's log then says which build ran)
set "SSVER=dev"
for /f "tokens=2 delims=:, " %%v in ('findstr /c:"\"version\"" ..\swofty.json') do if "!SSVER!"=="dev" set "SSVER=%%~v"
set FLAGS=%FLAGS% /DSS_VERSION=\"%SSVER%\"
set ENGINE=src\market.cpp src\text.cpp src\names.cpp
set ENGINE_OBJ=build\market.obj build\text.obj build\names.obj

set "WHAT=%~1"
if "%WHAT%"=="" set "WHAT=dll"
if /i "%WHAT%"=="preview" goto preview
if /i "%WHAT%"=="bench" goto bench
if /i "%WHAT%"=="benchexe" goto benchexe
if /i "%WHAT%"=="texts" goto texts
set DLL=0
set TESTS=
if /i "%WHAT%"=="dll" set DLL=1
if /i "%WHAT%"=="test" (
    set DLL=1
    set "TESTS=sim grid history plugin"
)
if /i "%WHAT%"=="tests" set "TESTS=sim grid history plugin"
rem quick: the same as test with sim_test's careers three seasons long instead of ten (the loop while working; run
rem "test" before a release)
set SIMSEASONS=10
if /i "%WHAT%"=="quick" (
    set DLL=1
    set "TESTS=sim grid history plugin"
    set SIMSEASONS=3
)
if /i "%WHAT%"=="sim" set "TESTS=sim"
if /i "%WHAT%"=="grid" set "TESTS=grid"
if /i "%WHAT%"=="history" set "TESTS=history"
if /i "%WHAT%"=="plugin" set "TESTS=plugin"
if "%DLL%"=="0" if "%TESTS%"=="" (
    echo build.bat: unknown target "%WHAT%" ^(dll, test, quick, tests, sim, grid, history, plugin, preview, bench, texts^)
    exit /b 1
)

set "SRC=%ENGINE%"
if "%DLL%"=="1" set "SRC=!SRC! src\plugin.cpp"
for %%t in (%TESTS%) do set "SRC=!SRC! tests\%%t_test.cpp"
cl %FLAGS% /MP /c %SRC% || exit /b 1

if "%DLL%"=="1" (
    link /NOLOGO /DLL build\plugin.obj %ENGINE_OBJ% /OUT:silly_season.dll /IMPLIB:build\silly_season.lib /DEBUG /PDB:build\silly_season.pdb /PDBALTPATH:%%_PDB%% /OPT:REF /OPT:ICF /INCREMENTAL:NO || exit /b 1
    del /q silly_season.exp 2>nul
    echo built %CD%\silly_season.dll
)
for %%t in (%TESTS%) do (
    link /NOLOGO build\%%t_test.obj %ENGINE_OBJ% /OUT:build\%%t_test.exe || exit /b 1
)
rem plugin_test needs the core's stand-in next to it (the SDK's typed events look for its exports)
set "PLUGIN_ARG="
if /i "%WHAT%"=="plugin" set "PLUGIN_ARG=%~2"
for %%t in (%TESTS%) do (
    if /i "%%t"=="plugin" (
        cl %FLAGS% /LD tests\core_stub.cpp /Fe:build\swofty_core.dll /link /NOLOGO /NOIMPLIB >nul || exit /b 1
    )
)
for %%t in (%TESTS%) do (
    if /i "%%t"=="sim" (
        build\sim_test.exe ..\data\baseline.json !SIMSEASONS! %2 || exit /b 1
    ) else if /i "%%t"=="plugin" (
        build\plugin_test.exe ..\data\baseline.json ..\swofty.json !PLUGIN_ARG! || exit /b 1
    ) else (
        build\%%t_test.exe ..\data\baseline.json || exit /b 1
    )
)
endlocal
exit /b 0

:preview
cl %FLAGS% /LD tests\core_stub.cpp /Fe:build\swofty_core.dll /link /NOLOGO /NOIMPLIB || exit /b 1
cl %FLAGS% /MP /c %ENGINE% tests\overlay_preview.cpp || exit /b 1
link /NOLOGO build\overlay_preview.obj %ENGINE_OBJ% /OUT:build\overlay_preview.exe || exit /b 1
build\overlay_preview.exe ..\data\baseline.json %2 %3 %4 || exit /b 1
endlocal
exit /b 0

:bench
cl %FLAGS% /MP /c %ENGINE% tests\bench.cpp || exit /b 1
link /NOLOGO build\bench.obj %ENGINE_OBJ% /OUT:build\bench.exe || exit /b 1
set "CAREERS=%~2"
set "SEASONS=%~3"
if "%CAREERS%"=="" set CAREERS=4
if "%SEASONS%"=="" set SEASONS=6
if exist build\bench_last.json (
    build\bench.exe ..\data\baseline.json %CAREERS% %SEASONS% --json build\bench_new.json --compare build\bench_last.json || exit /b 1
) else (
    build\bench.exe ..\data\baseline.json %CAREERS% %SEASONS% --json build\bench_new.json || exit /b 1
)
move /y build\bench_new.json build\bench_last.json >nul
endlocal
exit /b 0

:texts
cl %FLAGS% /MP /c %ENGINE% tests\text_dump.cpp || exit /b 1
link /NOLOGO build\text_dump.obj %ENGINE_OBJ% /OUT:build\text_dump.exe || exit /b 1
set "CAREERS=%~2"
set "SEASONS=%~3"
if "%CAREERS%"=="" set CAREERS=12
if "%SEASONS%"=="" set SEASONS=6
build\text_dump.exe ..\data\baseline.json build\texts.json %CAREERS% %SEASONS% || exit /b 1
endlocal
exit /b 0

:benchexe
set "TREE=%~f2"
set "OUT=%~f3"
if not exist "%TREE%\src\market.cpp" (
    echo build.bat benchexe: no engine sources in "%TREE%"
    exit /b 1
)
if not exist "%TREE%\tests\bench.cpp" (
    echo build.bat benchexe: no tests\bench.cpp in "%TREE%"
    exit /b 1
)
rem its objects go under the tree's build folder (ignored by git), apart from the main build's
set "ABOBJ=%TREE%\build\abobj"
if not exist "%ABOBJ%" mkdir "%ABOBJ%"
set ABFLAGS=/nologo /std:c++latest /EHsc /O2 /MT /W3 /Zc:preprocessor /Zc:__cplusplus /utf-8 /permissive- /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX
cl %ABFLAGS% /I "%TREE%\include" /Fo"%ABOBJ%\\" /MP /c "%TREE%\src\market.cpp" "%TREE%\src\text.cpp" "%TREE%\src\names.cpp" "%TREE%\tests\bench.cpp" || exit /b 1
link /NOLOGO "%ABOBJ%\bench.obj" "%ABOBJ%\market.obj" "%ABOBJ%\text.obj" "%ABOBJ%\names.obj" /OUT:"%OUT%" || exit /b 1
endlocal
exit /b 0
