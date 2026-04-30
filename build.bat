@echo off

IF NOT EXIST build mkdir build
pushd build

set commonCompilerDefines=-DCOMPRESSOR_WIN32=1 -DCOMPRESS_INTERNAL=1 -DCOMPRESS_DEBUG=1

set commonCompilerFlags=%commonCompilerDefines% /W4 /MTd /Zi /Zc:__cplusplus /FC /Fm /Od /Oi /EHa- /GR- /std:c17 /nologo
set commonLinkerFlags=/INCREMENTAL:NO /NOCOFFGRPINFO /EMITTOOLVERSIONINFO:NO /LTCG /OPT:REF /OPT:ICF /FIXED /merge:_RDATA=.rdata
set win32Libraries=Kernel32.lib

set exportedFunctions=/EXPORT:Compress

rem MSVC pdb sometimes don't build correctly and cause issues with Visual Studio
del *.pdb >nul 2>nul

echo WAITING FOR PDB > lock.tmp

set buildFailed=0

cl %commonCompilerFlags% ../src/compressor.c /I ../src /LD /link /PDB:compressor_%random%.pdb %exportedFunctions% %commonLinkerFlags%

if ERRORLEVEL 1 (
    set buildFailed=1
    echo [31m[1mcompressor.c failed[0m[1m
)

del lock.tmp

cl %commonCompilerFlags% ../src/win32_compressor.c /I ../src /link %commonLinkerFlags% %win32Libraries%

if ERRORLEVEL 1 (
    set buildFailed=1
    echo [31m[1mwin32_compressor.c failed[0m[1m
)

popd

set NOW=%TIME:~0,8%

echo.
if %buildFailed% NEQ 0 (
    echo [31m[1mBuild failed[0m[1m %DATE% %NOW%
) else (
    echo [32m[1mBuild succeeded[0m[1m %DATE% %NOW%
)
