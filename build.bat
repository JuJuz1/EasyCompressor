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

rem remove debug from release
set defines=-DCOMPRESSOR_WIN32=1 -DCOMPRESSOR_DEBUG=1
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
set linkerFlags=/SUBSYSTEM:WINDOWS

rem libraries
set win32Libraries=Kernel32.lib User32.lib Shell32.lib Comdlg32.lib Shlwapi.lib Pathcch.lib
set dxLibraries=d3d11.lib dxgi.lib d3dcompiler.lib

set buildFailed=0

if !modeApp! == 1 (
    if exist *.pdb del /q win32_compressor.pdb

    echo !defines!
    echo !flags!

    cl !flagsCombined! ../src/win32_compressor.cpp /I ../src /I ../vendor/imgui ^
    /link !linkerFlags! !win32Libraries! !dxLibraries!

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

if !modeTest! == 1 (
    echo Building tests...
    set defines=!defines! -DCOMPRESSOR_TESTS=1
    echo !defines!
    echo !flags!
    set flagsCombined=!defines! !flags!

    cl !flagsCombined! /wd4505 ../src/compressor_tests.cpp /I ../src /I ../vendor ^
    /link /SUBSYSTEM:CONSOLE !win32Libraries!
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
        compressor_tests.exe --no-intro
        if ERRORLEVEL 1 (
            set buildFailed=1
            echo [31m[1mTests failed[0m[1m !DATE! !NOW!
        ) else (
            echo [32m[1mTests passed[0m[1m !DATE! !NOW!
        )
    )
)

popd

echo.
if !buildFailed! NEQ 0 (
    echo BUILD FAILED
    exit /b 1
) else (
    echo BUILD SUCCESS
    exit /b 0
)
