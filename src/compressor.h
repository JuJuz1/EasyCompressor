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

// clang-tidy NOLINTBEGIN
#if COMPRESSOR_DEBUG
#    define ASSERT(expr) if (!(expr)) { *(static_cast<int*>(nullptr)) = 0; }
#else
#    define ASSERT(expr)
#endif
// clang-tidy NOLINTEND

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))

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
