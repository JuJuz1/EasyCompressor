#pragma once

#define MAX_PATH_COUNT 512

typedef struct PathInfo {
    char exeDir[MAX_PATH_COUNT]; // Absolute path to the exe directory
} PathInfo;

typedef struct CompressorCode {
    HMODULE dll;
    FILETIME lastWritetime;

    compressor_impl* compress;

    bool32 isValid;
} CompressorCode;
