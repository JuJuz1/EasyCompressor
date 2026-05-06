@echo off

IF NOT EXIST build mkdir build
pushd build

set commonCompilerDefines=-DCOMPRESSOR_WIN32=1 -DCOMPRESSOR_INTERNAL=1 -DCOMPRESSOR_DEBUG=1

rem /FAs /Fm, .asm and .map
rem /MT for release
set commonCompilerFlags=%commonCompilerDefines% /W4 /MTd /Zi /Zc:__cplusplus /FC /Od /Oi /EHa- /GR- /GS- /std:c++20 /nologo
rem /LTCG link time optimization
set commonLinkerFlags=/INCREMENTAL:NO /NOCOFFGRPINFO /EMITTOOLVERSIONINFO:NO /OPT:REF /OPT:ICF /FIXED /merge:_RDATA=.rdata /SUBSYSTEM:WINDOWS
set win32Libraries=Kernel32.lib User32.lib Shell32.lib Comdlg32.lib
set dxLibraries=d3d11.lib dxgi.lib d3dcompiler.lib

set buildFailed=0

del *.pdb

cl %commonCompilerFlags% ../src/win32_compressor.cpp /I ../src /I ../vendor/imgui /link %commonLinkerFlags% %win32Libraries% %dxLibraries%

if ERRORLEVEL 1 (
    set buildFailed=1
    echo [31m[1mwin32_compressor.cpp failed[0m[1m
)

popd

rem Don't remember the layout when testing UX
IF EXIST imgui.ini del imgui.ini

set NOW=%TIME:~0,8%

echo.
if %buildFailed% NEQ 0 (
    echo [31m[1mBuild failed[0m[1m %DATE% %NOW%
) else (
    echo [32m[1mBuild succeeded[0m[1m %DATE% %NOW%
)
