/*
    A simple ImGui UI wrapper for compressing files using ffmpeg
*/

// TODO: UNICODE SUPPORT?

#if COMPRESSOR_WIN32
//#    define UNICODE
#    define WIN32_LEAN_AND_MEAN
//#    define POPEN _popen
//#    define PCLOSE _pclose
#    define PATH_SEP '\\'
#    define NULL_DEV "NUL"

#    include <windows.h>

#    include <cderr.h>       // CommDlg errors
#    include <commdlg.h>     // OFN, GetSaveFileNameA
#    include <process.h>     // _beginthreadex
#    include <shlobj_core.h> // SHGetFolderPathA

// Windows...
#    ifdef ERROR
#        undef ERROR
#    endif

#    ifdef max
#        undef max
#    endif

#    ifdef min
#        undef min
#    endif
#endif

#include "imgui_draw.cpp"
#include "imgui_tables.cpp"
#include "imgui_widgets.cpp"

#include "imgui.cpp"

#include "backends/imgui_impl_dx11.cpp"
#include "backends/imgui_impl_win32.cpp"

//#include "backends/imgui_impl_dx11.h"
//#include "backends/imgui_impl_win32.h"
//#include "imgui.h"

#include "compressor.h"

#include "win32_compressor.h"

/// Printing

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

// Used in both debug and release
static void
PRINTF(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    OutputDebugStringA(buf);
}

// Frequency
static i64 gPerfFreq;

static LARGE_INTEGER
GetWallClock() {
    LARGE_INTEGER result;
    QueryPerformanceCounter(&result);
    return result;
}

static f32
GetMsElapsed(LARGE_INTEGER start, LARGE_INTEGER end) {
    f32 result =
        (static_cast<f32>(end.QuadPart - start.QuadPart) / static_cast<f32>(gPerfFreq)) * 1000.0f;
    return result;
}

struct ScopedTimer {
    LARGE_INTEGER start;
    const char* name;
    bool32 inSeconds;

    ScopedTimer(bool32 inSeconds = false) : name(""), inSeconds(inSeconds) {
        start = GetWallClock();
    }

    ScopedTimer(const char* n, bool32 inSeconds = false) : name(n), inSeconds(inSeconds) {
        start = GetWallClock();
    }

    ~ScopedTimer() {
        auto end = GetWallClock();
        f32 ms = GetMsElapsed(start, end);
        if (inSeconds) {
            ms /= 1000.0f;
            PRINTF("%s: %.2f s\n", name, ms);
        } else {
            PRINTF("%s: %.3f ms\n", name, ms);
        }
    }
};

static const char*
CodecText_(Codec s) {
    switch (s) {
    case Codec::H264:
        return "libx264";
    case Codec::H265:
        return "libx265";
    }

    return "";
}

static void
AddJob(AppState* appState, const char* path) {
    if (appState->jobCount >= MAX_JOBS) {
        DEBUG_PRINT("Jobs full!\n");
        return;
    }

    // TODO: If we allow adding, have to think about the UX a bit more
    //if (_InterlockedCompareExchange(&appState->compressing, 1, 1)) {
    //    DEBUG_PRINT("Can't add job while compressing!\n");
    //    return;
    //}

    UIJob* j = &appState->jobs[appState->jobCount++];
    *j = {}; // *j = UIJob{}; Compiler error when using /O2. Now seems fine?
    //ZeroMemory(j, sizeof(j)); nasty stuff, not dereferencing

    j->status = JobStatus::QUEUED;
    j->targetSizeMb = appState->defaultTargetSize;

    snprintf(j->input, sizeof(j->input), "%s", path);

    // TODO: maybe just use a dedicated output folder and use the input name as output
    // that way we wouldn't have to do this string processing nonsense
    const char* lastDot = nullptr;
    for (const char* scan = j->input; *scan; ++scan) {
        if (*scan == '.') {
            lastDot = scan;
        }
    }

    // No file extension found...
    // Don't fail but construct a default path without the extension but with _compressed
    if (!lastDot) {
        DEBUG_PRINT("Couldn't find file extension, constructed default path!\n");
        snprintf(j->output, sizeof(j->output), "%s_compressed", j->input);
    } else {
        i32 extensionLen = StrLength(lastDot);
        i32 inputLen = StrLength(j->input);
        i32 baseLen = inputLen - extensionLen; // Without extension

        char base[MAX_PATH_COUNT];
        CopyMemory(base, j->input, baseLen);
        base[baseLen] = '\0';

        snprintf(j->output, sizeof(j->output), "%s_compressed%s", base, lastDot);
    }

    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExA(j->input, GetFileExInfoStandard, &fileInfo)) {
        u64 bytes = (static_cast<u64>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow;
        j->inputFileSize = static_cast<f32>(bytes) / (1024.0f * 1024.0f);
    } else {
        DEBUG_PRINT("Failed to get file size!\n");
    }

    DEBUG_PRINTF("Added job: index = %d, input = %s,\ntarget size = %.2f MB, output = %s\n",
                 appState->jobCount - 1, j->input, j->targetSizeMb, j->output);
}

static void
RemoveJob(AppState* appState, i32 index) {
    ASSERT(index >= 0 && index < appState->jobCount);
    if (index < 0 || index >= appState->jobCount) {
        // bad!
        return;
    }

    DEBUG_PRINTF("Removed job %d\n", index);

    for (i32 i = index; i < appState->jobCount - 1; ++i) {
        appState->jobs[i] = appState->jobs[i + 1];
    }

    appState->jobCount--;
}

static void
MoveJob(AppState* appState, i32 from, i32 to, i32 highestRunningIndex) {
    // Holy assert heaven
    ASSERT(from != to && from >= 0 && to >= 0 && from < appState->jobCount &&
           to < appState->jobCount);
    if (from == to || from < 0 || to < 0 || from >= appState->jobCount ||
        to >= appState->jobCount) {
        return;
    }

    UIJob tmp = appState->jobs[from];
    ASSERT(tmp.status != JobStatus::RUNNING_PROBE && tmp.status != JobStatus::RUNNING_COMPRESS);
    ASSERT(appState->jobs[to].status != JobStatus::RUNNING_PROBE &&
           appState->jobs[to].status != JobStatus::RUNNING_COMPRESS);
    if (tmp.status == JobStatus::RUNNING_PROBE || tmp.status == JobStatus::RUNNING_COMPRESS ||
        appState->jobs[to].status == JobStatus::RUNNING_PROBE ||
        appState->jobs[to].status == JobStatus::RUNNING_COMPRESS) {
        return;
    }

    ASSERT(from > highestRunningIndex && to > highestRunningIndex);
    if (from <= highestRunningIndex || to <= highestRunningIndex) {
        return;
    }

    if (from < to) {
        for (i32 i = from; i < to; ++i) {
            appState->jobs[i] = appState->jobs[i + 1];
        }
    } else {
        for (i32 i = from; i > to; --i) {
            appState->jobs[i] = appState->jobs[i - 1];
        }
    }

    appState->jobs[to] = tmp;
    DEBUG_PRINTF("Moved job %d to %d\n", from, to);
}

// Wrapper for strtod for easier usage
// We use the double version for greater precision and rounding
// Default to a value of 0.0f on every error case
static f32
StrToF32(const char* start) {
    char* end = nullptr;
    f32 result = static_cast<f32>(strtod(start, &end));
    // (Error or underflow) or (no numbers parsed) or (overflow)
    if ((result == 0.0f) || (start == end) || (result == -HUGE_VALF || result == HUGE_VAL)) {
        result = 0.0f;
    }

    return result;
}

// -----------------------------------------------------------------------------
// Worker thread, runs jobs sequentially. For parallel encoding, spawn N of these
// and have them pop jobs off a shared index with InterlockedIncrement
// -----------------------------------------------------------------------------

