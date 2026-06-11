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

// Now that we use a fixed permanent storage, we have to clear manually
static void
AppStateReset(AppState* appState) {
    appState->jobCount = 0;
    for (i32 i = 0; i < ARR_COUNT(appState->jobs); ++i) {
        appState->jobs[i].input[0] = '\0';
        appState->jobs[i].output[0] = '\0';
    }

    appState->defaultCodec = Codec::NONE;
    appState->defaultTargetSize = 0.0f;
    ZeroMemory(appState->targetSizes, sizeof(appState->targetSizes));
    appState->outputFolder[0] = '\0';

    // The functions use the scratch arena so we do this manually here
    ArenaClear(&appState->scratchArena);
}

struct AppStateFixture {
    AppState* appState = nullptr;

    AppStateFixture() {
        static AppState staticAppState = {};
        static bool32 initialized = false;
        if (!initialized) {
            REQUIRE(InitAppState(&staticAppState));
            initialized = true;
        }

        appState = &staticAppState;
    }

    ~AppStateFixture() { AppStateReset(appState); }

    bool32
    IsZeroed() {
        bool32 targetSizesZeroed = true;
        for (i32 i = 0; i < ARR_COUNT(appState->targetSizes); ++i) {
            if (appState->targetSizes[i] != 0.0f) {
                targetSizesZeroed = false;
                break;
            }
        }

        bool32 defaultTargetSizeZeroed = appState->defaultTargetSize == 0.0f;
        bool32 codecZeroed = appState->defaultCodec == Codec::NONE;
        bool32 outputFolderZeroed = appState->outputFolder[0] == '\0';

        return targetSizesZeroed && defaultTargetSizeZeroed && codecZeroed && outputFolderZeroed;
    }
};

struct AddJobAppStateFixture : AppStateFixture {
    AddJobAppStateFixture() {
        // Other stuff?
        REQUIRE(appState->outputFolder != nullptr);
        _snprintf_s(appState->outputFolder, MAX_PATH_COUNT, _TRUNCATE, "C:\\output");
    }
};

TEST_SUITE_BEGIN("String stuff");

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

TEST_SUITE_END();

TEST_CASE_FIXTURE(AddJobAppStateFixture, "ConstructPathsForJob works correctly") {
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
    j.input = appState->jobs[0].input;
    j.output = appState->jobs[0].output;
    j.input[0] = '\0';
    j.output[0] = '\0';
    ConstructPathsForJob(appState, &j, tc.input);

    char inputUtf8[MAX_PATH_COUNT];
    UTF16To8(tc.input, inputUtf8);
    INFO("input:    ", inputUtf8);
    INFO("expected: ", tc.exp);
    INFO("actual:   ", j.output);
    CHECK(StrEqual(j.output, tc.exp));
}

TEST_CASE_FIXTURE(AddJobAppStateFixture, "IsPathFromOutputFolder works correctly") {
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
    INFO("input:           ", inputUtf8);
    INFO("appstate output: ", appState->outputFolder);
    INFO("expected:        ", tc.exp);
    CHECK_EQ(IsPathFromOutputFolder(appState, tc.input), tc.exp);
}

// Temp files for AddJob
struct TempFilesFixture {
    // TODO: test paths greater than MAX_PATH: 260 and current MAX_PATH_COUNT
    wchar dir[MAX_PATH_COUNT];
    wchar path[MAX_PATH_COUNT];
    wchar path2[MAX_PATH_COUNT];
    wchar path3[MAX_PATH_COUNT];

    TempFilesFixture() {
        i32 val = GetTempPathW(ARR_COUNT(dir), dir);
        REQUIRE(val != 0);
        dir[val - 1] = '\0'; // Remove trailing slash...
        //CreateDirectoryW(dir, nullptr);

        _snwprintf_s(path, ARR_COUNT(path), L"%ls\\test_fff.mp4", dir);
        _snwprintf_s(path2, ARR_COUNT(path2), L"%ls\\tb.mp4", dir);
        _snwprintf_s(path3, ARR_COUNT(path3), L"%ls\\ывьн.avi", dir);

        CreateTestFile(path);
        CreateTestFile(path2);
        CreateTestFile(path3);
    }

    ~TempFilesFixture() {
        DeleteFileW(path);
        DeleteFileW(path2);
        DeleteFileW(path3);
    }

