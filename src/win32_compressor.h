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

enum class Codec : u8 {
    NONE = 0,

    H264,
    H265
};

struct UIJob {
    char input[MAX_PATH_COUNT];
    char output[MAX_PATH_COUNT + 12]; // "_compressed" suffix by default

    f32 inputFileSize;
    f32 durationSeconds; // Probed from the video before compression (2 passes)

    f32 targetSizeMb;
    volatile long status;      // JobStatus
    volatile long progressPct; // 0..100 (compression progress only)
    //Codec codec;
    //Preset preset;

    f32 resultFileSize;
};

#define MAX_JOBS 10

// All in MB
#define MIN_TARGET_SIZE 0.5f
#define MAX_TARGET_SIZE 5000.0f

#define SIZES_COUNT 5

// Mainly for popup dialogs
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
    f32 targetSizes[SIZES_COUNT];
    Codec defaultCodec;

    // All UI state with ImGui
    UIState uiState;

    char exeDir[MAX_PATH_COUNT];     // Absolute path to the exe directory
    char ffmpegPath[MAX_PATH_COUNT]; // Relative to working directory OR TODO: path?
};