static void
RunProbe(AppState* appState, UIJob* job) {
    ASSERT(job->status == JobStatus::QUEUED);
    job->status = JobStatus::RUNNING_PROBE;

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;

    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        DEBUG_PRINT("Couldn't create pipe! ABORTING!\n");
        job->status = JobStatus::ERROR;
        return;
    }

    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    char cmd[(MAX_PATH_COUNT * 2) + 128];
    snprintf(cmd, sizeof(cmd),
             "\"%sffprobe.exe\" -v error -show_entries format=duration "
             "-of default=noprint_wrappers=1:nokey=1 \"%s\"",
             appState->ffmpegPath, job->input);
    DEBUG_PRINTF("Running: %s\n", cmd);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    PROCESS_INFORMATION pi = {};

    //DEBUG_PRINTF("Creating process ffprobe for job %d\n", i);
    // TODO: handle exiting the program more controlled
    // Now Windows decides if the process should finish or not
    BOOL created = CreateProcessA(nullptr, cmd, nullptr, nullptr,
                                  TRUE, // inherit handles!
                                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(writePipe);

    if (!created) {
        DEBUG_PRINT("Couldn't create process ffprobe! ABORTING!\n");
        job->status = JobStatus::ERROR;
        CloseHandle(readPipe);
        return;
    }

    // Read output
    char buffer[256];
    DWORD bytesRead = 0;

    char output[256];
    DWORD totalRead = 0;

    while (ReadFile(readPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        if (totalRead + bytesRead < sizeof(output)) {
            CopyMemory(output + totalRead, buffer, bytesRead);
            totalRead += bytesRead;
        } else {
            DEBUG_PRINT("No space in buffer for ffprobe output!\n");
        }
    }

    CloseHandle(readPipe);

    //DEBUG_PRINT("Waiting for ffprobe...\n");
    WaitForSingleObject(pi.hProcess, INFINITE);
    //DEBUG_PRINT("ffprobe finished\n");

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode == 0) {
        // Parse duration
        f32 result = StrToF32(output);
        if (result == 0.0f) {
            DEBUG_PRINTF("StrToF32 produced 0.0f for RunProbe %s", job->input);
            job->status = JobStatus::ERROR;
            return;
        }

        job->durationSeconds = result;

        //job->progressPct = 100;

        job->status = JobStatus::DONE_PROBE;
    } else {
        DEBUG_PRINT(buffer);
        job->status = JobStatus::ERROR;
    }
}

static f32
ParseTimeFromOutput(const char* buffer) {
    f32 seconds = 0.0f;
    const char* p = buffer;
    const char* last = nullptr;
    while (*p) {
        const char* candidate = p;
        // Get the last time_out_us
        const char* match = "out_time_us=";
        while (*candidate && *match && *candidate == *match) {
            ++candidate;
            ++match;
        }

        if (*match == '\0') {
            last = candidate;
            p = candidate;
        } else {
            ++p;
        }
    }

    if (!last) {
        return seconds;
    }

    f32 result = StrToF32(last);
    if (result != 0.0f) {
        seconds = result / 1'000'000.0f;
    }

    return seconds;
}

static void
RunCompress(AppState* appState, UIJob* job) {
    ASSERT(job->status == JobStatus::DONE_PROBE || job->status == JobStatus::DONE_COMPRESS ||
           job->status == JobStatus::CANCELLED);
    job->status = JobStatus::RUNNING_COMPRESS;

    job->progressPct = 0;
    job->displayProgress = 0.0f;

    f32 totalBits = job->targetSizeMb * 1024.0f * 1024.0f * 8.0f;
    f32 totalKbps = (totalBits / job->durationSeconds) / 1000.0f;

    // Scale down audio if small
    f32 audioKbps = 128.0f;
    if (totalKbps < 256.0f) {
        audioKbps = 64.0f;
    }
    if (totalKbps < 128.0f) {
        audioKbps = 32.0f;
    }

    // TODO: 4-5% is enough, 3% might be cutting it too close
    // TODO: for 10 MB 3% is good, for smaller targets might not be able to hit
    // If target size is lass than 10 maybe then use like 5%
    f32 multiplier = 0.97f;
    f32 videoKbps = (totalKbps - audioKbps) * multiplier;
    if (videoKbps < 50.0f) {
        DEBUG_PRINT("Target size too small for this video duration "
                    "(video bitrate would be < 50 kbps), ABORTING\n");
        // TODO: cancel? and continue
        return;
    }

    DEBUG_PRINTF("Target: %.2f MB | total: %.1f kbps | video * %.2f: %.1f kbps | audio: %.1f "
                 "kbps\n",
                 job->targetSizeMb, totalKbps, multiplier, videoKbps, audioKbps);

    ASSERT(appState->defaultCodec != Codec::NONE);
    if (appState->defaultCodec == Codec::NONE) {
        DEBUG_PRINT("Codec was NONE, aborting compression\n");
        return;
    }

    // TODO: different presets?? maybe not needed
    const char* codec = CodecText_(appState->defaultCodec);

    // The workload is roughly 40-45% pass 1 55-60% pass 2, H.264
    const i32 pass1Weight = 42;
    const i32 pass2Weight = 58;
    ASSERT(pass1Weight + pass2Weight == 100);

    // -progress pipe:1
    /*
        frame=51
        fps=0.00
        stream_0_0_q=40.0
        bitrate=N/A
        total_size=N/A
        out_time_us=833333
        out_time_ms=833333
        out_time=00:00:00.833333
        dup_frames=0
        drop_frames=0
        speed=1.62x
        progress=continue
    */

    char cmd[(MAX_PATH_COUNT * 2) + 128];

    /// Pass 1

    {
        ScopedTimer t = { "Pass 1", true };

        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;

        if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
            DEBUG_PRINT("Couldn't create pipe! ABORTING!\n");
            job->status = JobStatus::ERROR;
            return;
        }

        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

        snprintf(cmd, sizeof(cmd),
                 "%sffmpeg -y -hide_banner -loglevel error -progress pipe:1 "
                 "-i %s -c:v %s -preset medium -b:v %.0fk "
                 "-pass 1 -an -f null %s",
                 //"-pass 1 -passlogfile %s -an -f null %s",
                 appState->ffmpegPath, job->input, codec, videoKbps, NULL_DEV);
        DEBUG_PRINTF("Running: %s\n", cmd);

        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = writePipe;
        si.hStdError = writePipe;

        PROCESS_INFORMATION pi = {};
        BOOL created = CreateProcessA(nullptr, cmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                      nullptr, nullptr, &si, &pi);

        CloseHandle(writePipe);

        if (!created) {
            DEBUG_PRINT("Couldn't create process ffmpeg! ABORTING!\n");
            job->status = JobStatus::ERROR;
            CloseHandle(readPipe);
            return;
        }

        char buffer[256];
        DWORD bytesRead = 0;

        bool32 cancelled = false;
        while (ReadFile(readPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) &&
               bytesRead > 0) {
            if (_InterlockedCompareExchange(&appState->cancelRequested, 0, 0)) {
                DEBUG_PRINT("Cancelled ffmpeg pass 1!\n");
                TerminateProcess(pi.hProcess, 1);

                DEBUG_PRINT("Waiting for termination...\n");
                // Wait before requesting a delete, to safely delete
                WaitForSingleObject(pi.hProcess, INFINITE);
                if (!DeleteFileA(job->output)) {
                    DEBUG_PRINTF("Couldn't delete file %s after terminating process!\n",
                                 job->output);
                }

                job->status = JobStatus::CANCELLED;
                cancelled = true;
                break;
            }

            // Parse time and compare against the video duration
            // Update progressPct
            buffer[bytesRead] = '\0';
            f32 time = ParseTimeFromOutput(buffer);
            if (time != 0.0f) {
                LONG progress = static_cast<LONG>((time / job->durationSeconds) * pass1Weight);
                _InterlockedExchange(&job->progressPct, progress);
            }
        }

        CloseHandle(readPipe);

        if (!cancelled) {
            DEBUG_PRINT("Waiting for ffmpeg pass 1...\n");
            WaitForSingleObject(pi.hProcess, INFINITE);
        }

        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exitCode != 0) {
            if (!cancelled) {
                DEBUG_PRINT(buffer);
                job->status = JobStatus::ERROR;
                DEBUG_PRINT("Exit code != 0 pass 1\n");
            }

            return;
        }

        //job->status = JobStatus::DONE_COMPRESS_PASS1; ?
    }

    /// Pass 2

    {
        ScopedTimer t = { "Pass 2", true };

        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;

        if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
            DEBUG_PRINT("Couldn't create pipe! ABORTING!\n");
            job->status = JobStatus::ERROR;
            return;
        }

        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

        snprintf(cmd, sizeof(cmd),
                 "%sffmpeg -y -hide_banner -loglevel error -progress pipe:1 "
                 "-i %s -c:v %s -preset medium -b:v %.0fk "
                 "-pass 2 "
                 //"-pass 2 -passlogfile %s "
                 "-c:a aac -b:a %.0fk -movflags +faststart %s",
                 appState->ffmpegPath, job->input, codec, videoKbps //, passLog
                 ,
                 audioKbps, job->output);
        DEBUG_PRINTF("Running: %s\n", cmd);

        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = writePipe;
        si.hStdError = writePipe;

        PROCESS_INFORMATION pi = {};
        BOOL created = CreateProcessA(nullptr, cmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                      nullptr, nullptr, &si, &pi);

        CloseHandle(writePipe);

        if (!created) {
            DEBUG_PRINT("Couldn't create process ffmpeg! ABORTING!\n");
            job->status = JobStatus::ERROR;
            //CloseHandle(readPipe);
            return;
        }

        char buffer[256];
        DWORD bytesRead = 0;

        bool32 cancelled = false;
        while (ReadFile(readPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) &&
               bytesRead > 0) {
            if (_InterlockedCompareExchange(&appState->cancelRequested, 0, 0)) {
                DEBUG_PRINT("Cancelled ffmpeg pass 2!\n");
                TerminateProcess(pi.hProcess, 1);

                DEBUG_PRINT("Waiting for termination...\n");
                WaitForSingleObject(pi.hProcess, INFINITE);
                if (!DeleteFileA(job->output)) {
                    DEBUG_PRINTF("Couldn't delete file %s after terminating process!\n",
                                 job->output);
                }

                job->status = JobStatus::CANCELLED;
                cancelled = true;
                break;
            }

            buffer[bytesRead] = '\0';
            f32 time = ParseTimeFromOutput(buffer);
            if (time != 0.0f) {
                LONG progress =
                    pass1Weight + (static_cast<LONG>((time / job->durationSeconds) * pass2Weight));
                _InterlockedExchange(&job->progressPct, progress);
            }
        }

        CloseHandle(readPipe);

        if (!cancelled) {
            _InterlockedExchange(&job->progressPct, 100);
            DEBUG_PRINT("Waiting for ffmpeg pass 2...\n");
            WaitForSingleObject(pi.hProcess, INFINITE);
        }

        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exitCode != 0) {
            if (!cancelled) {
                DEBUG_PRINT(buffer);
                job->status = JobStatus::ERROR;
                DEBUG_PRINT("Exit code != 0 pass 2\n");
            }

            return;
        }

        WIN32_FILE_ATTRIBUTE_DATA fileInfo;
        if (GetFileAttributesExA(job->output, GetFileExInfoStandard, &fileInfo)) {
            u64 bytes = (static_cast<u64>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow;
            job->resultFileSize = static_cast<f32>(bytes) / (1024.0f * 1024.0f);
        } else {
            DEBUG_PRINT("Failed to get file size!\n");
        }

        job->status = JobStatus::DONE_COMPRESS;
        job->openFlashTimer = OPEN_FLASH_TIMER_START;
    }
}

