#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

// https://github.com/doctest/doctest/blob/master/doc/markdown/benchmarks.md#cost-of-an-assertion-macro
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS // Speed!!!
#include "doctest.h"

#include "win32_compressor.cpp"

TEST_CASE("Test fail") {
    CHECK_EQ(2, 1);
    CHECK_EQ(2, 6);
}

TEST_CASE("Test success") { CHECK_EQ(2.5f, 2.5f); }
