#pragma once

#include <stddef.h> // size_t
#include <stdint.h> // common types

#ifndef TRUE
#    define TRUE 1
#endif

#ifndef FALSE
#    define FALSE 0
#endif

typedef int32_t bool32;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef i32 bool32;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float f32;
typedef double f64;

typedef enum LogType {
    LogType_Info = 0,
    LogType_Warn,
    LogType_Error,
} LogType;

// clang-format off
#define PRINT_IMPL(name) void name(LogType type, const char* fmt, ...)
typedef PRINT_IMPL(print_impl);
// clang-format on

typedef struct Exports {
    print_impl* print;
} Exports;

// Supplied to compressor dll
typedef struct Memory {
    void* memory;
    u64 memorySize;

    Exports exports;
} Memory;

typedef struct CompressorParams {
    const char* ffmpegPath;

    const char* input;
    const char* output;

    double targetSizeMb;
} CompressorParams;

// clang-format off
#define COMPRESS_IMPL(name) void name(Memory* memory, CompressorParams* params)
typedef COMPRESS_IMPL(compressor_impl);
// clang-format on