static DWORD WINAPI
WorkerThread(void* param) {
    AppState* appState = static_cast<AppState*>(param);

    // TODO: move away from spin lock? but this is just much simpler...
    while (true) {
        i32 jobCount = static_cast<i32>(_InterlockedCompareExchange(&appState->jobCount, 0, 0));
        for (i32 i = 0; i < jobCount; ++i) {
            UIJob* job = &appState->jobs[i];
            if (job->status == JobStatus::QUEUED) {
                ScopedTimer t = {};
                RunProbe(appState, job);
                PRINTF("RunProbe for job %d", i);
            }
        }

        if (_InterlockedCompareExchange(&appState->compressing, 0, 0)) {
            jobCount = static_cast<i32>(_InterlockedCompareExchange(&appState->jobCount, 0, 0));
            for (i32 i = 0; i < jobCount; ++i) {
                UIJob* job = &appState->jobs[i];
                // Allow compressing again
                // TODO: should the default be this so every file gets compressed again?
                // OR the user can choose if just compressed files should be skipped or
                // compressed again. This covers both the cases of adding files when compressing
                // and not compressing
                if (job->status == JobStatus::DONE_PROBE ||
                    job->status == JobStatus::DONE_COMPRESS ||
                    job->status == JobStatus::CANCELLED) {
                    {
                        ScopedTimer t = { true };
                        PRINTF("Start RunCompress for job %d\n", i);
                        RunCompress(appState, job);
                        PRINTF("RunCompress for job %d", i);
                    }

                    // Sets to 0 automatically if was 1
                    if (_InterlockedCompareExchange(&appState->cancelRequested, 0, 1)) {
                        DEBUG_PRINT("Cancelled compressing...\n");
                        break;
                    }
                }
            }

            _InterlockedExchange(&appState->compressing, 0);
        }

        Sleep(50);
    }
}

static void
StartBatch(AppState* appState) {
    if (appState->jobCount == 0) {
        return;
    }

    // appState->compressing is read by WorkerThread
    _InterlockedExchange(&appState->compressing, 1);
    DEBUG_PRINT("Start clicked\n");
}

// -----------------------------------------------------------------------------
/// Path stuff
// -----------------------------------------------------------------------------

static void
GetExeDirectory(AppState* appState) {
    GetModuleFileNameA(nullptr, appState->exeDir, sizeof(appState->exeDir));

    i32 lastSlashIndex = -1;
    const char* scan = appState->exeDir;
    for (i32 i = 0; *scan; ++scan, ++i) {
        if (*scan == PATH_SEP) {
            lastSlashIndex = i;
        }
    }

    if (lastSlashIndex >= 0) {
        appState->exeDir[lastSlashIndex] = '\0';
    }
}

static void
SelectInExplorer(HWND hWnd, const char* path) {
    char cmd[MAX_PATH_COUNT];
    snprintf(cmd, sizeof(cmd), "/select,\"%s\"", path);
    ShellExecuteA(hWnd, "open", "explorer.exe", cmd, nullptr, SW_SHOWNORMAL);
}

static void
OpenInExplorer(HWND hWnd, const char* path) {
    ShellExecuteA(hWnd, "open", "explorer.exe", path, nullptr, SW_SHOWNORMAL);
}

static void
PickOutputPath(HINSTANCE hInstance, HWND hWnd, char* outPath) {
    char buffer[MAX_PATH_COUNT] = {};

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);

    ofn.hwndOwner = hWnd;
    ofn.hInstance = hInstance;

    // TODO: more extensions
    ofn.lpstrFilter = "MP4 Video (*.mp4)\0*.mp4\0All Files\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.lpstrTitle = "Select output file";
    ofn.nMaxFile = sizeof(buffer);
    // NOCHANGE_DIR to prevent generating imgui.ini
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "mp4";

    // TODO: throws some weird exceptions via explorerframe.dll
    // Error codes 80004001, 80070057
    // Works correctly though
    // TODO: consider using the more modern IFileDialog
    // https://learn.microsoft.com/en-us/windows/win32/shell/common-file-dialog
    // But taking a look at it, it's just so messy compared to this... (not a surprise though)
    if (GetSaveFileNameA(&ofn)) {
        CopyMemory(outPath, ofn.lpstrFile, MAX_PATH_COUNT);
        DEBUG_PRINTF("Picked new output path: %s\n", outPath);
    } else {
        DWORD err = CommDlgExtendedError();
        DEBUG_PRINTF("CommDlgExtendedError in PickOutputPath: %u\n", err);

        if (err == FNERR_BUFFERTOOSMALL) {
            // TODO: show errors for user here also
            DEBUG_PRINT("Buffer too small for output path!\n");
        } else if (err == FNERR_INVALIDFILENAME) {
            DEBUG_PRINT("Invalid file name for output path!\n");
            // TODO: handle?
        } else {
            DEBUG_PRINT("Cancelled pick output path dialog!\n");
        }
    }
}