    void
    CreateTestFile(const wchar* pathW) {
        HANDLE file = CreateFileW(pathW, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        REQUIRE(file != INVALID_HANDLE_VALUE);

        char data[4096] = "Test data\n :D";
        DWORD written = 0;
        BOOL ok = WriteFile(file, data, sizeof(data), &written, nullptr);
        REQUIRE(ok);
        REQUIRE(written == ARR_COUNT(data));

        CloseHandle(file);
    }
};

TEST_SUITE_BEGIN("AddJob");

struct AddJobFixture : AddJobAppStateFixture, TempFilesFixture {};

TEST_CASE_FIXTURE(AddJobFixture, "AddJob reads file size correctly") {
    CAPTURE(appState);
    CHECK(AddJob(appState, path) == AddJobResult::SUCCESS);
    CHECK(appState->jobCount == 1);
    CHECK(appState->jobs[0].inputFileSize == doctest::Approx(4096 / (1024.0f * 1024.0f)));
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob fails at MAX_JOBS") {
    appState->jobCount = MAX_JOBS;
    CHECK(AddJob(appState, path) == AddJobResult::JOBS_FULL);
    CHECK(appState->jobCount == MAX_JOBS);
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob succeeds at MAX_JOBS - 1") {
    appState->jobCount = MAX_JOBS - 1;
    CHECK(AddJob(appState, path) == AddJobResult::SUCCESS);
    CHECK(appState->jobCount == MAX_JOBS);
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob increments job list correctly") {
    CHECK(AddJob(appState, path) == AddJobResult::SUCCESS);
    CHECK(AddJob(appState, path2) == AddJobResult::SUCCESS);
    CHECK(AddJob(appState, path3) == AddJobResult::SUCCESS);
    CHECK(appState->jobCount == 3);

    for (i32 i = 0; i < 3; ++i) {
        CHECK(appState->jobs[i].status == JobStatus::QUEUED);
    }
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob rejects duplicate input") {
    CHECK(AddJob(appState, path) == AddJobResult::SUCCESS);

    CHECK(AddJob(appState, path) == AddJobResult::DUPLICATE_JOB);
    CHECK(AddJob(appState, path) == AddJobResult::DUPLICATE_JOB);
    CHECK(appState->jobCount == 1);

    CHECK(AddJob(appState, path2) == AddJobResult::SUCCESS);
    CHECK(AddJob(appState, path2) == AddJobResult::DUPLICATE_JOB);
    CHECK(appState->jobCount == 2);
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob rejects input from output folder") {
    char pathUtf8[MAX_PATH_COUNT];
    char dirUtf8[MAX_PATH_COUNT];
    UTF16To8(path, pathUtf8);
    UTF16To8(dir, dirUtf8);
    INFO(pathUtf8);

    snprintf(appState->outputFolder, MAX_PATH_COUNT, "%s", dirUtf8);
    INFO(appState->outputFolder);

    CHECK(AddJob(appState, path) == AddJobResult::JOB_FROM_OUTPUT);
    CHECK(appState->jobCount == 0);
}

TEST_SUITE_END();

TEST_CASE_FIXTURE(AddJobFixture, "MoveJob works correctly") {
    REQUIRE(AddJob(appState, path) == AddJobResult::SUCCESS);
    REQUIRE(AddJob(appState, path2) == AddJobResult::SUCCESS);
    REQUIRE(AddJob(appState, path3) == AddJobResult::SUCCESS);
    REQUIRE(appState->jobCount == 3);

    char pathUtf8[MAX_PATH_COUNT];
    char path2Utf8[MAX_PATH_COUNT];
    char path3Utf8[MAX_PATH_COUNT];
    UTF16To8(path, pathUtf8);
    UTF16To8(path2, path2Utf8);
    UTF16To8(path3, path3Utf8);

    // Normal operations
    CHECK(MoveJob(appState, 0, 2));
    CHECK(MoveJob(appState, 2, 0));
    CHECK(MoveJob(appState, 1, 2));
    CHECK(StrEqual(appState->jobs[1].input, path3Utf8));
    CHECK(StrEqual(appState->jobs[2].input, path2Utf8));

    CHECK(MoveJob(appState, 2, 0));
    CHECK(StrEqual(appState->jobs[0].input, path2Utf8));
    CHECK(StrEqual(appState->jobs[1].input, pathUtf8));
    CHECK(StrEqual(appState->jobs[2].input, path3Utf8));

    // Invalid
    CHECK(!MoveJob(appState, 3, 2));
    CHECK(!MoveJob(appState, -2, 2));
    CHECK(StrEqual(appState->jobs[0].input, path2Utf8));
    CHECK(StrEqual(appState->jobs[1].input, pathUtf8));
    CHECK(StrEqual(appState->jobs[2].input, path3Utf8));

    // Highest running index
    CHECK(!MoveJob(appState, 0, 2, 1));
    CHECK(StrEqual(appState->jobs[1].input, pathUtf8));
    CHECK(StrEqual(appState->jobs[2].input, path3Utf8));
    CHECK(MoveJob(appState, 2, 1, 0));
    CHECK(StrEqual(appState->jobs[1].input, path3Utf8));
    CHECK(StrEqual(appState->jobs[2].input, pathUtf8));
    CHECK(!MoveJob(appState, 1, 2, 2));
    CHECK(StrEqual(appState->jobs[1].input, path3Utf8));
    CHECK(StrEqual(appState->jobs[2].input, pathUtf8));
}

TEST_CASE_FIXTURE(AddJobFixture, "RemoveJob works correctly") {
    REQUIRE(AddJob(appState, path) == AddJobResult::SUCCESS);
    REQUIRE(AddJob(appState, path2) == AddJobResult::SUCCESS);
    REQUIRE(AddJob(appState, path3) == AddJobResult::SUCCESS);
    REQUIRE(appState->jobCount == 3);

    char pathUtf8[MAX_PATH_COUNT];
    char path2Utf8[MAX_PATH_COUNT];
    char path3Utf8[MAX_PATH_COUNT];
    UTF16To8(path, pathUtf8);
    UTF16To8(path2, path2Utf8);
    UTF16To8(path3, path3Utf8);

    CHECK(RemoveJob(appState, 1));
    CHECK(appState->jobCount == 2);
    CHECK(StrEqual(appState->jobs[0].input, pathUtf8));
    CHECK(StrEqual(appState->jobs[1].input, path3Utf8));

    // Remove from end
    CHECK(RemoveJob(appState, 1));
    CHECK(appState->jobCount == 1);
    CHECK(StrEqual(appState->jobs[0].input, pathUtf8));

    // Tests adding after removal
    REQUIRE(AddJob(appState, path3) == AddJobResult::SUCCESS);
    REQUIRE(AddJob(appState, path2) == AddJobResult::SUCCESS);

    // Invalid
    CHECK(!RemoveJob(appState, -1));
    CHECK(appState->jobCount == 3);
    CHECK(!RemoveJob(appState, 4));
    CHECK(appState->jobCount == 3);
    CHECK(!RemoveJob(appState, 3));
    CHECK(appState->jobCount == 3);
    CHECK(StrEqual(appState->jobs[0].input, pathUtf8));
    CHECK(StrEqual(appState->jobs[1].input, path3Utf8));
    CHECK(StrEqual(appState->jobs[2].input, path2Utf8));

    CHECK(RemoveJob(appState, 0));
    CHECK(appState->jobCount == 2);
    CHECK(StrEqual(appState->jobs[0].input, path3Utf8));
    CHECK(StrEqual(appState->jobs[1].input, path2Utf8));

    CHECK(RemoveJob(appState, 1));
    CHECK(appState->jobCount == 1);
    CHECK(StrEqual(appState->jobs[0].input, path3Utf8));
    CHECK(RemoveJob(appState, 0));
    CHECK(appState->jobCount == 0);

    CHECK(!RemoveJob(appState, 0));
    CHECK(appState->jobCount == 0);
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob, RemoveJob and MoveJob work correctly") {
    char pathUtf8[MAX_PATH_COUNT];
    char path2Utf8[MAX_PATH_COUNT];
    char path3Utf8[MAX_PATH_COUNT];
    UTF16To8(path, pathUtf8);
    UTF16To8(path2, path2Utf8);
    UTF16To8(path3, path3Utf8);

    REQUIRE(AddJob(appState, path) == AddJobResult::SUCCESS);
    REQUIRE(AddJob(appState, path2) == AddJobResult::SUCCESS);
    REQUIRE(AddJob(appState, path3) == AddJobResult::SUCCESS);
    REQUIRE(appState->jobCount == 3);
    // [path, path2, path3]

    // Move then remove the result
    CHECK(MoveJob(appState, 0, 2));
    // [path2, path3, path]
    CHECK(RemoveJob(appState, 2));
    CHECK(appState->jobCount == 2);
    CHECK(StrEqual(appState->jobs[0].input, path2Utf8));
    CHECK(StrEqual(appState->jobs[1].input, path3Utf8));

    // Re-add the removed job, check it lands at the end
    REQUIRE(AddJob(appState, path) == AddJobResult::SUCCESS);
    CHECK(appState->jobCount == 3);
    CHECK(StrEqual(appState->jobs[2].input, pathUtf8));
    // [path2, path3, path]

    // Remove from front
    CHECK(RemoveJob(appState, 0));
    CHECK(appState->jobCount == 2);
    CHECK(StrEqual(appState->jobs[0].input, path3Utf8));
    CHECK(StrEqual(appState->jobs[1].input, pathUtf8));
    // [path3, path]

    // Re-add path2, move it to front
    REQUIRE(AddJob(appState, path2) == AddJobResult::SUCCESS);
    CHECK(appState->jobCount == 3);
    // [path3, path, path2]
    CHECK(MoveJob(appState, 2, 0));
    // [path2, path3, path]
    CHECK(StrEqual(appState->jobs[0].input, path2Utf8));
    CHECK(StrEqual(appState->jobs[1].input, path3Utf8));
    CHECK(StrEqual(appState->jobs[2].input, pathUtf8));

    // Remove middle, move remaining
    CHECK(RemoveJob(appState, 1));
    CHECK(appState->jobCount == 2);
    // [path2, path]
    CHECK(MoveJob(appState, 1, 0));
    // [path, path2]
    CHECK(StrEqual(appState->jobs[0].input, pathUtf8));
    CHECK(StrEqual(appState->jobs[1].input, path2Utf8));

    // Remove down to empty
    CHECK(RemoveJob(appState, 0));
    CHECK(RemoveJob(appState, 0));
    CHECK(appState->jobCount == 0);
    CHECK(!RemoveJob(appState, 0));
    CHECK(appState->jobCount == 0);

    // Re-add after empty
    REQUIRE(AddJob(appState, path3) == AddJobResult::SUCCESS);
    CHECK(appState->jobCount == 1);
    CHECK(StrEqual(appState->jobs[0].input, path3Utf8));
}

TEST_SUITE_BEGIN("Adding files");

TEST_CASE_FIXTURE(AddJobFixture, "HandleOpenFileNameBuffer handles single file") {
    wchar buff[MAX_PATH_COUNT];
    _snwprintf_s(buff, ARR_COUNT(buff), L"%ls", this->path);

    // Don't care what this ends up with
    AddJobResult lastError;
    // fileOffset points to the filename after the last backslash
    UINT fileOffset = static_cast<UINT>((StrLengthW(this->dir) + 1)); // dir + '\' + fil)ename
    i32 rejected = HandleOpenFileNameBuffer(appState, buff, fileOffset, &lastError);
    CHECK(rejected == 0);
    CHECK(appState->jobCount == 1);
}

TEST_CASE_FIXTURE(AddJobFixture, "HandleOpenFileNameBuffer handles multiple files") {
    wchar buff[MAX_PATH_COUNT * 3] = {};
    // "C:\dir\0test_fff.mp4\0tb.mp4\0ывьн.avi\0\0"
    UINT dirLen = static_cast<UINT>(StrLengthW(this->dir));
    // This looks very ugly and errorprone
    UINT off = 0;
    wmemcpy(buff + off, this->dir, dirLen + 1);
    off += dirLen + 1;
    // IMPORTANT: these must match AddJobFixture path, path2 and path3 file names
    wmemcpy(buff + off, L"test_fff.mp4", wcslen(L"test_fff.mp4") + 1);
    off += static_cast<UINT>(wcslen(L"test_fff.mp4")) + 1;
    wmemcpy(buff + off, L"tb.mp4", wcslen(L"tb.mp4") + 1);
    off += static_cast<UINT>(wcslen(L"tb.mp4")) + 1;
    wmemcpy(buff + off, L"ывьн.avi", wcslen(L"ывьн.avi") + 1);

    AddJobResult lastError;
    UINT fileOffset = dirLen + 1;
    i32 rejected = HandleOpenFileNameBuffer(appState, buff, fileOffset, &lastError);
    CHECK(rejected == 0);
    CHECK(appState->jobCount == 3);
}

TEST_CASE_FIXTURE(AddJobFixture, "HandleOpenFileNameBuffer rejects truncated path") {
    // dir + \ + file must exceed MAX_PATH_COUNT
    // dir = MAX_PATH_COUNT - 5 chars, file = "f.mp4" (5 chars)
    // total = (MAX_PATH_COUNT - 5) + 1 + 5 = MAX_PATH_COUNT + 1 -> truncated
    wchar buff[MAX_PATH_COUNT * 2] = {};

    UINT dirLen = MAX_PATH_COUNT - 5;
    for (UINT i = 0; i < dirLen; ++i) {
        buff[i] = L'a';
    }

    buff[dirLen] = L'\0';

    const wchar* filename = L"f.mp4";
    UINT fileLen = static_cast<UINT>(wcslen(filename));
    wmemcpy(buff + dirLen + 1, filename, fileLen + 1);

    AddJobResult lastError;
    UINT fileOffset = dirLen + 1;
    i32 rejected = HandleOpenFileNameBuffer(appState, buff, fileOffset, &lastError);
    CHECK(rejected == 1);
    CHECK(appState->jobCount == 0);
}

TEST_SUITE_END();

/// -----------------------------------------------------------------------------
/// Config file stuff
/// -----------------------------------------------------------------------------

struct TempConfigFileFixture {
    wchar dir[MAX_PATH_COUNT];
    wchar path[MAX_PATH_COUNT];

