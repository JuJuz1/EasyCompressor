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

set defines=-DCOMPRESSOR_WIN32=1 -DCOMPRESSOR_DEBUG=1
set flags=/W4 /FC /Oi /EHa- /GR- /GS /std:c++20 /nologo

rem Couldn't get AddressSanitizer to be found automatically
rem so just copied the clang_rt.asan_dynamic-x86_64.dll to root...

rem set VS=""
rem where /Q clssasdasdasd.exe || (
rem     echo start
rem   set __VSCMD_ARG_NO_LOGO=1
rem   for /f "tokens=*" %%i in ('"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.VisualStudio.Workload.NativeDesktop -property installationPath') do set VS=%%i
rem   if "!VS!" == "" (
rem     echo ERROR: Visual Studio installation not found
rem     exit /b 1
rem   )
rem   echo calling VsDevCmd.bat
rem   call "!VS!\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -startdir=none -no_logo || exit /b 1
rem )

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

echo !defines!
echo !flags!
set flags=!defines! !flags!

rem TODO: take a look at /FIXED
rem TODO: examine flags that might make Microsoft Defender flag as a virus
rem set linkerFlags=/INCREMENTAL:NO /NOCOFFGRPINFO /EMITTOOLVERSIONINFO:NO /OPT:REF /OPT:ICF /FIXED /merge:_RDATA=.rdata /SUBSYSTEM:WINDOWS
set linkerFlags=/SUBSYSTEM:WINDOWS

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