static void
PickInputFiles(HINSTANCE hInstance, HWND hWnd, AppState* appState) {
    // TODO: Take care of stack size if MAX_PATH_COUNT is changed
    char buffer[MAX_PATH_COUNT * MAX_JOBS] = {};

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);

    ofn.hwndOwner = hWnd;
    ofn.hInstance = hInstance;

    // TODO: more extensions
    ofn.lpstrFilter = "MP4 Video (*.mp4)\0*.mp4\0All Files\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.lpstrTitle = "Select input files";
    ofn.nMaxFile = sizeof(buffer);
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT;
    ofn.lpstrDefExt = "mp4";

    if (!GetOpenFileNameA(&ofn)) {
        DWORD err = CommDlgExtendedError();
        DEBUG_PRINTF("CommDlgExtendedError in PickInputFiles: %u\n", err);
        return;
    }

    // When multiple files are selected
    // "C:\dir\0file1.mp4\0file2.mp4\0\0"
    // When a single file selected
    // "C:\dir\file1.mp4\0\0"
    // We can see it ends with a double null termination
    const char* dir = buffer;
    const char* file = buffer + ofn.nFileOffset; // Get the first file

    // Single file
    if (*(file - 1) != '\0') {
        AddJob(appState, buffer);
        return;
    }

    // Multiple files
    while (*file) {
        char path[MAX_PATH_COUNT];
        snprintf(path, sizeof(path), "%s\\%s", dir, file);
        AddJob(appState, path);
        file += StrLength(file) + 1;
    }
}

// -----------------------------------------------------------------------------
// ImGui
// -----------------------------------------------------------------------------

static const char*
JobStatusText(JobStatus s) {
    switch (s) {
    case JobStatus::QUEUED:
        return "Queued for next start...";
    case JobStatus::RUNNING_PROBE:
        return "Calculating duration...";
    case JobStatus::DONE_PROBE:
        return "Ready to compress";
    case JobStatus::RUNNING_COMPRESS:
        return "Compressing...";
    case JobStatus::DONE_COMPRESS:
        return "Compressed!";
    case JobStatus::CANCELLED:
        return "Cancelled!";
    case JobStatus::ERROR:
        return "Error";
    }

    return "";
}

static void
SetTargetSizeForAll(AppState* appState, f32 targetSize) {
    DEBUG_PRINTF("Set target size for all %.2f MB\n", targetSize);

    for (i32 i = 0; i < appState->jobCount; ++i) {
        UIJob* job = &appState->jobs[i];
        if (job->status != JobStatus::RUNNING_PROBE && job->status != JobStatus::RUNNING_COMPRESS) {
            job->targetSizeMb = targetSize;
        }
    }
}

static f32
ClampF32(f32 value, f32 min, f32 max) {
    ASSERT(min <= max);
    if (value < min) {
        return min;
    }

    if (value > max) {
        return max;
    }

    return value;
}

static void
CancelBatch(AppState* appState) {
    _InterlockedExchange(&appState->cancelRequested, 1);
    DEBUG_PRINT("Cancel pressed!\n");
}

