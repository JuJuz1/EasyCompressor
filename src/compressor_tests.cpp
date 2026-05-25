#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

// https://github.com/doctest/doctest/blob/master/doc/markdown/benchmarks.md#cost-of-an-assertion-macro
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS                 // Speed!!!
#define DOCTEST_CONFIG_TREAT_CHAR_STAR_AS_STRING          // For printing the strings out
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS // To use: REQUIRE
#include "doctest.h"

#include "win32_compressor.cpp"

/*
CANDIDATES TO TEST:
    - UTF8To16 / UTF16To8
    - ConstructPathsForJob
    - AddJob / RemoveJob / MoveJob
    - RunProbe / RunCompress
    - ParseTimeFromOutput
    - StrToF32
    - LoadConfigFile / CreateDefaultConfigFile
    - GetExeDirectory
*/

struct AppStateFixture {
    AppState appState = {};

    AppStateFixture() {
        // Other stuff?
        snprintf(appState.outputFolder, ARR_COUNT(appState.outputFolder), "C:\\output");
    }
};

TEST_CASE("StrEqual works correctly") {
    struct TC {
        const char* a;
        const char* b;
        bool32 exp;
    };

    auto tc = GENERATE(TC{ "first", "first", true }, TC{ "first", "second", false },
                       TC{ "", "", true }, TC{ "a", "ab", false }, TC{ "ab", "a", false });
    CHECK_EQ(StrEqual(tc.a, tc.b), tc.exp);
}

TEST_CASE("UTF8 round trip") {
    const char* str = GENERATE(
        "Easy Compressor is here!", "äöå", "C:\\Users\\Andy\\Desktop\\test_file – kopöäl.mp4", "",
        "ascii only", "mixed äöå and ascii", "∞ ≠ ≤ ≥ ± √ π Σ Δ", "– — “quotes” ‘single’ … • ™ © ®",
        "γειά σου κόσμε", "你好世界", "안녕하세요 세계", "مرحبا بالعالم", "שלום עולם",
        "😈 👩 🚀 some emojis", "👨‍👩‍👧‍👦 🏳️‍🌈 👍🏻", "☢ ☣ ⚠ ♫ ❤");

    wchar strW[MAX_PATH_COUNT];
    UTF8To16(str, strW);
    char back[MAX_PATH_COUNT];
    UTF16To8(strW, back);
    CHECK(StrEqual(str, back));
}

TEST_CASE("UTF16 round trip") {
    const wchar* strW =
        GENERATE(L"Easy Compr!", L"äöå", L"C:\\Users\\Andy\\Desktop\\test_file – kopöäl.mp4", L"",
                 L"ascii only", L"mixed äöå and ascii", L"∞ ≠ ≤ ≥ ± √ π Σ Δ",
                 L"– — “quotes” ‘single’ … • ™ © ®", L"γειά σου κόσμε", L"你好世界",
                 L"안녕하세요 세계", L"مرحبا بالعالم", L"שלום עולם", L"😈 👩 🚀 some emojis",
                 L"👨‍👩‍👧‍👦 🏳️‍🌈 👍🏻", L"☢ ☣ ⚠ ♫ ❤");

    char str[MAX_PATH_COUNT];
    UTF16To8(strW, str);
    wchar back[MAX_PATH_COUNT];
    UTF8To16(str, back);
    char str2[MAX_PATH_COUNT];
    UTF16To8(back, str2);
    CHECK(StrEqual(str, str2));
}

TEST_CASE_FIXTURE(AppStateFixture, "ConstructPathsForJob works correctly") {
    struct TC {
        const wchar* input;
        const char* exp;
    };

    auto tc = GENERATE(TC{ L"C:\\input\\video.mp4", "C:\\output\\video.mp4" },
                       TC{ L"C:\\input\\video äöå.mp4", "C:\\output\\video äöå.mp4" },
                       TC{ L"C:\\some\\deep\\path\\video.mp4", "C:\\output\\video.mp4" },
                       TC{ L"D:\\input\\no_extension", "C:\\output\\no_extension" },
                       TC{ L"C:\\input\\multiple.dots.mp4", "C:\\output\\multiple.dots.mp4" },
                       TC{ L"F:\\äöå folder\\video.mp4", "C:\\output\\video.mp4" },
                       TC{ L"C:\\input\\video 😈.mp4", "C:\\output\\video 😈.mp4" },
                       TC{ L"D:\\input\\фывьн - --.mp4", "C:\\output\\фывьн - --.mp4" },
                       TC{ L"K:\\input\\요 세계.hgfh.mp4", "C:\\output\\요 세계.hgfh.mp4" });

    UIJob j = {};
    ConstructPathsForJob(&appState, &j, tc.input);

    char inputUtf8[MAX_PATH_COUNT];
    UTF16To8(tc.input, inputUtf8);
    INFO("input:    ", inputUtf8);
    INFO("expected: ", tc.exp);
    INFO("actual:   ", j.output);
    CHECK(StrEqual(j.output, tc.exp));
}

