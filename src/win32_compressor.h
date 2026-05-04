#pragma once

// Has to be enough, including null terminator
#define MAX_PATH_COUNT 2048

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
    i32 jobCount;
    volatile long workerRunning; // 0/1
    volatile long cancelRequested;
    HANDLE workerThread;

    f32 defaultTargetSize;

    // All UI state with ImGui
    UIState uiState;

    char exeDir[MAX_PATH_COUNT];     // Absolute path to the exe directory
    char ffmpegPath[MAX_PATH_COUNT]; // Relative to working directory OR TODO: path?
};

//struct FFmpegParams {
//    const char* ffmpegPath;
//    const char* input;
//    const char* output;

//    f32 targetSizeMb;
//};
