#pragma once

enum JobStatus : u8 {
    ERROR = 0,

    QUEUED,
    RUNNING_PROBE,
    DONE_PROBE,
    RUNNING_COMPRESS,
    DONE_COMPRESS,
    CANCELLED
};

enum class Codec : u8 {
    NONE = 0,

    H264,
    H265
};

enum class AddJobResult : u8 {
    JOBS_FULL = 0,
    DUPLICATE_JOB,
    JOB_FROM_OUTPUT,

    SUCCESS
};

// Has to be enough, including null terminator
// MAX_PATH is 260 defined by Windows
// TODO: if we want to support wide paths (32 767 characters), would need to allocate heap memory
// https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation?tabs=registry
#define MAX_PATH_COUNT 32767 // 512

#define OPEN_FLASH_TIMER_START 5.0f

#define MAX_JOBS 50

// All in MB
#define MIN_TARGET_SIZE 0.5f
#define MAX_TARGET_SIZE 5000.0f

#define TARGET_SIZES_COUNT 5

#define CONFIG_FILE_MAX_SIZE (1024 + MAX_PATH_COUNT)

struct UIJob {
    char* input;
    char* output; // No suffix anymore
    //bool32 hasValidFileExtension; Some file extension is required by ffmpeg pass 2
    // We pass "-f mp4" as default if this is false

    f32 inputFileSize;
    f32 durationSeconds; // Probed from the video before compression (2 passes)

    f32 targetSizeMb;
    volatile long status;      // JobStatus
    volatile long progressPct; // 0..100 (compression progress only as probing is fast)
    //Codec codec;
    //Preset preset; different quality vs a target size, don't think this is needed

    f32 resultFileSize;

    f32 displayProgress; // UI progress bar

    f32 openFlashTimer; // Open button flash
};

// Mainly for popup dialogs
struct UIState {
    char errorMsg[256];      // Buffer for different error messages
    char errorMsgPopup[256]; // Buffer for popup error messages
    bool32 showError;
    bool32 showPopupError;
    bool32 helpAboutClicked;

    // Input
    bool32 escJustPressed; // Here for now becase we really don't need a whole lot of inputs
};

struct AppState {
    void* permanentMemory;
    u64 permanentMemorySize;
    void* scratchMemory;
    u64 scratchMemorySize;

    // Permanent arena is not necessary but helps the setup code so it's not so manual
    Arena permanentArena;
    Arena scratchArena;

    UIJob jobs[MAX_JOBS];
    volatile long jobCount;
    // If we were to use condition variables instead of spin lock
    //CONDITION_VARIABLE jobAvailable;
    //CRITICAL_SECTION jobLock;
    volatile long compressing;
    volatile long cancelRequested;
    HANDLE workerThread;

    f32 defaultTargetSize;
    f32 targetSizes[TARGET_SIZES_COUNT];
    Codec defaultCodec;

    // All UI state with ImGui
    UIState uiState;

    char* outputFolder; // User/Documents
    //bool32 useOutputFolder;

    char* exeDir;  // Absolute path to the exe directory
    char* tempDir; // Temp dir for ffmpeg logs

    char* appData;        // AppData/Local/EasyCompressor, where the config and imgui.ini
                          // are stored
    char* imguiIniPath;   // AppData/Local/EasyCompressor/imgui.ini
    char* configFilePath; // AppData/Local/EasyCompressor/easycompressor.cfg

    char* ffmpegPath; // Relative to working directory OR TODO: path?
};