TEST_CASE_FIXTURE(AppStateFixture, "IsPathFromOutputFolder works correctly") {
    struct TC {
        const wchar* input;
        bool32 exp;
    };

    auto tc = GENERATE(
        TC{ L"C:\\output\\video.mp4", true }, TC{ L"C:\\output\\subfolder\\video.mp4", false },
        TC{ L"C:\\other\\video.avi", false }, TC{ L"C:\\output äöå\\video.mkv", false },
        TC{ L"C:\\other\\", false }, TC{ L"C:\\output\\", true }, TC{ L"C:\\output", false });

    char inputUtf8[MAX_PATH_COUNT];
    UTF16To8(tc.input, inputUtf8);
    INFO("input:    ", inputUtf8);
    INFO("expected: ", tc.exp);
    CHECK_EQ(IsPathFromOutputFolder(&appState, tc.input), tc.exp);
}

// Temp file for AddJob
struct TempFileFixture {
    // TODO: test paths greater than MAX_PATH: 260 and current MAX_PATH_COUNT
    wchar dir[MAX_PATH_COUNT];
    wchar path[MAX_PATH_COUNT];

    TempFileFixture() {
        i32 val = GetTempPathW(ARR_COUNT(dir), dir);
        REQUIRE(val != 0);
        dir[val - 1] = '\0'; // Remove trailing slash...
        //CreateDirectoryW(dir, nullptr);

        swprintf(path, ARR_COUNT(path), L"%ls\\test_file.mp4", dir);
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        REQUIRE(file != INVALID_HANDLE_VALUE);

        char data[4096] = {};
        DWORD written = 0;

        BOOL ok = WriteFile(file, data, sizeof(data), &written, nullptr);
        REQUIRE(ok);
        REQUIRE(written == ARR_COUNT(data));
        CloseHandle(file);
    }

    ~TempFileFixture() {
        DeleteFileW(path);
        //RemoveDirectoryW(dir);
    }
};

struct AddJobFixture : AppStateFixture, TempFileFixture {};

TEST_CASE_FIXTURE(AddJobFixture, "AddJob reads file size") {
    CAPTURE(appState);
    bool32 ok = AddJob(&appState, path);
    CHECK(ok);
    CHECK(appState.jobCount == 1);
    CHECK(appState.jobs[0].inputFileSize > 0.0f);
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob fails at MAX_JOBS") {
    appState.jobCount = MAX_JOBS;
    bool32 ok = AddJob(&appState, path);
    CHECK(!ok);
    CHECK(appState.jobCount == MAX_JOBS);
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob succeeds at MAX_JOBS - 1") {
    appState.jobCount = MAX_JOBS - 1;
    bool32 ok = AddJob(&appState, path);
    CHECK(ok);
    CHECK(appState.jobCount == MAX_JOBS);
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob increments job list correctly") {
    CHECK(AddJob(&appState, path));
    CHECK(AddJob(&appState, path));
    CHECK(AddJob(&appState, path));

    CHECK(appState.jobCount == 3);

    CHECK(appState.jobs[0].status == JobStatus::QUEUED);
    CHECK(appState.jobs[1].status == JobStatus::QUEUED);
    CHECK(appState.jobs[2].status == JobStatus::QUEUED);
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob rejects input from output folder") {
    char pathUtf8[MAX_PATH_COUNT];
    char dirUtf8[MAX_PATH_COUNT];
    UTF16To8(path, pathUtf8);
    UTF16To8(dir, dirUtf8);
    INFO(pathUtf8);

    snprintf(appState.outputFolder, ARR_COUNT(appState.outputFolder), "%s", dirUtf8);
    INFO(appState.outputFolder);

    bool32 ok = AddJob(&appState, path);
    CHECK(!ok);
    CHECK(appState.jobCount == 0);
}