    TempConfigFileFixture() {
        // TODO: duplicate code, same as above
        i32 val = GetTempPathW(ARR_COUNT(dir), dir);
        REQUIRE(val != 0);
        dir[val - 1] = '\0';
        _snwprintf_s(path, ARR_COUNT(path), L"%ls\\easycompressor.cfg", dir);

        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        REQUIRE(file != INVALID_HANDLE_VALUE);

        char data[2048] = { "Another test data..." };
        DWORD written = 0;

        BOOL ok = WriteFile(file, data, sizeof(data), &written, nullptr);
        REQUIRE(ok);
        REQUIRE(written == ARR_COUNT(data));
        CloseHandle(file);
    }

    ~TempConfigFileFixture() { DeleteFileW(path); }
};

TEST_SUITE_BEGIN("Parsing config");

// This tests file I/O as well
TEST_CASE_FIXTURE(TempConfigFileFixture, "ReadConfigFile works correctly") {
    struct TC {
        const wchar* path;
        bool32 exp;
    };

    char buff[CONFIG_FILE_MAX_SIZE];
    auto tc = GENERATE(TC{ L"invalid file path", false }, TC{ path, true }, TC{ L"", false });

    bool32 ok = ReadConfigFile(tc.path, buff, ARR_COUNT(buff));
    CHECK_EQ(ok, tc.exp);
}

// Here we don't care about the file I/O anymore, we can just use the raw content as test data
TEST_CASE_FIXTURE(AppStateFixture, "ParseConfigBuffer skips comments") {
    struct TC {
        const char* content;
    };

    auto tc = GENERATE(TC{ "# THis is a comment, shouldn't affect anything" },
                       TC{ "# THis isffect anything\n"
                           "#ASDasd"
                           "test characters### dфывьн - --" });
    ParseConfigBuffer(appState, tc.content);
    CHECK(this->IsZeroed());
}

TEST_CASE_FIXTURE(AppStateFixture, "ParseConfigBuffer skips garbage input") {
    struct TC {
        const char* content;
    };

    auto tc = GENERATE(TC{ "# " },
                       TC{ ""
                           "ьн - --" },
                       TC{ "" }, TC{ "    " }, TC{ "\n" }, TC{ "\r\r\n" },
                       TC{ " #comment only junk\n !@ #$ % ^&*() _ + "
                           "# valid comment\nrandomtextwithoutsections"
                           "### comment\n1234567890"
                           "#comment\n     !!!! #### ????"
                           "" });
    ParseConfigBuffer(appState, tc.content);
    CHECK(this->IsZeroed());
}

// TODO: this creates folders and deletes them
TEST_CASE_FIXTURE(AppStateFixture, "ParseConfigBuffer works correctly") {
    struct TC {
        const char* content;
        f32 expSizes[ARR_COUNT(appState->targetSizes)];
        Codec expCodec;
        const char* expOutput;
        f32 expDefaultSize;
    };

    auto tc = GENERATE(TC{ "   \n   "
                           "# comment\n[Sizes]\n1.0"
                           "# comment\n[Codecs]\nh264"
                           "# junk\nrandomtext"
                           "[OutputPath]\nC:\\EasyCompTemp",
                           { 1.0f },
                           Codec::NONE,
                           "" },
                       TC{ "# comment\n[Sizes]\n1.0\n5\n4.9 !"
                           "# comment\n[Codecs]\nh264"
                           "# junk\nra{},{},\n"
                           "[OutputPath]\nC:\\EasyCompTemp",
                           { 1.0f, 5.0f, 4.9f },
                           Codec::NONE,
                           "C:\\EasyCompTemp",
                           4.9f },
                       TC{ "[Sizes]\n#ignored\n1.0\n2.0"
                           "тест\n# comment",
                           { 1.0f, 2.0f },
                           Codec::NONE,
                           "" },
                       TC{ "\n\n\n\r\n[Sizes]\n1.055 !\n5\n4.9 !\n4.9 !\n4.5 !\n4.22 !"
                           "# comment\n[Codecs]\nh264"
                           "# junk\nra{},{},\n"
                           "[OutputPath]\nC:\\test_folder__",
                           { 1.055f, 5.0f, 4.9f, 4.9f, 4.5f },
                           Codec::NONE,
                           "C:\\test_folder__",
                           1.055f },
                       TC{ "\n\n\n\r\n[Sizes]\n2.0 \n5\n50.2 !\n4.9 !\n4.5 !\n4.22 !"
                           "\n\n\n[Codecs]\n\n\nh264 !\nh265"
                           "# junk\nra{},{},\n",
                           { 2.0f, 5.0f, 50.2f, 4.9f, 4.5f },
                           Codec::H264,
                           "",
                           50.2f },
                       TC{ "\\\n[Sizes]\n2.0s2 \n5\n51.2 !\n4000.9 !\n4.5 !\n4.22 !"
                           "# comment\n[Codecs]\nh264\nh265 !\nhINVALID",
                           { 2.0f, 5.0f, 51.2f, 4000.9f, 4.5f },
                           Codec::H265,
                           "",
                           51.2f },
                       // TODO: currently ParseConfigBuffer doesn't set the default target sizes
                       // It's done at the end of LoadConfig
                       TC{ "\\\n[Sizes]"
                           "# comment\n[Codecs]\nhnhINVALID",
                           {},
                           Codec::NONE,
                           "" });

    // TODO: this creates the folder for output path
    ParseConfigBuffer(appState, tc.content);
    INFO(tc.content);
    for (i32 i = 0; i < ARR_COUNT(appState->targetSizes); ++i) {
        CHECK(appState->targetSizes[i] == doctest::Approx(tc.expSizes[i]));
    }

    CHECK(appState->defaultTargetSize == doctest::Approx(tc.expDefaultSize));
    CHECK_EQ(appState->defaultCodec, tc.expCodec);

    INFO(appState->outputFolder);
    INFO(tc.expOutput);
    CHECK(StrEqual(appState->outputFolder, tc.expOutput));

    // Cleanup
    wchar outputW[MAX_PATH_COUNT];
    UTF8To16(tc.expOutput, outputW);
    if (PathFileExistsW(outputW)) {
        RemoveDirectoryW(outputW);
    }
}

TEST_SUITE_END();

/// LoadConfigFile

struct LoadConfigFileFixture : AppStateFixture {
    wchar dir[MAX_PATH_COUNT];
    wchar path[MAX_PATH_COUNT];

