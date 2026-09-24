@echo off
rem ---------------------------------------------------------------------------
rem  build.bat - configure and build gedi on Windows (MSVC / NMake Makefiles).
rem
rem  The vendored dependencies are 64-bit only:
rem      sdl2\lib\SDL2.lib
rem      thirdparty\llvm\lib\libclang.lib
rem  so the build MUST run in an x64 toolchain environment. Configuring from a
rem  32-bit developer prompt produces a target that looks for the cdecl-decorated
rem  _SDL_Init / _clang_* symbols, which the x64 import libs do not contain; the
rem  link then dies with ~94 unresolved externals, the only real clue being the
rem  easily-missed "LNK4272: library machine type 'x64' conflicts with target
rem  machine type 'x86'" warning. This script always selects x64 and throws away
rem  a cache that a 32-bit prompt left behind.
rem
rem  Usage:  build.bat [Debug^|Release] [clean]
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "SRC_DIR=%~dp0"
if "%SRC_DIR:~-1%"=="\" set "SRC_DIR=%SRC_DIR:~0,-1%"
set "BUILD_DIR=%SRC_DIR%\build"
set "BUILD_TYPE=Debug"
set "DO_CLEAN="

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="clean"   ( set "DO_CLEAN=1"         & shift & goto parse_args )
if /i "%~1"=="Debug"   ( set "BUILD_TYPE=Debug"   & shift & goto parse_args )
if /i "%~1"=="Release" ( set "BUILD_TYPE=Release" & shift & goto parse_args )
echo Unknown argument: %~1
echo Usage: build.bat [Debug^|Release] [clean]
exit /b 1
:args_done

rem --- Locate the x64 developer environment -----------------------------------
rem  The "(x86)" in the vswhere path is a parser hazard: cmd counts parentheses
rem  before it cares about quoting, so an expanded "Program Files (x86)" closes a
rem  multi-line "if ( )" block or a "for /f in ( )" clause early and the script
rem  dies with a bare "\Microsoft was unexpected at this time". Hence: single-line
rem  "if ... goto" tests, and !VSWHERE! rather than %VSWHERE% inside the for below
rem  - delayed expansion substitutes the path after the line has been parsed.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

set "VCVARS="
if not exist "%VSWHERE%" goto vswhere_done
rem  The extra outer pair of quotes is required too: cmd strips the first and the
rem  last quote of a back-quoted command, which would otherwise split the
rem  installer path on its spaces.
for /f "usebackq tokens=*" %%i in (`""!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath"`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
:vswhere_done

if exist "%VCVARS%" goto vcvars_ok
echo ERROR: could not locate vcvars64.bat.
echo        Install the MSVC "Desktop development with C++" workload, or run
echo        this script from an "x64 Native Tools Command Prompt for VS".
exit /b 1
:vcvars_ok

echo Using %VCVARS%
rem  Note: vcvars64.bat itself prints "'vswhere.exe' is not recognized" on some
rem  installs - that comes from Microsoft's script, not this one, and is harmless.
call "%VCVARS%" >nul
if errorlevel 1 goto env_failed
goto env_ok
:env_failed
echo ERROR: failed to initialise the x64 build environment.
exit /b 1
:env_ok

rem --- Drop a stale or 32-bit cache ---------------------------------------------
if not defined DO_CLEAN goto clean_done
if not exist "%BUILD_DIR%" goto clean_done
echo Cleaning %BUILD_DIR%
rmdir /s /q "%BUILD_DIR%"
:clean_done

if not exist "%BUILD_DIR%\CMakeCache.txt" goto cache_ok
findstr /i /c:"CMAKE_CXX_COMPILER:FILEPATH" "%BUILD_DIR%\CMakeCache.txt" | findstr /i /c:"Hostx86/x86" >nul
if errorlevel 1 goto cache_ok
echo Existing cache was configured for x86 - discarding it.
rmdir /s /q "%BUILD_DIR%\CMakeFiles" 2>nul
del /q "%BUILD_DIR%\CMakeCache.txt"
:cache_ok

rem --- Configure and build ------------------------------------------------------
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
pushd "%BUILD_DIR%"

cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% "%SRC_DIR%"
if errorlevel 1 goto build_failed

nmake
if errorlevel 1 goto build_failed

popd
echo.
echo Build succeeded: %BUILD_DIR%\gedi-gui.exe
exit /b 0

:build_failed
popd
echo.
echo ERROR: build failed.
exit /b 1