static void
DrawUi(AppState* appState, HINSTANCE hInstance, HWND hWnd, f32 scale, f32 delta) {
    UIState* uiState = &appState->uiState;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("EasyCompressor", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Add files...")) {
                DEBUG_PRINT("File -> Add files\n");
                PickInputFiles(hInstance, hWnd, appState);
            }

            if (ImGui::MenuItem("Exit")) {
                PostQuitMessage(0);
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                uiState->helpAboutClicked = true;
                DEBUG_PRINT("About clicked\n");
            }

            if (ImGui::MenuItem("Open config folder...")) {
                OpenInExplorer(hWnd, appState->appData);
            }

            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    /// END MENU

    ImGui::TextDisabled("Drop video files anywhere on this window. Max %d", MAX_JOBS);
    ImGui::Separator();

    bool32 compressing = _InterlockedCompareExchange(&appState->compressing, 0, 0);
    bool32 cancelled = _InterlockedCompareExchange(&appState->cancelRequested, 0, 0);
    bool32 noJobs = appState->jobCount == 0;

#if COMPRESSOR_INTERNAL
    ImGui::TextDisabled("Compressing: %d, cancelled: %d", compressing, cancelled);
#endif

    const f32 sliderWidth = 190 * scale;

    /// Default target size

    ImGui::Text("Default target size:");
    f32* defaultTargetSize = &appState->defaultTargetSize;
    ImGui::SetNextItemWidth(sliderWidth);
    ImGui::SliderFloat("##mb_slider_default", defaultTargetSize, MIN_TARGET_SIZE, MAX_TARGET_SIZE,
                       "%.1f MB", ImGuiSliderFlags_Logarithmic);

    ImGui::SameLine();
    ImGui::BeginDisabled(compressing || noJobs);
    if (ImGui::Button("Apply to all files")) {
        SetTargetSizeForAll(appState, *defaultTargetSize);
    }

    ImGui::EndDisabled();

    ImGui::SameLine(0.0f, 10.0f);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0.0f, 10.0f);

    ImGui::Text("Codec used:");
    ImGui::SameLine();
    ImGui::BeginDisabled(compressing);

    /// Codec

    Codec currentCodec = appState->defaultCodec;
    if (ImGui::RadioButton("H.264", appState->defaultCodec == Codec::H264)) {
        appState->defaultCodec = Codec::H264;
    }

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Recommended default codec\n");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (ImGui::RadioButton("H.265", currentCodec == Codec::H265)) {
        appState->defaultCodec = Codec::H265;
    }

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            "Can reduce video size 20-75% more compared to H.264, but will take a lot longer\n"
            "Final size may end up being significantly below the target");
        ImGui::EndTooltip();
    }

    ImGui::EndDisabled();

    ImGui::SetNextItemWidth(sliderWidth);
    ImGui::InputFloat("##mb_input_default", defaultTargetSize, 0.0f, 0.0f, "%.2f MB");
    *defaultTargetSize = ClampF32(*defaultTargetSize, MIN_TARGET_SIZE, MAX_TARGET_SIZE);

    for (i32 i = 0; i < ARRAY_COUNT(appState->targetSizes); ++i) {
        f32 size = appState->targetSizes[i];
        // Default value which is filled if the user supplied less than SIZES_COUNT
        if (size == 0.0f) {
            continue;
        }

        if (i != 0) {
            ImGui::SameLine();
        }

        char label[32];
        snprintf(label, sizeof(label), "%.1f MB##default_%d", size, i);
        if (ImGui::Button(label, ImVec2(80 * scale, 0))) {
            *defaultTargetSize = size;
        }
    }

    /// Table

    // TODO: allow sorting based on input/output?
    // This is just an idea and in no means a necessary feature
    // At least for file info (size or duration or both???, both might not be feasible or even
    // necessary), target size and status

    // Disable the visual feedback of BeginDisabled (making the DisabledAlpha 0.5f)
    // when doing probing to avoid flashing the texts annoyingly
    bool32 alphaDisabledModified = false;

    if (ImGui::BeginTable("jobs", 6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn(
            "#", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 20 * scale);
        ImGui::TableSetupColumn("Input/Output", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("File info", ImGuiTableColumnFlags_WidthFixed, 127 * scale);
        ImGui::TableSetupColumn("Target size", ImGuiTableColumnFlags_WidthFixed, sliderWidth);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 193 * scale);
        ImGui::TableSetupColumn("Remove?", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        i32 moveFromIndex = -1, moveToIndex = -1, removeIndex = -1;

        // Only allow reordering of jobs that are above the current running index
        // Otherwise it's too much work to get right
        // This is because when we hit start, the WorkerThread stores the count of jobs at that time
        i32 highestRunningIndex = -1;
        for (i32 i = 0; i < appState->jobCount; ++i) {
            auto status = appState->jobs[i].status;
            if (status == JobStatus::RUNNING_PROBE || status == JobStatus::RUNNING_COMPRESS) {
                highestRunningIndex = i;
                // Alpha disabled modifier
                if (status == JobStatus::RUNNING_PROBE) {
                    alphaDisabledModified = true;
                    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
                }
            }
        }

        for (i32 i = 0; i < appState->jobCount; ++i) {
            UIJob* job = &appState->jobs[i];

            bool32 jobRunning = job->status == JobStatus::RUNNING_PROBE ||
                                job->status == JobStatus::RUNNING_COMPRESS;
            compressing |= jobRunning;

            ImGui::PushID(i);
            ImGui::TableNextRow();

            // Drag & drop reordering
            auto handleJobDragDrop = [&](i32 i) {
                if (!jobRunning && i > highestRunningIndex) {
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
                        ImGui::SetDragDropPayload("JOB_ROW", &i, sizeof(i32));
                        ImGui::Text("Move %s", appState->jobs[i].input);
                        ImGui::EndDragDropSource();
                    }

                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JOB_ROW")) {
                            moveFromIndex = *(static_cast<i32*>(p->Data));
                            moveToIndex = i;
                        }

                        ImGui::EndDragDropTarget();
                    }
                }
            };

            ImGui::TableSetColumnIndex(0);
            //ImGui::Text("%d", i + 1);
            ImGui::BeginDisabled(jobRunning || i < highestRunningIndex);
            {
                char label[4];
                snprintf(label, sizeof(label), "%d", i + 1);
                // Full cell width and height
                ImGui::Selectable(
                    label, false, 0,
                    ImVec2(0, ImGui::GetFrameHeight() * 2 + ImGui::GetStyle().ItemSpacing.y));
            }

            handleJobDragDrop(i);

            ImGui::TableSetColumnIndex(1);
            //ImGui::TextUnformatted(job->input);
            ImGui::Selectable(job->input);
            handleJobDragDrop(i);

            ImGui::EndDisabled();

            ImGui::BeginDisabled(jobRunning);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.7f, 1.0f));
            ImGui::Selectable(job->output);
            ImGui::PopStyleColor();

            ImGui::EndDisabled();

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Click to change output directory\n");
                ImGui::TextUnformatted("Middle click to open input in explorer\n");
                ImGui::TextUnformatted("Right click for more options\n");
                ImGui::EndTooltip();
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                PickOutputPath(hInstance, hWnd, job->output);
                // This is done to reset broken hover state after the dialog closes
                ImGui::GetIO().ClearInputMouse();
            } else if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) {
                SelectInExplorer(hWnd, job->input);
            } else if (ImGui::BeginPopupContextItem("job_context_menu")) {
                if (ImGui::MenuItem("Open input in explorer...")) {
                    SelectInExplorer(hWnd, job->input);
                }

                // TODO: more?
                //if (ImGui::MenuItem("Reset to default output")) {
                //DeriveOutputPath(job->input, job->output, sizeof(job->output));
                //}

                ImGui::EndPopup();
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("Size: %.1f MB", job->inputFileSize);
            if (job->durationSeconds != 0.0f) {
                ImGui::Text("Duration: %.1f s", job->durationSeconds);
            }

            ImGui::TableSetColumnIndex(3);

            f32* targetSize = &job->targetSizeMb;
            bool32 tooLargeBefore = *targetSize >= job->inputFileSize;
            if (tooLargeBefore) {
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.35f, 0.1f, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.45f, 0.15f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.55f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.65f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.75f, 0.25f, 0.25f, 1.0f));
            }

            ImGui::BeginDisabled(jobRunning);
            ImGui::SetNextItemWidth(sliderWidth);
            ImGui::SliderFloat("##mb_slider", targetSize, MIN_TARGET_SIZE, MAX_TARGET_SIZE,
                               "%.1f MB", ImGuiSliderFlags_Logarithmic);
            if (tooLargeBefore && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Warning: target size greater than file size! File will "
                                       "not be compressed!");
                ImGui::EndTooltip();
            }

            ImGui::SetNextItemWidth(sliderWidth);
            ImGui::InputFloat("##mb_input", targetSize, 0.0f, 0.0f, "%.2f MB");
            if (tooLargeBefore && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Warning: target size greater than file size! File will "
                                       "not be compressed!");
                ImGui::EndTooltip();
            }

            if (tooLargeBefore) {
                ImGui::PopStyleColor(5);
            }

            *targetSize = ClampF32(*targetSize, MIN_TARGET_SIZE, MAX_TARGET_SIZE);
            ImGui::EndDisabled();

            ImGui::TableSetColumnIndex(4);

            const char* statusText = JobStatusText(static_cast<JobStatus>(job->status));
            if (job->progressPct == 0) {
                ImGui::TextUnformatted(statusText);
            } else {
                ASSERT(job->progressPct >= 0 && job->progressPct <= 100);
                f32 target = static_cast<f32>(job->progressPct) / 100.0f;
                f32 diff = target - job->displayProgress;
                //DEBUG_PRINTF("%.3f\n", diff);
                f32 speed = diff > 0.1f ? 8.0f : 3.0f;
                job->displayProgress += (target - job->displayProgress) * speed * delta;
                //DEBUG_PRINTF("%.3f\n", job->displayProgress);
                job->displayProgress = ClampF32(job->displayProgress, 0.0f, 1.0f);
                ImGui::ProgressBar(job->displayProgress);
                // A way to get the height for a dummy for example
                //ImGui::ProgressBar(job->displayProgress, ImVec2(-1.0f,
                //ImGui::GetTextLineHeight()));
            }

            if (job->progressPct != 0 && job->status == JobStatus::CANCELLED) {
                ImGui::TextUnformatted(statusText);
            }

            if (job->status == JobStatus::DONE_COMPRESS) {
                ImGui::Text("Result size: %.1f MB", job->resultFileSize);

                ImGui::SameLine();

                ASSERT(job->openFlashTimer >= 0.0f &&
                       job->openFlashTimer <= OPEN_FLASH_TIMER_START);
                job->openFlashTimer -= delta;
                job->openFlashTimer = ClampF32(job->openFlashTimer, 0.0f, OPEN_FLASH_TIMER_START);
                f32 fade = ClampF32(job->openFlashTimer, 0.0f, 1.0f);
                f32 pulse = (sinf(job->openFlashTimer * 4.0f) + 1.0f) * 0.5f;
                f32 t = pulse * fade;

                ImVec4 colorA = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
                ImVec4 colorB = ImGui::GetStyleColorVec4(ImGuiCol_Button);
                ImVec4 color = ImVec4(colorB.x + ((colorA.x - colorB.x) * t),
                                      colorB.y + ((colorA.y - colorB.y) * t),
                                      colorB.z + ((colorA.z - colorB.z) * t), 1.0f);

                ImGui::PushStyleColor(ImGuiCol_Button, color);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
                // Align end
                f32 avail = ImGui::GetContentRegionAvail().x;
                const char* label = "Open";
                f32 buttonWidth =
                    ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - buttonWidth);
                if (ImGui::SmallButton(label)) {
                    SelectInExplorer(hWnd, job->output);
                }

                ImGui::PopStyleColor(2);
            }

            ImGui::TableSetColumnIndex(5);
            // TODO: If we use compressing here
            // we disable removing any file while compressing any file
            // It would be a bit tricky to allow only removing newly added jobs if they are
            // added during compressing. Current workflow is canceling the compressing and then
            // removing them, which is fine
            ImGui::BeginDisabled(compressing || jobRunning);
            if (ImGui::SmallButton("X")) {
                removeIndex = i;
            }

            ImGui::EndDisabled();
            ImGui::PopID();
        }

        ImGui::EndTable();

        if (moveFromIndex != -1) {
            MoveJob(appState, moveFromIndex, moveToIndex, highestRunningIndex);
        }

        if (removeIndex != -1) {
            RemoveJob(appState, removeIndex);
        }
    }

    ImGui::BeginDisabled(compressing || noJobs);
    if (ImGui::Button("Start", ImVec2(100 * scale, 0))) {
        StartBatch(appState);
    }

    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!compressing || cancelled || noJobs);
    if (ImGui::Button("Cancel")) {
        CancelBatch(appState);
    }

    ImGui::EndDisabled();

    /// File icon for adding files

    ImGui::SameLine();
    f32 size = 36.0f * scale;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - size) * 0.5f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    f32 fileW = size * 0.7f;
    f32 fold = size * 0.25f;

    ImU32 col = IM_COL32(200, 200, 200, 255);

    // File outline
    dl->AddLine(ImVec2(pos.x, pos.y), ImVec2(pos.x + fileW - fold, pos.y), col, 2.0f);
    dl->AddLine(ImVec2(pos.x, pos.y), ImVec2(pos.x, pos.y + size), col, 2.0f);
    dl->AddLine(ImVec2(pos.x, pos.y + size), ImVec2(pos.x + fileW, pos.y + size), col, 2.0f);
    dl->AddLine(ImVec2(pos.x + fileW, pos.y + fold), ImVec2(pos.x + fileW, pos.y + size), col,
                2.0f);

    // Fold diagonal
    dl->AddLine(ImVec2(pos.x + fileW - fold, pos.y), ImVec2(pos.x + fileW, pos.y + fold), col,
                2.0f);

    // Corner triangle
    dl->AddTriangleFilled(ImVec2(pos.x + fileW - fold - 1.0f, pos.y + 1.0f),
                          ImVec2(pos.x + fileW - 1.0f, pos.y + fold + 1.0f),
                          ImVec2(pos.x + fileW - fold - 1.0f, pos.y + fold + 1.0f),
                          IM_COL32(150, 150, 150, 255));

    // Plus sign
    f32 cx = pos.x + (size * 0.35f);
    f32 cy = pos.y + (size * 0.65f);
    f32 arm = size * 0.15f;
    f32 thickness = 2.0f;
    dl->AddLine(ImVec2(cx - arm, cy), ImVec2(cx + arm, cy), IM_COL32(100, 220, 100, 255),
                thickness);
    dl->AddLine(ImVec2(cx, cy - arm), ImVec2(cx, cy + arm), IM_COL32(100, 220, 100, 255),
                thickness);

    // Invisible button for interaction
    ImGui::InvisibleButton("##add_files", ImVec2(fileW, size));
    if (ImGui::IsItemClicked()) {
        PickInputFiles(hInstance, hWnd, appState);
        ImGui::GetIO().ClearInputMouse(); // ImGui input really doesn't like blocking functions
    }

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Shortcut: Control + A");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (size - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::TextDisabled("Add files...");

    ImGui::Separator();

    ImGui::BeginDisabled(compressing || noJobs);
    if (ImGui::Button("Clear", ImVec2(80 * scale, 0))) {
        DEBUG_PRINT("Clear\n");
        appState->jobCount = 0;
    }

    ImGui::EndDisabled();

    // Remove the global alpha disabled modifier
    if (alphaDisabledModified) {
        ImGui::PopStyleVar();
    }

    ImGui::End();

    /// ImGui::End()

    // We have to do popup stuff here

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("HelpAboutPopup", nullptr,
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextUnformatted("EasyCompressor");
        ImGui::Separator();

        ImGui::TextUnformatted("Built with");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("ImGui", "https://github.com/ocornut/imgui");

        ImGui::Text("Max length for input/output paths is %d", MAX_PATH_COUNT);
        ImGui::TextUnformatted("Small target sizes (below 10 MB) might result in the\ncompressed "
                               "size being slightly above the target size");

        if (ImGui::Button("OK")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (uiState->helpAboutClicked) {
        ImGui::OpenPopup("HelpAboutPopup");
        uiState->helpAboutClicked = false;
    }
}

// -----------------------------------------------------------------------------
/// Win32 / DX11 boilerplate (from the ImGui example, trimmed)
// -----------------------------------------------------------------------------

static ID3D11Device* gDevice;
static ID3D11DeviceContext* gContext;
static IDXGISwapChain* gSwap;
static ID3D11RenderTargetView* gRtv;

static bool32 gSwapChainOccluded;
static i32 gResizeWidth, gResizeHeight;

static void
CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    gSwap->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    gDevice->CreateRenderTargetView(pBackBuffer, nullptr, &gRtv);
    pBackBuffer->Release();
}