    LoadConfigFileFixture() {
        i32 val = GetTempPathW(ARR_COUNT(dir), dir);
        REQUIRE(val != 0);
        dir[val - 1] = '\0';
        _snwprintf_s(path, ARR_COUNT(path), L"%ls\\easycompressor_test.cfg", dir);
    }

    ~LoadConfigFileFixture() { DeleteFileW(path); }

    void
    WriteConfig(const char* content) {
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        REQUIRE(file != INVALID_HANDLE_VALUE);
        DWORD written = 0;
        BOOL ok = WriteFile(file, content, StrLength(content), &written, nullptr);
        REQUIRE(ok);
        REQUIRE(written == StrLength(content));
        CloseHandle(file);
    }

    void
    CleanupCreatedOutputFolder() {
        wchar outputW[MAX_PATH_COUNT];
        UTF8To16(appState->outputFolder, outputW);
        if (PathFileExistsW(outputW)) {
            RemoveDirectoryW(outputW);
        }
    }
};

TEST_SUITE_BEGIN("Loading config");

// These just test that the fallbacks work correctly
// ParseConfigBuffer does the heavy lifting
TEST_CASE_FIXTURE(LoadConfigFileFixture, "LoadConfigFile fails correctly") {
    CHECK(!LoadConfigFile(nullptr, appState, L"invalid/path/file.cfg"));

    // No sizes is the only content failure
    this->WriteConfig("[Codecs]\nh264\n");
    CHECK(!LoadConfigFile(nullptr, appState, path));
    CHECK(appState->defaultCodec == Codec::H264);
    INFO(appState->outputFolder);
    // TODO: we assign to User/Documents/EasyCompressor
    // hard to test so we just check this
    CHECK(appState->outputFolder[0] != '\0');
}

TEST_CASE_FIXTURE(LoadConfigFileFixture, "LoadConfigFile applies fallbacks correctly") {
    this->WriteConfig("[Sizes]\n2.0\n5.0\n");
    REQUIRE(LoadConfigFile(nullptr, appState, path));
    CHECK(appState->defaultCodec == Codec::H264);
    CHECK(appState->defaultTargetSize == doctest::Approx(2.0f));
    CHECK(appState->outputFolder[0] != '\0');
    this->CleanupCreatedOutputFolder();

    //appState = {};

    /*
    fatal error C1001: Internal compiler error.
    (compiler file 'D:\a\_work\1\s\src\vctools\Compiler\Utc\src\p2\main.cpp', line 263)
    To work around this problem, try simplifying or changing the program near the locations listed
    above. If possible please provide a repro here: https://developercommunity.visualstudio.com
    Please choose the Technical Support command on the Visual C++ Help menu, or open the Technical
    Support help file for more information
    cl!RaiseException()+0x8a
    cl!RaiseException()+0x8a
    cl!CloseTypeServerPDB()+0x1ec3c3
    cl!CloseTypeServerPDB()+0x23c232
    cl!DllGetObjHandler()+0xb3f8b
    cl!DllGetObjHandler()+0x180fbf

    Already had this happen before inside AddJob:
    UIJob* j = appState->jobs[appState->jobCount];
    *j = {};

    I guess the compiler tries to do some magic but crashes in that attempt. Using ZeroMemory
    manually has fixed the issue every time
    */

    //appState = {};
    //ZeroMemory(appState, sizeof(appState));
    // Manual reset as now we use arenas with fixed permanent storage
    AppStateReset(appState);
    this->WriteConfig("[Sizes]\n2.0\n5.0 !\n[Codecs]\nh265 !\n[OutputPath]\nC:\\EasyCompTemp");
    REQUIRE(LoadConfigFile(nullptr, appState, path));
    CHECK(appState->defaultTargetSize == doctest::Approx(5.0f));
    CHECK(appState->defaultCodec == Codec::H265);
    CHECK(StrEqual(appState->outputFolder, "C:\\EasyCompTemp"));
    this->CleanupCreatedOutputFolder();

    AppStateReset(appState);
    this->WriteConfig("[Sizes]\n7.5\n    6.42\n");
    REQUIRE(LoadConfigFile(nullptr, appState, path));
    CHECK(appState->defaultTargetSize == doctest::Approx(7.5f));
    this->CleanupCreatedOutputFolder();

    AppStateReset(appState);
    this->WriteConfig("[Sizes]\n2.0\n5.0 !\n4.0\n");
    REQUIRE(LoadConfigFile(nullptr, appState, path));
    CHECK(appState->defaultTargetSize == doctest::Approx(5.0f));
    this->CleanupCreatedOutputFolder();

    AppStateReset(appState);
    this->WriteConfig("[Sizes]\n2.\n[Codecs]");
    REQUIRE(LoadConfigFile(nullptr, appState, path));
    CHECK(appState->defaultTargetSize == doctest::Approx(2.0f));
    CHECK(appState->defaultCodec == Codec::H264);
    this->CleanupCreatedOutputFolder();
}

TEST_SUITE_END();

/// -----------------------------------------------------------------------------
/// Arena
/// -----------------------------------------------------------------------------

TEST_SUITE_BEGIN("Arena");

struct ArenaFixture {
    static constexpr u64 arenaSize = 1024;
    u8 buff[arenaSize];
    Arena arena;

