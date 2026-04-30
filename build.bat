@echo off

set linkerOptions=/INCREMENTAL:NO /NOCOFFGRPINFO /EMITTOOLVERSIONINFO:NO /LTCG /OPT:REF /OPT:ICF /FIXED /merge:_RDATA=.rdata
cl compress.c /Od /Zi /W4 /EHsc- /GS- /std:c11 /GL /Fe:compress.exe /link %linkerOptions%
