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

typedef wchar_t wchar;

// clang-format off

// clang-tidy NOLINTBEGIN
#if COMPRESSOR_DEBUG
#    define ASSERT(expr) if (!(expr)) { *(static_cast<int*>(nullptr)) = 0; }
#    define INVALID_CODE_PATH ASSERT(false) // TODO: Crashes for now
#else
#    define ASSERT(expr)
#    define INVALID_CODE_PATH
#endif
// clang-tidy NOLINTEND

#define ARR_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))

// clang-format on

/**
 * Returns number of bytes, NOT the number of "real" characters
 */
static i32
StrLength(const char* str) {
    ASSERT(str);

    i32 len = 0;
    while (*str++) {
        len++;
    }

    return len;
}

/**
 * Returns number of code units (wchar elements), DON'T use for byte counts or file I/O
 * Same as StrLength but for UTF-16, doesn't return the "real" count of perceived characters
 */
static i32
StrLengthW(const wchar* str) {
    ASSERT(str);

    i32 len = 0;
    while (*str++) {
        len++;
    }

    return len;
}

/**
 * Inspects the bytes in order
 */
static bool32
StrEqual(const char* a, const char* b) {
    ASSERT(a && b);

    while (*a && *b) {
        if (*a != *b) {
            return false;
        }

        ++a;
        ++b;
    }

    return *a == *b;
}
