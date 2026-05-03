#pragma once

/*
    Platform independent stuff here used for possible porting
*/

#include <stddef.h> // size_t
#include <stdint.h> // common types

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

// clang-format off
#if COMPRESSOR_DEBUG
#    define ASSERT(expr) if (!(expr)) { *(static_cast<int*>(nullptr)) = 0; }
#else
#    define ASSERT(expr)
#endif
// clang-format on

static i32
StrLength(const char* str) {
    ASSERT(str);

    i32 len{};
    while (*str++) {
        len++;
    }

    return len;
}

//enum class LogType {
//    Info = 0,
//    Warn,
//    Error,
//};

//// clang-format off
//#define PRINT_IMPL(name) void name(LogType type, const char* fmt, ...)
//typedef PRINT_IMPL(print_impl);
//// clang-format on

//struct Exports {
//    print_impl* print;
//};

//// Supplied to compressor dll
//struct Memory {
//    void* memory;
//    u64 memorySize;

//    Exports exports;
//};

//struct CompressorParams {
//    const char* ffmpegPath;

//    const char* input;
//    const char* output;

//    double targetSizeMb;
//};

//// clang-format off
//#define COMPRESS_IMPL(name) void name(Memory* memory, CompressorParams* params)
//typedef COMPRESS_IMPL(compressor_impl);
//// clang-format on