static void
CleanupRenderTarget() {
    if (gRtv) {
        gRtv->Release();
        gRtv = nullptr;
    }
}

static bool
CreateDeviceD3D(HWND hWnd) {
    // Setup swap chain
    // This is a basic setup. Optimally could use e.g. DXGI_SWAP_EFFECT_FLIP_DISCARD and handle
    // fullscreen mode differently. See #8979 for suggestions.
    DXGI_SWAP_CHAIN_DESC sd = {};

    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2,
        D3D11_SDK_VERSION, &sd, &gSwap, &gDevice, &featureLevel, &gContext);
    if (res == DXGI_ERROR_UNSUPPORTED) { // Try high-performance WARP software driver if
                                         // hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2,
            D3D11_SDK_VERSION, &sd, &gSwap, &gDevice, &featureLevel, &gContext);
    }
    if (res != S_OK) {
        return false;
    }

    CreateRenderTarget();
    return true;
}

static void
CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (gSwap) {
        gSwap->Release();
        gSwap = nullptr;
    }
    if (gContext) {
        gContext->Release();
        gContext = nullptr;
    }
    if (gDevice) {
        gDevice->Release();
        gDevice = nullptr;
    }
}

// Used only inside WndProc
// TODO: there might be a way to pass appState via the HWND
static AppState* gAppState;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

static LRESULT WINAPI
WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg) {
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        UINT fileCount = DragQueryFileA(drop, 0xFFFFFFFF, nullptr, 0);
        // TODO: validate by most common video file extensions?
        for (UINT i = 0; i < fileCount && i < MAX_JOBS; ++i) {
            char path[MAX_PATH_COUNT];
            // Query the required character amount first, not including null terminator
            // If we don't query we have no way of deducing if the path was truncated or it's
            // exactly MAX_PATH_COUNT long
            UINT required = DragQueryFileA(drop, i, nullptr, 0);

            // Get the path and get the copied amount, not including null terminator
            UINT copied = DragQueryFileA(drop, i, path, sizeof(path));

            DEBUG_PRINTF("Required: %u, copied %u (both not including null terminator)\n", required,
                         copied);

            // required > copied would work as well
            if (required >= sizeof(path)) {
                DEBUG_PRINTF("Path was truncated, didn't add job! Max length: %d\nPath would have "
                             "been %s\n",
                             MAX_PATH_COUNT, path);
                // TODO: show error for user for discarded files
                continue;
            }

            ASSERT(gAppState);
            AddJob(gAppState, path);
        }

        DragFinish(drop);
        return 0;
    } break;

    case WM_SIZE: {
        if (gDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            gSwap->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }

        return 0;
    } break;

    case WM_DESTROY: {
        PostQuitMessage(0);
        return 0;
    } break;

    default: {
    } break;
    }

    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

static bool32
CreateDefaultConfigFile(const char* path) {
    DEBUG_PRINT("Trying to create default config file...\n");
    HANDLE file =
        CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!file) {
        DEBUG_PRINT("Couldn't create default config file!\n");
        return false;
    }

    char content[] =
        "# IMPORTANT: don't change anything inside this file except the sizes!\n\n"
        "# You can specify up to 5 preset sizes the app loads when it starts, only the "
        "first 5 are used\n"
        "# Append ! after a value to signal it as the default value\n"
        "# Note that the min and max values are 0.5 and 5000.0. Values outside this "
        "range are clamped\n\n"
        "[Sizes]\n5.0\n10.0 !\n25.0\n50.0\n100.0\n\n"
        "[Codecs]\nh264 !\nh265\n";

    DWORD written = 0;
    // Don't write null terminator
    WriteFile(file, content, sizeof(content) - 1, &written, nullptr);
    if (written == 0) {
        DEBUG_PRINT("Couldn't write to default config file!\n");
        CloseHandle(file);
        return false;
    }

    DEBUG_PRINT("Created default config file!\n");
    CloseHandle(file);
    return true;
}

