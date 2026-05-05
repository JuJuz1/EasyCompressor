#pragma once

// Has to be enough, including null terminator
#define MAX_PATH_COUNT 2048

#if COMPRESSOR_DEBUG

static void
DEBUG_PRINT(const char* msg) {
    OutputDebugStringA(msg);
}

static void
DEBUG_PRINTF(const char* fmt, ...) {
    // TODO: for my debug purposes this is enough
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    OutputDebugStringA(buf);
}

#else
#    define DEBUG_PRINT(...)
#    define DEBUG_PRINTF(...)

#endif

enum JobStatus : u8 {
    QUEUED = 0,

    RUNNING_PROBE,
    DONE_PROBE,
    RUNNING_COMPRESS,
    DONE_COMPRESS,
    ERROR
};

struct UIJob {
    char input[MAX_PATH_COUNT];
    char output[MAX_PATH_COUNT + 12]; // "_compressed" suffix by default
    volatile long status;             // JobStatus, written across threads via Interlocked*
    //volatile long progressPct; // 0..100, optional (parse from ffmpeg -stats if you want)
    f32 targetSizeMb;

    f32 inputFileSize;
    f32 durationSeconds; // Probed from the video before compression (2 passes)

    f32 resultFileSize;
};

#define MAX_JOBS 10

// All in MB
#define DEFAULT_TARGET_SIZE 10.0f
#define MIN_TARGET_SIZE 0.5f
#define MAX_TARGET_SIZE 5000.0f

struct UIState {
    bool32 helpAboutClicked;
};

struct AppState {
    UIJob jobs[MAX_JOBS];
    volatile long jobCount;
    // If we were to use condition variables instead of spin lock
    //CONDITION_VARIABLE jobAvailable;
    //CRITICAL_SECTION jobLock;
    volatile long compressing;
    volatile long cancelRequested;
    HANDLE workerThread;

    f32 defaultTargetSize;

    // All UI state with ImGui
    UIState uiState;

    char exeDir[MAX_PATH_COUNT];     // Absolute path to the exe directory
    char ffmpegPath[MAX_PATH_COUNT]; // Relative to working directory OR TODO: path?
};
