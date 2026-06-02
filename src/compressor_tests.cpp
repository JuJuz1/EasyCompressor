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

struct AddJobAppStateFixture {
    AppState appState = {};

    AddJobAppStateFixture() {
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
    ConstructPathsForJob(&appState, &j, tc.input);

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
    INFO("appstate output: ", appState.outputFolder);
    INFO("expected:        ", tc.exp);
    CHECK_EQ(IsPathFromOutputFolder(&appState, tc.input), tc.exp);
}

// Temp files for AddJob
struct TempFileFixture {
    // TODO: test paths greater than MAX_PATH: 260 and current MAX_PATH_COUNT
    wchar dir[MAX_PATH_COUNT];
    wchar path[MAX_PATH_COUNT];
    wchar path2[MAX_PATH_COUNT];
    wchar path3[MAX_PATH_COUNT];

    TempFileFixture() {
        i32 val = GetTempPathW(ARR_COUNT(dir), dir);
        REQUIRE(val != 0);
        dir[val - 1] = '\0'; // Remove trailing slash...
        //CreateDirectoryW(dir, nullptr);

        swprintf(path, ARR_COUNT(path), L"%ls\\test_fff.mp4", dir);
        swprintf(path2, ARR_COUNT(path2), L"%ls\\tb.mp4", dir);
        swprintf(path3, ARR_COUNT(path3), L"%ls\\ывьн.avi", dir);

        CreateTestFile(path);
        CreateTestFile(path2);
        CreateTestFile(path3);
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

    ~TempFileFixture() {
        DeleteFileW(path);
        DeleteFileW(path2);
        DeleteFileW(path3);
    }
};

struct AddJobFixture : AddJobAppStateFixture, TempFileFixture {};

TEST_CASE_FIXTURE(AddJobFixture, "AddJob reads file size correctly") {
    CAPTURE(appState);
    CHECK(AddJob(&appState, path) == AddJobResult::SUCCESS);
    CHECK(appState.jobCount == 1);
    CHECK(appState.jobs[0].inputFileSize == doctest::Approx(4096 / (1024.0f * 1024.0f)));
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob fails at MAX_JOBS") {
    appState.jobCount = MAX_JOBS;
    CHECK(AddJob(&appState, path) == AddJobResult::JOBS_FULL);
    CHECK(appState.jobCount == MAX_JOBS);
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob succeeds at MAX_JOBS - 1") {
    appState.jobCount = MAX_JOBS - 1;
    CHECK(AddJob(&appState, path) == AddJobResult::SUCCESS);
    CHECK(appState.jobCount == MAX_JOBS);
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob increments job list correctly") {
    CHECK(AddJob(&appState, path) == AddJobResult::SUCCESS);
    CHECK(AddJob(&appState, path2) == AddJobResult::SUCCESS);
    CHECK(AddJob(&appState, path3) == AddJobResult::SUCCESS);
    CHECK(appState.jobCount == 3);

    for (i32 i = 0; i < 3; ++i) {
        CHECK(appState.jobs[i].status == JobStatus::QUEUED);
    }
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob rejects duplicate input") {
    CHECK(AddJob(&appState, path) == AddJobResult::SUCCESS);

    CHECK(AddJob(&appState, path) == AddJobResult::DUPLICATE_JOB);
    CHECK(AddJob(&appState, path) == AddJobResult::DUPLICATE_JOB);
    CHECK(appState.jobCount == 1);

    CHECK(AddJob(&appState, path2) == AddJobResult::SUCCESS);
    CHECK(AddJob(&appState, path2) == AddJobResult::DUPLICATE_JOB);
    CHECK(appState.jobCount == 2);
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob rejects input from output folder") {
    char pathUtf8[MAX_PATH_COUNT];
    char dirUtf8[MAX_PATH_COUNT];
    UTF16To8(path, pathUtf8);
    UTF16To8(dir, dirUtf8);
    INFO(pathUtf8);

    snprintf(appState.outputFolder, ARR_COUNT(appState.outputFolder), "%s", dirUtf8);
    INFO(appState.outputFolder);

    CHECK(AddJob(&appState, path) == AddJobResult::JOB_FROM_OUTPUT);
    CHECK(appState.jobCount == 0);
}

TEST_CASE_FIXTURE(AddJobFixture, "MoveJob works correctly") {
    REQUIRE(AddJob(&appState, path) == AddJobResult::SUCCESS);
    REQUIRE(AddJob(&appState, path2) == AddJobResult::SUCCESS);
    REQUIRE(AddJob(&appState, path3) == AddJobResult::SUCCESS);
    REQUIRE(appState.jobCount == 3);

    char pathUtf8[MAX_PATH_COUNT];
    char path2Utf8[MAX_PATH_COUNT];
    char path3Utf8[MAX_PATH_COUNT];
    UTF16To8(path, pathUtf8);
    UTF16To8(path2, path2Utf8);
    UTF16To8(path3, path3Utf8);

    // Normal operations
    CHECK(MoveJob(&appState, 0, 2));
    CHECK(MoveJob(&appState, 2, 0));
    CHECK(MoveJob(&appState, 1, 2));
    CHECK(StrEqual(appState.jobs[1].input, path3Utf8));
    CHECK(StrEqual(appState.jobs[2].input, path2Utf8));

    CHECK(MoveJob(&appState, 2, 0));
    CHECK(StrEqual(appState.jobs[0].input, path2Utf8));
    CHECK(StrEqual(appState.jobs[1].input, pathUtf8));
    CHECK(StrEqual(appState.jobs[2].input, path3Utf8));

    // Invalid
    CHECK(!MoveJob(&appState, 3, 2));
    CHECK(!MoveJob(&appState, -2, 2));
    CHECK(StrEqual(appState.jobs[0].input, path2Utf8));
    CHECK(StrEqual(appState.jobs[1].input, pathUtf8));
    CHECK(StrEqual(appState.jobs[2].input, path3Utf8));

    // Highest running index
    CHECK(!MoveJob(&appState, 0, 2, 1));
    CHECK(StrEqual(appState.jobs[1].input, pathUtf8));
    CHECK(StrEqual(appState.jobs[2].input, path3Utf8));
    CHECK(MoveJob(&appState, 2, 1, 0));
    CHECK(StrEqual(appState.jobs[1].input, path3Utf8));
    CHECK(StrEqual(appState.jobs[2].input, pathUtf8));
    CHECK(!MoveJob(&appState, 1, 2, 2));
    CHECK(StrEqual(appState.jobs[1].input, path3Utf8));
    CHECK(StrEqual(appState.jobs[2].input, pathUtf8));
}

TEST_CASE_FIXTURE(AddJobFixture, "RemoveJob works correctly") {
    REQUIRE(AddJob(&appState, path) == AddJobResult::SUCCESS);
    REQUIRE(AddJob(&appState, path2) == AddJobResult::SUCCESS);
    REQUIRE(AddJob(&appState, path3) == AddJobResult::SUCCESS);
    REQUIRE(appState.jobCount == 3);

    char pathUtf8[MAX_PATH_COUNT];
    char path2Utf8[MAX_PATH_COUNT];
    char path3Utf8[MAX_PATH_COUNT];
    UTF16To8(path, pathUtf8);
    UTF16To8(path2, path2Utf8);
    UTF16To8(path3, path3Utf8);

    CHECK(RemoveJob(&appState, 1));
    CHECK(appState.jobCount == 2);
    CHECK(StrEqual(appState.jobs[0].input, pathUtf8));
    CHECK(StrEqual(appState.jobs[1].input, path3Utf8));

    // Remove from end
    CHECK(RemoveJob(&appState, 1));
    CHECK(appState.jobCount == 1);
    CHECK(StrEqual(appState.jobs[0].input, pathUtf8));

    // Tests adding after removal
    REQUIRE(AddJob(&appState, path3) == AddJobResult::SUCCESS);
    REQUIRE(AddJob(&appState, path2) == AddJobResult::SUCCESS);

    // Invalid
    CHECK(!RemoveJob(&appState, -1));
    CHECK(appState.jobCount == 3);
    CHECK(!RemoveJob(&appState, 4));
    CHECK(appState.jobCount == 3);
    CHECK(!RemoveJob(&appState, 3));
    CHECK(appState.jobCount == 3);
    CHECK(StrEqual(appState.jobs[0].input, pathUtf8));
    CHECK(StrEqual(appState.jobs[1].input, path3Utf8));
    CHECK(StrEqual(appState.jobs[2].input, path2Utf8));

    CHECK(RemoveJob(&appState, 0));
    CHECK(appState.jobCount == 2);
    CHECK(StrEqual(appState.jobs[0].input, path3Utf8));
    CHECK(StrEqual(appState.jobs[1].input, path2Utf8));

    CHECK(RemoveJob(&appState, 1));
    CHECK(appState.jobCount == 1);
    CHECK(StrEqual(appState.jobs[0].input, path3Utf8));
    CHECK(RemoveJob(&appState, 0));
    CHECK(appState.jobCount == 0);

    CHECK(!RemoveJob(&appState, 0));
    CHECK(appState.jobCount == 0);
}

TEST_CASE_FIXTURE(AddJobFixture, "AddJob, RemoveJob and MoveJob work correctly") {
    char pathUtf8[MAX_PATH_COUNT];
    char path2Utf8[MAX_PATH_COUNT];
    char path3Utf8[MAX_PATH_COUNT];
    UTF16To8(path, pathUtf8);
    UTF16To8(path2, path2Utf8);
    UTF16To8(path3, path3Utf8);

    REQUIRE(AddJob(&appState, path) == AddJobResult::SUCCESS);
    REQUIRE(AddJob(&appState, path2) == AddJobResult::SUCCESS);
    REQUIRE(AddJob(&appState, path3) == AddJobResult::SUCCESS);
    REQUIRE(appState.jobCount == 3);
    // [path, path2, path3]

    // Move then remove the result
    CHECK(MoveJob(&appState, 0, 2));
    // [path2, path3, path]
    CHECK(RemoveJob(&appState, 2));
    CHECK(appState.jobCount == 2);
    CHECK(StrEqual(appState.jobs[0].input, path2Utf8));
    CHECK(StrEqual(appState.jobs[1].input, path3Utf8));

    // Re-add the removed job, check it lands at the end
    REQUIRE(AddJob(&appState, path) == AddJobResult::SUCCESS);
    CHECK(appState.jobCount == 3);
    CHECK(StrEqual(appState.jobs[2].input, pathUtf8));
    // [path2, path3, path]

    // Remove from front
    CHECK(RemoveJob(&appState, 0));
    CHECK(appState.jobCount == 2);
    CHECK(StrEqual(appState.jobs[0].input, path3Utf8));
    CHECK(StrEqual(appState.jobs[1].input, pathUtf8));
    // [path3, path]

    // Re-add path2, move it to front
    REQUIRE(AddJob(&appState, path2) == AddJobResult::SUCCESS);
    CHECK(appState.jobCount == 3);
    // [path3, path, path2]
    CHECK(MoveJob(&appState, 2, 0));
    // [path2, path3, path]
    CHECK(StrEqual(appState.jobs[0].input, path2Utf8));
    CHECK(StrEqual(appState.jobs[1].input, path3Utf8));
    CHECK(StrEqual(appState.jobs[2].input, pathUtf8));

    // Remove middle, move remaining
    CHECK(RemoveJob(&appState, 1));
    CHECK(appState.jobCount == 2);
    // [path2, path]
    CHECK(MoveJob(&appState, 1, 0));
    // [path, path2]
    CHECK(StrEqual(appState.jobs[0].input, pathUtf8));
    CHECK(StrEqual(appState.jobs[1].input, path2Utf8));

    // Remove down to empty
    CHECK(RemoveJob(&appState, 0));
    CHECK(RemoveJob(&appState, 0));
    CHECK(appState.jobCount == 0);
    CHECK(!RemoveJob(&appState, 0));
    CHECK(appState.jobCount == 0);

    // Re-add after empty
    REQUIRE(AddJob(&appState, path3) == AddJobResult::SUCCESS);
    CHECK(appState.jobCount == 1);
    CHECK(StrEqual(appState.jobs[0].input, path3Utf8));
}

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
        swprintf(path, ARR_COUNT(path), L"%ls\\easycompressor.cfg", dir);

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

struct AppStateFixture {
    AppState appState = {};