static bool32
LoadConfigFile(AppState* appState, const char* path) {
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!file) {
        DEBUG_PRINT("Config file didn't exist or couldn't open!\n");
        return false;
    }

    char buf[512];
    DWORD bytesRead = 0;

    if (!ReadFile(file, buf, sizeof(buf), &bytesRead, nullptr) || bytesRead == 0) {
        DEBUG_PRINT("Couldn't read file!\n");
        CloseHandle(file);
        return false;
    }

    buf[bytesRead] = '\0';

    i32 sizesParsed = 0;
    bool32 inSizes = false;
    bool32 inCodecs = false;
    bool32 foundDefaultForSize = false;
    bool32 foundDefaultForCodec = false;
    const char* p = buf;

    i32 sizesCount = ARRAY_COUNT(appState->targetSizes);

    // Currently handles whitespace and other characters at the end only
    // I think this is fine
    while (*p) {
        while (*p == ' ' || *p == '\r' || *p == '\n') {
            ++p;
        }

        // Comments
        if (*p == '#') {
            while (*p && *p != '\n') {
                ++p;
            }

            continue;
        }

        // Section
        if (*p == '[') {
            inSizes = (p[1] == 'S' && p[2] == 'i' && p[3] == 'z' && p[4] == 'e' && p[5] == 's' &&
                       p[6] == ']');

            inCodecs = (p[1] == 'C' && p[2] == 'o' && p[3] == 'd' && p[4] == 'e' && p[5] == 'c' &&
                        p[6] == 's' && p[7] == ']');

            while (*p && *p != '\n') {
                ++p;
            }

            continue;
        }

        if (inSizes) {
            f32 value = StrToF32(p);
            if (value != 0.0f || (*p >= '0' && *p <= '9')) {
                f32 oldValue = value;
                value = ClampF32(value, MIN_TARGET_SIZE, MAX_TARGET_SIZE);
                if (oldValue != value) {
                    DEBUG_PRINTF("Clamped %.2f to %.2f\n", oldValue, value);
                }

                if (sizesParsed == sizesCount) {
                    DEBUG_PRINTF("Tried to parse more than %d sizes, SUCCESS\n", sizesCount);
                    break;
                }

                appState->targetSizes[sizesParsed++] = value;
                DEBUG_PRINTF("Parsed size: %.2f\n", value);

                if (!foundDefaultForSize) {
                    const char* start = p;
                    while (*p && *p != '\n') {
                        ++p;
                    }

                    bool32 isDefault = false;
                    for (const char* t = start; t < p; ++t) {
                        if (*t == '!') {
                            isDefault = true;
                            break;
                        }
                    }

                    if (isDefault) {
                        foundDefaultForSize = true;
                        DEBUG_PRINTF("Found default target size of %.2f!\n", value);
                        appState->defaultTargetSize = value;
                    }
                }
            }
        } else if (inCodecs) {
            if (!foundDefaultForCodec) {
                const char* start = p;
                while (*p && *p != '\n') {
                    ++p;
                }

                bool32 isDefault = false;
                for (const char* t = start; t < p; ++t) {
                    if (*t == '!') {
                        isDefault = true;
                        break;
                    }
                }

                // Skip leading whitespace
                const char* end = p;
                const char* t = start;
                while (t < end && *t == ' ') {
                    ++t;
                }

                Codec codec = Codec::NONE;
                if ((end - t) >= 4 && t[0] == 'h' && t[1] == '2' && t[2] == '6' && t[3] == '4') {
                    codec = Codec::H264;
                } else if ((end - t) >= 4 && t[0] == 'h' && t[1] == '2' && t[2] == '6' &&
                           t[3] == '5') {
                    codec = Codec::H265;
                }

                if (codec != Codec::NONE && isDefault) {
                    foundDefaultForCodec = true;
                    DEBUG_PRINTF("Found default codec %s!\n", CodecText_(codec));
                    appState->defaultCodec = codec;
                }
            }
        }

        while (*p && *p != '\n') {
            ++p;
        }
    }

    CloseHandle(file);

    // Having no codecs is NOT considered a failure
    // Having no default codec is also NOT considered a failure
    // This is just for the simplicity and differs heavily from the logic of target sizes
    // Simply: if we don't have the default set via "!", we assign a default
    // TODO: maybe revise the logic here, see above
    if (appState->defaultCodec == Codec::NONE) {
        DEBUG_PRINT("No codec specified, assigning default!\n");
        appState->defaultCodec = Codec::H264;
    }

    // User added some sizes but not the full amount
    // We consider having 0 target sizes a failure
    if (sizesParsed > 0 && sizesParsed < sizesCount) {
        DEBUG_PRINTF("Parsed %d out of %d, meaning SUCCESS\n", sizesParsed, sizesCount);
        if (!foundDefaultForSize) {
            // Set the first user-supplied size as the default
            f32 defaultSize = appState->targetSizes[0];
            DEBUG_PRINTF("Didn't find default, setting it to %.2f\n", defaultSize, 0);
            appState->defaultTargetSize = defaultSize;
        }

        sizesParsed = sizesCount;
    }

    bool32 success = sizesParsed == sizesCount;
    return success;
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE /*unused*/, LPSTR /*unused*/, int /*unused*/) {
    ImGui_ImplWin32_EnableDpiAwareness();
    f32 mainScale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    const char* name = "EasyCompressor";
    WNDCLASSEXA windowClass = { sizeof(windowClass),
                                CS_CLASSDC,
                                WndProc,
                                0,
                                0,
                                hInstance,
                                nullptr,
                                nullptr,
                                nullptr,
                                nullptr,
                                name,
                                nullptr };

    if (!RegisterClassExA(&windowClass)) {
        DEBUG_PRINT("Failed to register windowClass!\n");
        return 0;
    }

    HWND hWnd =
        CreateWindowExA(0, windowClass.lpszClassName, name, WS_OVERLAPPEDWINDOW, 100, 100,
                        static_cast<i32>(1280 * mainScale), static_cast<i32>(800 * mainScale),
                        nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) {
        DEBUG_PRINT("Failed to create windowHandle!\n");
        return 0;
    }

    if (!CreateDeviceD3D(hWnd)) {
        CleanupDeviceD3D();
        return 0;
    }

    ////////////////////////////////////////////////////////////

    DragAcceptFiles(hWnd, TRUE); // enable WM_DROPFILES
    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    /// ImGui

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(gDevice, gContext);

    ImGui::StyleColorsDark();
    // auto would default to a copy instead of a reference... found this out the hard way
    // Didn't know this but I guess it makes sense, why doesn't the compiler warn about it...
    auto& style = ImGui::GetStyle();
    style.ScaleAllSizes(mainScale);
    style.Colors[ImGuiCol_PlotHistogram] = style.Colors[ImGuiCol_SliderGrab];

    auto& io = ImGui::GetIO();
    io.ConfigDpiScaleFonts = true; // Automatically scales fonts for docking branch
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    AppState appState = {};
    gAppState = &appState;

    GetExeDirectory(&appState);
    DEBUG_PRINTF("Exe dir: %s\n", appState.exeDir);
    //GetFFMpegPath(&pathInfo);

    /// Config file

    char appData[MAX_PATH_COUNT];
    if (!SUCCEEDED(SHGetFolderPathA(hWnd, CSIDL_LOCAL_APPDATA, nullptr, 0, appData))) {
        DEBUG_PRINT("Couldn't get user appdata folder, using exe dir as working dir...\n");
        // Use exe dir as working dir...
        snprintf(appState.appData, sizeof(appState.appData), "%s", appState.exeDir);
    } else {
        DEBUG_PRINTF("Found appdata %s\n", appData);
        snprintf(appState.appData, sizeof(appState.appData), "%s\\%s", appData, name);
        // It's okay to fail silently
        BOOL created = CreateDirectoryA(appState.appData, nullptr);
        if (!created) {
            DWORD err = GetLastError();
            if (err != ERROR_ALREADY_EXISTS) {
                // This shouldn't ever happen though
                DEBUG_PRINTF("SHGetFolderPathA returned %s but couldn't create the directory "
                             "there. Using exe dir...\n");
                snprintf(appState.appData, sizeof(appState.appData), "%s", appState.exeDir);
                ASSERT(false);
            }
        }
    }

    char configPath[MAX_PATH_COUNT];
    snprintf(configPath, sizeof(configPath), "%s\\easycompressor.cfg", appState.appData);

    bool32 loaded = LoadConfigFile(&appState, configPath);
    if (!loaded) {
        bool32 created = CreateDefaultConfigFile(configPath);
        if (created) {
            DEBUG_PRINT("Loading just created config file...\n");
            LoadConfigFile(&appState, configPath);
        } else {
            DEBUG_PRINT("Fallback to using default target sizes...\n");
            f32 defaultTargetSizes[5] = { 5.0f, 10.0f, 25.0, 50.0, 100.0f };
            CopyMemory(appState.targetSizes, defaultTargetSizes, sizeof(appState.targetSizes));
            appState.defaultTargetSize = defaultTargetSizes[1];
        }
    }

    // imgui.ini also to same path
    snprintf(configPath, sizeof(configPath), "%s\\imgui.ini", appState.appData);
    io.IniFilename = configPath;

    // TODO: support package managers so read from PATH
    snprintf(appState.ffmpegPath, sizeof(appState.ffmpegPath), "vendor\\ffmpeg\\");

    // Start worker thread
    HANDLE workerThread = CreateThread(nullptr, 0, WorkerThread, &appState, 0, nullptr);
    if (!workerThread) {
        DEBUG_PRINT("Couldn't create WorkerThread");
        return 0;
    }

    appState.workerThread = workerThread;

// Test data
#if COMPRESSOR_INTERNAL
    DEBUG_PRINT("Adding test files at startup...\n");

    char testPath1[MAX_PATH_COUNT];
    char testPath2[MAX_PATH_COUNT];
    char testPath3[MAX_PATH_COUNT];

    snprintf(testPath1, sizeof(testPath1), "%s\\..\\test_file1_large.mp4", appState.exeDir);
    snprintf(testPath2, sizeof(testPath2), "%s\\..\\test_file2.mp4", appState.exeDir);
    snprintf(testPath3, sizeof(testPath3), "%s\\..\\testi_file_small.mp4", appState.exeDir);

    AddJob(&appState, testPath1);
    AddJob(&appState, testPath2);
    AddJob(&appState, testPath3);
#endif

    /// Performance statistics
    LARGE_INTEGER freqCounter;
    QueryPerformanceFrequency(&freqCounter);
    gPerfFreq = freqCounter.QuadPart;

    auto currentCounter = GetWallClock();
    u64 currentCycleCount{ __rdtsc() };

    f32 frameWorkAvgMs = 0.0f;

    bool32 running = true;

    //ImVec4 clearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    while (running) {
        auto frameStart = GetWallClock();
        // For the first frame delta is ~0.0f, which is fine
        f32 deltaMs = GetMsElapsed(currentCounter, frameStart);
        //f32 delta = deltaMs / 1000.0f;
        // We kinda have to clamp the delta as the file dialogs are blocking the main thread
        // so we end up with values like 10 seconds which break the progress bar animations and such
        f32 delta = ClampF32(deltaMs / 1000.0f, 0.0f, 0.05f);
        currentCounter = frameStart;

        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            if (msg.message == WM_QUIT) {
                running = false;
            }
        }

        auto msgEnd = GetWallClock();
        f32 msgMs = GetMsElapsed(frameStart, msgEnd);

        if (!running) {
            break;
        }

        // Handle window being minimized or screen locked
        if (gSwapChainOccluded && gSwap->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            continue;
        }

        gSwapChainOccluded = false;

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        if (gResizeWidth != 0 && gResizeHeight != 0) {
            CleanupRenderTarget();
            gSwap->ResizeBuffers(0, gResizeWidth, gResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            gResizeWidth = gResizeHeight = 0;
            CreateRenderTarget();
        }

        auto uiStart = GetWallClock();
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        /// Input

        // If we use ImGui input it has to be inside ImGui::NewFrame()
        //HandleInput(); ??
        // Now it works with io.ClearInputKeys();
        // Still consider using something like SDL if porting
        bool32 ctrlPressed = ImGui::IsKeyDown(ImGuiKey_LeftCtrl);
        if (ctrlPressed && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            PickInputFiles(hInstance, hWnd, &appState);
            // Otherwise we have to press control + A twice to enter here
            // Likely due to the blocking nature of the function so ImGui gets confused
            // Although it definitely should not get confused...
            io.ClearInputKeys();
        }

        /// Draw

        DrawUi(&appState, hInstance, hWnd, mainScale, delta);
        auto uiEnd = GetWallClock();
        f32 uiMs = GetMsElapsed(uiStart, uiEnd);

        /// Render

        auto renderStart = GetWallClock();
        ImGui::Render();

        //const float clearColorWithAlpha[4] = { clearColor.x * clearColor.w,
        //                                       clearColor.y * clearColor.w,
        //                                       clearColor.z * clearColor.w, clearColor.w };
        gContext->OMSetRenderTargets(1, &gRtv, nullptr);
        //gContext->ClearRenderTargetView(gRtv, clearColorWithAlpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        auto renderEnd = GetWallClock();
        f32 renderMs = GetMsElapsed(renderStart, renderEnd);

        /// Frame work end

        auto frameWorkEnd = GetWallClock();
        f32 frameWorkMs = GetMsElapsed(frameStart, frameWorkEnd);
        // NOTE: in a real system one would not probably do this as this is purely to account for
        // the blocking nature of the file dialogs inside DrawUI
        // I guess this stems from the fact that we are using ImGui and not storing the state it
        // produces anywhere when clicking UI elements.
        // IMPORTANT: If we were to store the state and only after drawing the UI, no matter the
        // blocking nature, we would catch true frame drops and such

        // This is also assuming we correctly scope our profiling timings and not just blindly do
        // frameEnd - frameStart, as there might have been blocking functions along the way
        // But for now these blocking functions only disturb that frame's timing so it's probably
        // fine along with a small alpha value like 0.02f, so we really don't even need the clamp
        // IMPORTANT: Instead probably a wiser choice is to just ignore blatant stalls like 1 second
        // Currently the fps gets skewed for 1 frame but that's it really
        //f32 clampedFrameWorkMs =
        //    ClampF32(frameWorkMs, 0.0f, (1.0f / 60.0f) * 1000.0f); // Clamp to max of 60 fps

        //lastCounter = frameEnd;
        f32 fps = 1000.0f / frameWorkMs;

        // Calculate average frameWorkMs using
        // Exponential smoothing: https://en.wikipedia.org/wiki/Exponential_smoothing
        // Just noticed it's the same as a lerp!
        if (frameWorkMs < 1000.0f) {
            f32 alpha = 0.02f; // Bigger values make the reaction faster
            frameWorkAvgMs = (frameWorkMs * alpha) + ((1.0f - alpha) * frameWorkAvgMs);
        }

        // RDTSC
        u64 endCycleCount = __rdtsc();
        f32 cycleElapsedM =
            static_cast<f32>(endCycleCount - currentCycleCount) / (1000.0f * 1000.0f);
        currentCycleCount = endCycleCount;

        auto presentStart = GetWallClock();
        gSwap->Present(1, 0); // (1, 0) -> vsync, otherwise we hog the cpu quite a lot
        auto presentEnd = GetWallClock();
        f32 presentMs = GetMsElapsed(presentStart, presentEnd);
#if 0
        PRINTF("frame work: %.5f (avg: %.5f) ms | msg: %.5f ms | ui: %.5f ms | render: "
               "%.5f ms | present: %.5f ms\nFPS: %.0f | cycles: %.4f M\n",
               frameWorkMs, frameWorkAvgMs, msgMs, uiMs, renderMs, presentMs, fps, cycleElapsedM);
#else
        (void)(msgMs, uiMs, renderMs, presentMs, fps, cycleElapsedM);
#endif
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hWnd);
    UnregisterClassA(windowClass.lpszClassName, windowClass.hInstance);
    return 0;
}
