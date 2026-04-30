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

// Supplied to compressor dll
typedef struct Memory {
    void* memory;
    u64 memorySize;
} Memory;

// clang-format off
#define COMPRESS_IMPL(name) i32 name(Memory* memory)
typedef COMPRESS_IMPL(compressor_impl);
// clang-format on