    bool32
    IsZeroed() {
        bool32 targetSizesZeroed = true;
        for (i32 i = 0; i < ARR_COUNT(appState.targetSizes); ++i) {
            if (appState.targetSizes[i] != 0.0f) {
                targetSizesZeroed = false;
                break;
            }
        }

        bool32 defaultTargetSizeZeroed = appState.defaultTargetSize == 0.0f;
        bool32 codecZeroed = appState.defaultCodec == Codec::NONE;
        bool32 outputFolderZeroed = appState.outputFolder[0] == '\0';

        return targetSizesZeroed && defaultTargetSizeZeroed && codecZeroed && outputFolderZeroed;
    }
};

// Here we don't care about the file I/O anymore, we can just use the raw content as test data
TEST_CASE_FIXTURE(AppStateFixture, "ParseConfigBuffer skips comments") {
    struct TC {
        const char* content;
    };

    auto tc = GENERATE(TC{ "# THis is a comment, shouldn't affect anything" },
                       TC{ "# THis isffect anything\n"
                           "#ASDasd"
                           "test characters### dфывьн - --" });
    ParseConfigBuffer(&appState, tc.content);
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
    ParseConfigBuffer(&appState, tc.content);
    CHECK(this->IsZeroed());
}

struct ParseConfigAppStateFixture {
    AppState appState = {};
};

// TODO: this creates folders and deletes them
TEST_CASE_FIXTURE(ParseConfigAppStateFixture, "ParseConfigBuffer works correctly") {
    struct TC {
        const char* content;
        f32 expSizes[ARR_COUNT(appState.targetSizes)];
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
    ParseConfigBuffer(&appState, tc.content);
    INFO(tc.content);
    for (i32 i = 0; i < ARR_COUNT(appState.targetSizes); ++i) {
        CHECK(appState.targetSizes[i] == doctest::Approx(tc.expSizes[i]));
    }

    CHECK(appState.defaultTargetSize == doctest::Approx(tc.expDefaultSize));
    CHECK_EQ(appState.defaultCodec, tc.expCodec);

    INFO(appState.outputFolder);
    INFO(tc.expOutput);
    CHECK(StrEqual(appState.outputFolder, tc.expOutput));

    // Cleanup
    wchar outputW[MAX_PATH_COUNT];
    UTF8To16(tc.expOutput, outputW);
    if (PathFileExistsW(outputW)) {
        RemoveDirectoryW(outputW);
    }
}

/// LoadConfigFile

struct LoadConfigFileFixture {
    AppState appState = {};
    wchar dir[MAX_PATH_COUNT];
    wchar path[MAX_PATH_COUNT];

    LoadConfigFileFixture() {
        i32 val = GetTempPathW(ARR_COUNT(dir), dir);
        REQUIRE(val != 0);
        dir[val - 1] = '\0';
        swprintf(path, ARR_COUNT(path), L"%ls\\easycompressor_test.cfg", dir);
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
        UTF8To16(appState.outputFolder, outputW);
        if (PathFileExistsW(outputW)) {
            RemoveDirectoryW(outputW);
        }
    }
};

// These just test that the fallbacks work correctly
// ParseConfigBuffer does the heavy lifting
TEST_CASE_FIXTURE(LoadConfigFileFixture, "LoadConfigFile fails correctly") {
    CHECK(!LoadConfigFile(nullptr, &appState, L"invalid/path/file.cfg"));

    // No sizes is the only content failure
    this->WriteConfig("[Codecs]\nh264\n");
    CHECK(!LoadConfigFile(nullptr, &appState, path));
    CHECK(appState.defaultCodec == Codec::H264);
    INFO(appState.outputFolder);
    // TODO: we assign to User/Documents/EasyCompressor
    // hard to test so we just check this
    CHECK(appState.outputFolder[0] != '\0');
}

TEST_CASE_FIXTURE(LoadConfigFileFixture, "LoadConfigFile applies fallbacks correctly") {
    this->WriteConfig("[Sizes]\n2.0\n5.0\n");
    REQUIRE(LoadConfigFile(nullptr, &appState, path));
    CHECK(appState.defaultCodec == Codec::H264);
    CHECK(appState.defaultTargetSize == doctest::Approx(2.0f));
    CHECK(appState.outputFolder[0] != '\0');
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
    UIJob* j = &appState->jobs[appState->jobCount];
    *j = {};

    I guess the compiler tries to do some magic but crashes in that attempt. Using ZeroMemory
    manually has fixed the issue every time
    */

    ZeroMemory(&appState, sizeof(appState));
    this->WriteConfig("[Sizes]\n2.0\n5.0 !\n[Codecs]\nh265 !\n[OutputPath]\nC:\\EasyCompTemp");
    REQUIRE(LoadConfigFile(nullptr, &appState, path));
    CHECK(appState.defaultTargetSize == doctest::Approx(5.0f));
    CHECK(appState.defaultCodec == Codec::H265);
    CHECK(StrEqual(appState.outputFolder, "C:\\EasyCompTemp"));
    this->CleanupCreatedOutputFolder();

    //appState = {};
    ZeroMemory(&appState, sizeof(appState));
    this->WriteConfig("[Sizes]\n7.5\n    6.42\n");
    REQUIRE(LoadConfigFile(nullptr, &appState, path));
    CHECK(appState.defaultTargetSize == doctest::Approx(7.5f));
    this->CleanupCreatedOutputFolder();

    //appState = {};
    ZeroMemory(&appState, sizeof(appState));
    this->WriteConfig("[Sizes]\n2.0\n5.0 !\n4.0\n");
    REQUIRE(LoadConfigFile(nullptr, &appState, path));
    CHECK(appState.defaultTargetSize == doctest::Approx(5.0f));
    this->CleanupCreatedOutputFolder();

    //appState = {};
    ZeroMemory(&appState, sizeof(appState));
    this->WriteConfig("[Sizes]\n2.\n[Codecs]");
    REQUIRE(LoadConfigFile(nullptr, &appState, path));
    CHECK(appState.defaultTargetSize == doctest::Approx(2.0f));
    CHECK(appState.defaultCodec == Codec::H264);
    this->CleanupCreatedOutputFolder();
}
