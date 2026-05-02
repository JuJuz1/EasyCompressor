@echo off

IF NOT EXIST build mkdir build
pushd build

set commonCompilerDefines=-DCOMPRESSOR_WIN32=1 -DCOMPRESSOR_INTERNAL=1 -DCOMPRESSOR_DEBUG=1

set commonCompilerFlags=%commonCompilerDefines% /W4 /MTd /Zi /Zc:__cplusplus /FC /Fm /Od /Oi /EHa- /GR- /std:c++20 /nologo
rem /LTCG link time optimization
set commonLinkerFlags=/INCREMENTAL:NO /NOCOFFGRPINFO /EMITTOOLVERSIONINFO:NO /OPT:REF /OPT:ICF /FIXED /merge:_RDATA=.rdata
set win32Libraries=Kernel32.lib User32.lib Shell32.lib Comdlg32.lib
set dxLibraries=d3d11.lib dxgi.lib d3dcompiler.lib

set buildFailed=0

cl %commonCompilerFlags% ../src/win32_compressor_ui.cpp /I ../src /I ../vendor/imgui /link %commonLinkerFlags% %win32Libraries% %dxLibraries%

if ERRORLEVEL 1 (
    set buildFailed=1
    echo [31m[1mwin32_compressor_ui.cpp failed[0m[1m
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
