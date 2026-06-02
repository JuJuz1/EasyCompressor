@echo off
setlocal enabledelayedexpansion

IF not EXIST build mkdir build
pushd build

set arg1=%1
set arg2=%2

set config=debug
set modeApp=1
set modeTest=0

rem default modeApp: 1 modeTest: 0
rem build test: modeApp: 1 modeTest: 1
rem test-only: modeApp: 0 modeTest: 1

rem build -> debug build
rem build rel -> release build
rem build test -> debug build and run debug test
rem build rel test -> rel build and run rel test
rem build test-only -> run debug test
rem build rel test-only -> run rel test
rem "rel" can be replaced with "release" anytime

if "!arg1!" == "rel" (
    set config=release
) else if "!arg1!" == "release" (
    set config=release
) else if "!arg2!" == "rel" (
    set config=release
) else if "!arg2!" == "release" (
    set config=release
)

if "!arg1!" == "test" (
    set modeTest=1
) else if "!arg2!" == "test" (
    set modeTest=1
)

rem test-only overrides everything
if "!arg1!" == "test-only" (
    set modeApp=0
    set modeTest=1
) else if "!arg2!" == "test-only" (
    set modeApp=0
    set modeTest=1
)

echo Config: !config!
echo App: !modeApp!
echo Test: !modeTest!

rem /FAs /Fm, .asm and .map
rem /LTCG link time optimization, not really used for unity builds I suppose
rem not used: /Zc:__cplusplus

rem TODO: remove debug from release
set defines=-DCOMPRESSOR_WIN32=1 -DCOMPRESSOR_DEBUG=1
rem /Bt+ compile times
set flags=/W4 /FC /Oi /EHa- /GR- /GS /std:c++20 /utf-8 /nologo

rem Couldn't get AddressSanitizer to be found automatically
rem so just copied the clang_rt.asan_dynamic-x86_64.dll to root...

rem debug: /MTd /Zi /Od
rem release: /MT /O2
if !config! == debug (
    set defines=!defines! -DCOMPRESSOR_DEV=1 -DCOMPRESSOR_DEBUG=1
    rem /fsanitize=address
    set flags=!flags! /MTd /Od /Zi
) else if !config! == release (
    rem /MT /O2
    set flags=!flags! /MD /Od
)

set flagsCombined=!defines! !flags!

rem TODO: take a look at /FIXED
rem TODO: examine flags that might make Microsoft Defender flag as a virus
rem set linkerFlags=/INCREMENTAL:NO /NOCOFFGRPINFO /EMITTOOLVERSIONINFO:NO /OPT:REF /OPT:ICF /FIXED /merge:_RDATA=.rdata /SUBSYSTEM:WINDOWS
set linkerFlags=/INCREMENTAL:NO /NOCOFFGRPINFO /EMITTOOLVERSIONINFO:NO /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS
rem set linkerFlags=/SUBSYSTEM:WINDOWS

rem libraries
set win32Libraries=Kernel32.lib User32.lib Shell32.lib Comdlg32.lib Shlwapi.lib Pathcch.lib
set dxLibraries=d3d11.lib dxgi.lib d3dcompiler.lib

set buildFailed=0

if !modeApp! == 1 (
    echo !defines!
    echo !flags!

    set BUILD_START=!TIME!
    cl !flagsCombined! ../src/win32_compressor.cpp /I ../src /I ../vendor/imgui ^
    /link !linkerFlags! !win32Libraries! !dxLibraries!
    set BUILD_END=!TIME!

    set NOW=!TIME:~0,8!

    if ERRORLEVEL 1 (
        set buildFailed=1
        echo [31m[1mwin32_compressor.cpp failed[0m[1m
    )

    rem Don't remember the layout when testing UX
    IF EXIST imgui.ini del imgui.ini

    set NOW=!TIME:~0,8!

    echo.
    if !buildFailed! NEQ 0 (
        echo [31m[1mBuild failed[0m[1m !DATE! !NOW!
    ) else (
        echo [32m[1mBuild succeeded[0m[1m !DATE! !NOW!
    )
)

if !buildFailed! == 0 if !modeApp! == 1 (
    call :OutputCommandTime "!BUILD_START!" "!BUILD_END!" "Build" !modeApp!
)

if !modeTest! == 1 if !buildFailed! == 0 (
    echo.
    echo Building tests...
    set defines=!defines! -DCOMPRESSOR_TESTS=1
    echo !defines!
    echo !flags!
    set flagsCombined=!defines! !flags!

    set TEST_BUILD_START=!TIME!
    rem /wd4505 unreferenced internal function removed
    cl !flagsCombined! /wd4505 ../src/compressor_tests.cpp /I ../src /I ../vendor ^
    /link /SUBSYSTEM:CONSOLE !win32Libraries!
    set TEST_BUILD_END=!TIME!

    set NOW=!TIME:~0,8!
    if ERRORLEVEL 1 (
        echo [31m[1mtests.cpp failed[0m[1m !DATE! !NOW!
        set buildFailed=1
    ) else (
        echo Running tests...
        rem --success, show all INFO output
        rem TODO: figure out a way to show total time taken for tests
        rem probably have to introduce custom main() and measure there
        rem --duration only shows time per test
        rem Currently done via .bat logic which is truly something, see below
        set TEST_RUN_START=!TIME!
        compressor_tests.exe --no-intro
        set TEST_RUN_END=!TIME!
    )
)

if !buildFailed! == 0 if !modeTest! == 1 (
    echo.
    call :OutputCommandTime "!TEST_BUILD_START!" "!TEST_BUILD_END!" "Test build"
    call :OutputCommandTime "!TEST_RUN_START!" "!TEST_RUN_END!" "Test run"
)

popd

echo.
if !buildFailed! NEQ 0 (
    echo [31m[1mBUILD FAILED[0m[1m
    exit /b 1
)

echo [32m[1mBUILD SUCCESS[0m[1m
exit /b 0


rem Really? A function in a .bat? YES
:OutputCommandTime
set START=%~1
set END=%~2
set TEXT=%~3

rem I mean wtf is this, it works though...
for /f "tokens=1-4 delims=:,." %%a in ("!START!") do (
    set S_H=%%a
    set S_M=%%b
    set S_S=%%c
    set S_MS=%%d
)

for /f "tokens=1-4 delims=:,." %%a in ("!END!") do (
    set E_H=%%a
    set E_M=%%b
    set E_S=%%c
    set E_MS=%%d
)

set /a S_H=1%S_H%-100 2>nul
set /a S_M=1%S_M%-100 2>nul
set /a S_S=1%S_S%-100 2>nul
set /a S_MS=1%S_MS%-100 2>nul
set /a E_H=1%E_H%-100 2>nul
set /a E_M=1%E_M%-100 2>nul
set /a E_S=1%E_S%-100 2>nul
set /a E_MS=1%E_MS%-100 2>nul
set /a DIFF_H=E_H-S_H 2>nul
set /a DIFF_M=E_M-S_M 2>nul
set /a DIFF_S=E_S-S_S 2>nul
set /a DIFF_MS=E_MS-S_MS 2>nul
set /a ELAPSED=DIFF_H*360000 2>nul
set /a ELAPSED=ELAPSED+DIFF_M*6000 2>nul
set /a ELAPSED=ELAPSED+DIFF_S*100 2>nul
set /a ELAPSED=ELAPSED+DIFF_MS 2>nul
set /a SECS=ELAPSED/100 2>nul
set /a CS=ELAPSED%%100 2>nul

echo !TEXT!: !SECS!.!CS!s

goto :eof
