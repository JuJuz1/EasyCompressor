@echo off
setlocal enabledelayedexpansion

IF not EXIST build mkdir build
pushd build

set argConfig=%1
set config=debug

if "!argConfig!" == "release" (
    set config=release
)

echo Config: !config!

rem /FAs /Fm, .asm and .map
rem /LTCG link time optimization, not really used for unity builds I suppose
rem not used: /Zc:__cplusplus

set defines=-DCOMPRESSOR_WIN32=1
set flags=/W4 /FC /Oi /EHa- /GR- /GS- /std:c++20 /nologo

rem debug: /MTd /Zi /Od
rem release: /MT /O2
if !config! == debug (
    set defines=!defines! -DCOMPRESSOR_DEV=1 -DCOMPRESSOR_DEBUG=1
    set flags=!flags! /MTd /Od /Zi
) else if !config! == release (
    set flags=!flags! /MT /O2
)

echo !defines!
echo !flags!
set flags=!defines! !flags!

rem TODO: take a look at /FIXED
set linkerFlags=/INCREMENTAL:NO /NOCOFFGRPINFO /EMITTOOLVERSIONINFO:NO /OPT:REF /OPT:ICF /FIXED /merge:_RDATA=.rdata /SUBSYSTEM:WINDOWS

rem libraries
set win32Libraries=Kernel32.lib User32.lib Shell32.lib Comdlg32.lib
set dxLibraries=d3d11.lib dxgi.lib d3dcompiler.lib

set buildFailed=0

if exist *.pdb del /q *.pdb

cl !flags! ../src/win32_compressor.cpp /I ../src /I ../vendor/imgui ^
/link !linkerFlags! !win32Libraries! !dxLibraries!

if ERRORLEVEL 1 (
    set buildFailed=1
    echo [31m[1mwin32_compressor.cpp failed[0m[1m
)

popd

rem Don't remember the layout when testing UX
IF EXIST imgui.ini del imgui.ini

set NOW=!TIME:~0,8!

echo.
if !buildFailed! NEQ 0 (
    echo [31m[1mBuild failed[0m[1m !DATE! !NOW!
) else (
    echo [32m[1mBuild succeeded[0m[1m !DATE! !NOW!
)
