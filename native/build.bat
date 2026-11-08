@echo off
rem Builds Silly Season with MSVC (Visual Studio 2022 or later, "Desktop development with C++").
rem   build.bat          -> native\silly_season.dll
rem   build.bat test     -> also builds and runs native\tests\sim_test.exe
rem   build.bat preview <dir> [en|es] [rounds] -> draws the F7 hub and the banner into SVG files
setlocal
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
set FLAGS=/nologo /std:c++latest /EHsc /O2 /MT /W3 /Zc:preprocessor /Zc:__cplusplus /utf-8 /permissive- /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /I include /Fobuild\

cl %FLAGS% /LD src\plugin.cpp src\market.cpp src\text.cpp src\names.cpp /Fe:silly_season.dll /link /NOLOGO /IMPLIB:build\silly_season.lib || exit /b 1
del /q silly_season.exp 2>nul
echo built %CD%\silly_season.dll

if /i "%1"=="test" (
    cl %FLAGS% tests\sim_test.cpp src\market.cpp src\text.cpp src\names.cpp /Fe:build\sim_test.exe /link /NOLOGO || exit /b 1
    build\sim_test.exe ..\data\baseline.json 10 %2 || exit /b 1
)
if /i "%1"=="preview" (
    cl %FLAGS% /LD tests\core_stub.cpp /Fe:build\swofty_core.dll /link /NOLOGO /NOIMPLIB || exit /b 1
    cl %FLAGS% tests\overlay_preview.cpp src\market.cpp src\text.cpp src\names.cpp /Fe:build\overlay_preview.exe /link /NOLOGO || exit /b 1
    build\overlay_preview.exe ..\data\baseline.json %2 %3 %4 || exit /b 1
)
endlocal