    ArenaFixture() {
        ZeroMemory(buff, sizeof(buff));
        ArenaInit(&arena, buff, arenaSize);
    }
};

struct TestStruct {
    i32 a;
    float b;
    u64 c;
};

TEST_CASE_FIXTURE(ArenaFixture, "ArenaInit sets fields correctly") {
    CHECK(arena.base == buff);
    CHECK(arena.used == 0);
    CHECK(arena.size == arenaSize);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaPush returns pointer to base on first alloc") {
    u8* p = PushArray(&arena, u8, 8);
    CHECK(p == buff);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaPush advances used by requested size") {
    PushArray(&arena, u8, 16);
    CHECK(arena.used == 16);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaPush returns non-overlapping pointers") {
    u8* p1 = PushArray(&arena, u8, 32);
    u8* p2 = PushArray(&arena, u8, 32);
    CHECK(p2 == p1 + 32);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaPush of size 1 works") {
    u8* p = PushArray(&arena, u8, 1);
    CHECK(p != nullptr);
    CHECK(arena.used == 1);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaPush fills exactly to capacity") {
    u8* p = PushArray(&arena, u8, arenaSize);
    CHECK(p == buff);
    CHECK(arena.used == arenaSize);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaPush multiple times up to capacity") {
    u64 chunk = 64;
    u64 count = arenaSize / chunk;
    for (u64 i = 0; i < count; ++i) {
        u8* p = PushArray(&arena, u8, chunk);
        CHECK(p == buff + i * chunk);
    }

    CHECK(arena.used == arenaSize);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaPushZero returns zeroed memory") {
    memset(buff, 0xCD, arenaSize);
    for (i32 i = 0; i < arenaSize; ++i) {
        REQUIRE(buff[i] == 0xCD);
    }

    u8* p = PushArrayZero(&arena, u8, 32);
    for (i32 i = 0; i < 32; ++i) {
        CHECK(p[i] == 0);
    }

    for (i32 i = 32; i < arenaSize; ++i) {
        CHECK(p[i] == 0xCD);
    }
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaPushZero advances used correctly") {
    PushArrayZero(&arena, u8, 64);
    CHECK(arena.used == 64);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaPop decreases used") {
    PushArray(&arena, u8, 64);
    ArenaPop(&arena, 32);
    CHECK(arena.used == 32);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaPop of full push returns to zero") {
    PushArray(&arena, u8, 64);
    ArenaPop(&arena, 64);
    CHECK(arena.used == 0);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaPop then push reuses memory") {
    u8* p1 = PushArray(&arena, u8, 64);
    ArenaPop(&arena, 64);
    u8* p2 = PushArray(&arena, u8, 64);
    CHECK(p1 == p2);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaClear resets used to zero") {
    PushArray(&arena, u8, 128);
    PushArray(&arena, u8, 64);
    ArenaClear(&arena);
    CHECK(arena.used == 0);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaClear allows full reuse of bufffer") {
    PushArray(&arena, u8, arenaSize);
    ArenaClear(&arena);
    u8* p = PushArray(&arena, u8, arenaSize);
    CHECK(p == buff);
    CHECK(arena.used == arenaSize);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaGetPos returns current used") {
    PushArray(&arena, u8, 48);
    CHECK(ArenaGetPos(&arena) == 48);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaSetPos restores position") {
    PushArray(&arena, u8, 128);
    u64 mark = ArenaGetPos(&arena);
    PushArray(&arena, u8, 256);
    ArenaSetPos(&arena, mark);
    CHECK(arena.used == 128);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaSetPos then push reuses memory from that point") {
    PushArray(&arena, u8, 128);
    u64 mark = ArenaGetPos(&arena);
    PushArray(&arena, u8, 64);
    ArenaSetPos(&arena, mark);
    u8* p = PushArray(&arena, u8, 64);
    CHECK(p == buff + 128);
}

TEST_CASE_FIXTURE(ArenaFixture, "ArenaSetPos to zero allows full reuse") {
    PushArray(&arena, u8, arenaSize / 2);
    ArenaSetPos(&arena, 0);
    CHECK(arena.used == 0);
    u8* p = PushArray(&arena, u8, arenaSize);
    CHECK(p == buff);
}

TEST_CASE_FIXTURE(ArenaFixture, "PushArray returns typed pointer and correct size") {
    i32* arr = PushArray(&arena, i32, 4);
    CHECK(arr != nullptr);
    CHECK(arena.used == sizeof(i32) * 4);
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    arr[3] = 40;
    CHECK(arr[0] == 10);
    CHECK(arr[3] == 40);
}

TEST_CASE_FIXTURE(ArenaFixture, "PushStruct returns pointer of correct size") {
    TestStruct* s = PushStruct(&arena, TestStruct);
    CHECK(s != nullptr);
    CHECK(arena.used == sizeof(TestStruct));
    s->a = 1;
    s->b = 2.0f;
    s->c = 3;
    CHECK(s->a == 1);
    CHECK(s->b == doctest::Approx(2.0f));
    CHECK(s->c == 3);
}

TEST_CASE_FIXTURE(ArenaFixture, "PushStructZero returns zeroed struct") {
    memset(buff, 0xFF, arenaSize);

    TestStruct* s = PushStructZero(&arena, TestStruct);
    CHECK(s->a == 0);
    CHECK(s->b == doctest::Approx(0.0f));
    CHECK(s->c == 0);
}

TEST_CASE_FIXTURE(ArenaFixture, "Scratch pattern: push pop leaves arena unchanged") {
    PushArray(&arena, u8, 32);
    u64 usedBefore = arena.used;

    char* tmp = PushArray(&arena, char, 64);
    _snprintf_s(tmp, 64, _TRUNCATE, "temporary string");
    ArenaPop(&arena, 64);

    CHECK(arena.used == usedBefore);
}

TEST_CASE_FIXTURE(ArenaFixture, "Scratch pattern: save restore via GetPos SetPos") {
    PushArray(&arena, u8, 32);
    u64 mark = ArenaGetPos(&arena);

    PushArray(&arena, char, 128);
    PushArray(&arena, i32, 16);

    ArenaSetPos(&arena, mark);
    CHECK(arena.used == 32);
}

TEST_CASE_FIXTURE(ArenaFixture, "Scratch pattern: multiple scratch allocs then clear") {
    for (i32 frame = 0; frame < 8; ++frame) {
        PushArray(&arena, char, 64);
        PushArray(&arena, TestStruct, 4);
        PushArray(&arena, i32, 16);
        ArenaClear(&arena);
        CHECK(arena.used == 0);
    }
}

TEST_SUITE_END();
