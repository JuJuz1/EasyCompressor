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
#    define NULL_DEV "NU"

#    include <windows.h>

#    include <cderr.h>   // CommDlg errors
#    include <commdlg.h> // OFN, GetSaveFileNameA
#    include <process.h> // _beginthreadex

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

static void
PRINTF(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    OutputDebugStringA(buf);
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
    //*j = UIJob{}; Compiler error when using /O2
    ZeroMemory(j, sizeof(j));

    j->status = JobStatus::QUEUED;
    j->targetSizeMb = appState->defaultTargetSize;

    snprintf(j->input, sizeof(j->input), "%s", path);

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

// -----------------------------------------------------------------------------
// Worker thread, runs jobs sequentially. For parallel encoding, spawn N of these
// and have them pop jobs off a shared index with InterlockedIncrement
// -----------------------------------------------------------------------------

static void
RunProbe(AppState* appState, UIJob* job, i32 i) {
    ASSERT(job->status == JobStatus::QUEUED);
    job->status = JobStatus::RUNNING_PROBE;

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;

    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        DEBUG_PRINT("Couldn't create pipe! Aborting all jobs!\n");
        job->status = JobStatus::ERROR;
        return;
    }

    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    char cmd[(MAX_PATH_COUNT * 2) + 128];
    snprintf(cmd, sizeof(cmd),
             "\"%sffprobe.exe\" -v error -show_entries format=duration "
             "-of default=noprint_wrappers=1:nokey=1 \"%s\"",
             appState->ffmpegPath, job->input);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    PROCESS_INFORMATION pi = {};

    DEBUG_PRINTF("Creating process ffprobe for job %d\n", i);
    // TODO: handle exiting the program more controlled
    // Now Windows decides if the process should finish or not
    BOOL created = CreateProcessA(nullptr, cmd, nullptr, nullptr,
                                  TRUE, // inherit handles!
                                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(writePipe);

    if (!created) {
        DEBUG_PRINT("Couldn't create process ffprobe! Aborting all jobs!\n");
        job->status = JobStatus::ERROR;
        CloseHandle(readPipe);
        return;
    }

    // Read output
    char buffer[256] = {};
    DWORD bytesRead = 0;

    char output[256] = {};
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

    DEBUG_PRINT("Waiting for ffprobe...\n");
    WaitForSingleObject(pi.hProcess, INFINITE);
    DEBUG_PRINT("ffprobe finished\n");

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode == 0) {
        // Parse duration
        job->durationSeconds = static_cast<f32>(atof(output));

        //job->progressPct = 100;

        job->status = JobStatus::DONE_PROBE;
    } else {
        job->status = JobStatus::ERROR;
    }
}

static void
RunCompress(AppState* appState, UIJob* job, i32 i) {
    ASSERT(job->status == JobStatus::DONE_PROBE);
    job->status = JobStatus::RUNNING_COMPRESS;

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
                    "(video bitrate would be < 50 kbps)");
        // TODO: cancel? and continue
    }

    {
        DEBUG_PRINTF("Target: %.2f MB | total: %.1f kbps | video * %.2f: %.1f kbps | audio: %.1f "
                     "kbps\n",
                     job->targetSizeMb, totalKbps, multiplier, videoKbps, audioKbps);
    }

    // Pass 1
    char cmd[(MAX_PATH_COUNT * 2) + 128];
    // TODO: different presets and h.264 and 265
    snprintf(cmd, sizeof(cmd),
             "%sffmpeg -y -hide_banner -loglevel error -stats "
             "-i %s -c:v libx264 -preset medium -b:v %.0fk "
             "-pass 1 -an -f null %s",
             //"-pass 1 -passlogfile %s -an -f null %s",
             appState->ffmpegPath, job->input, videoKbps, NULL_DEV);

    STARTUPINFOA si1 = {};
    si1.cb = sizeof(si1);
    //si.dwFlags = STARTF_USESTDHANDLES;
    //si.hStdOutput = writePipe;
    //si.hStdError = writePipe;

    PROCESS_INFORMATION pi1 = {};

    DEBUG_PRINTF("Creating process ffmpeg pass 1 for job %d\n", i);
    BOOL created1 = CreateProcessA(nullptr, cmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   nullptr, &si1, &pi1);

    //CloseHandle(writePipe);

    if (!created1) {
        DEBUG_PRINT("Couldn't create process ffmpeg! Aborting all jobs!\n");
        job->status = JobStatus::ERROR;
        //CloseHandle(readPipe);
        return;
    }

    //char buffer[256] = {};
    //DWORD bytesRead = 0;

    //char output[256] = {};
    //DWORD totalRead = 0;

    //while (ReadFile(readPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) &&
    //       bytesRead > 0) {
    //    if (totalRead + bytesRead < sizeof(output)) {
    //        CopyMemory(output + totalRead, buffer, bytesRead);
    //        totalRead += bytesRead;
    //    } else {
    //        DEBUG_PRINT("No space in buffer for ffmpeg output!\n");
    //    }
    //}

    //CloseHandle(readPipe);

    DEBUG_PRINT("Waiting for ffmpeg pass 1...\n");
    WaitForSingleObject(pi1.hProcess, INFINITE);
    DEBUG_PRINT("ffmpeg finished pass 1\n");

    DWORD exitCode1 = 0;
    GetExitCodeProcess(pi1.hProcess, &exitCode1);

    CloseHandle(pi1.hProcess);
    CloseHandle(pi1.hThread);

    if (exitCode1 != 0) {
        job->status = JobStatus::ERROR;
        DEBUG_PRINT("Exit code != 0 pass 1");
        return;
    }

    //job->status = JobStatus::DONE_COMPRESS;
    //job->status = JobStatus::DONE_COMPRESS_PASS1; ?

    // Pass 2
    snprintf(cmd, sizeof(cmd),
             "%sffmpeg -y -hide_banner -loglevel error -stats "
             "-i %s -c:v libx264 -preset medium -b:v %.0fk "
             "-pass 2 "
             //"-pass 2 -passlogfile %s "
             "-c:a aac -b:a %.0fk -movflags +faststart %s",
             appState->ffmpegPath, job->input, videoKbps //, passLog
             ,
             audioKbps, job->output);

    STARTUPINFOA si2 = {};
    si2.cb = sizeof(si2);
    //si.dwFlags = STARTF_USESTDHANDLES;
    //si.hStdOutput = writePipe;
    //si.hStdError = writePipe;

    PROCESS_INFORMATION pi2 = {};

    DEBUG_PRINTF("Creating process ffmpeg pass 2 for job %d\n", i);
    BOOL created2 = CreateProcessA(nullptr, cmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   nullptr, &si2, &pi2);

    //CloseHandle(writePipe);

    if (!created2) {
        DEBUG_PRINT("Couldn't create process ffmpeg! Aborting all jobs!\n");
        job->status = JobStatus::ERROR;
        //CloseHandle(readPipe);
        return;
    }

    //char buffer[256] = {};
    //DWORD bytesRead = 0;

    //char output[256] = {};
    //DWORD totalRead = 0;

    //while (ReadFile(readPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) &&
    //       bytesRead > 0) {
    //    if (totalRead + bytesRead < sizeof(output)) {
    //        CopyMemory(output + totalRead, buffer, bytesRead);
    //        totalRead += bytesRead;
    //    } else {
    //        DEBUG_PRINT("No space in buffer for ffmpeg output!\n");
    //    }
    //}

    //CloseHandle(readPipe);

    DEBUG_PRINT("Waiting for ffmpeg pass 2...\n");
    WaitForSingleObject(pi2.hProcess, INFINITE);
    DEBUG_PRINT("ffmpeg finished pass 2\n");

    DWORD exitCode2 = 0;
    GetExitCodeProcess(pi2.hProcess, &exitCode2);

    CloseHandle(pi2.hProcess);
    CloseHandle(pi2.hThread);

    if (exitCode2 != 0) {
        job->status = JobStatus::ERROR;
        DEBUG_PRINT("Exit code != 0 pass 2");
        return;
    }

    job->status = JobStatus::DONE_COMPRESS;

    // TODO: We could probably get this from ffmpeg output also, but much easier this way
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExA(job->output, GetFileExInfoStandard, &fileInfo)) {
        u64 bytes = (static_cast<u64>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow;
        job->resultFileSize = static_cast<f32>(bytes) / (1024.0f * 1024.0f);
    } else {
        DEBUG_PRINT("Failed to get file size!\n");
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
                RunProbe(appState, job, i);
            }
        }

        if (_InterlockedCompareExchange(&appState->compressing, 0, 0)) {
            jobCount = static_cast<i32>(_InterlockedCompareExchange(&appState->jobCount, 0, 0));
            for (i32 i = 0; i < jobCount; ++i) {
                if (_InterlockedCompareExchange(&appState->cancelRequested, 0, 1)) {
                    DEBUG_PRINT("Cancelled compressing...");
                    break;
                }

                UIJob* job = &appState->jobs[i];
                // Allow compressing again
                // TODO: should the default be this so every file gets compressed again?
                // OR the user can choose if just compressed files should be skipped or compressed
                // again. This covers both the cases of adding files when compressing and not
                // compressing
                if (job->status == JobStatus::DONE_PROBE ||
                    job->status == JobStatus::DONE_COMPRESS) {
                    RunCompress(appState, job, i);
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
    GetModuleFileNameA(0, appState->exeDir, sizeof(appState->exeDir));

    i32 lastSlashIndex = -1;
    char* scan = appState->exeDir;
    for (i32 i = 0; *scan; ++scan, ++i) {
        if (*scan == PATH_SEP) {
            lastSlashIndex = i;
        }
    }

    if (lastSlashIndex >= 0) {
        appState->exeDir[lastSlashIndex + 1] = '\0';
    }
}

static void
OpenInExplorer(HWND hWnd, const char* path) {
    char cmd[MAX_PATH_COUNT];
    snprintf(cmd, sizeof(cmd), "/select,\"%s\"", path);
    ShellExecuteA(hWnd, "open", "explorer.exe", cmd, nullptr, SW_SHOWNORMAL);
}

static void
PickOutputPath(HINSTANCE hInstance, HWND hWnd, char* outPath) {
    OPENFILENAMEA ofn = {};
    char buffer[MAX_PATH_COUNT] = {};

    ofn.lStructSize = sizeof(ofn);

    ofn.hwndOwner = hWnd;
    ofn.hInstance = hInstance;

    ofn.lpstrFilter = "MP4 Video (*.mp4)\0*.mp4\0All Files\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.lpstrTitle = "Select output file";
    ofn.nMaxFile = MAX_PATH_COUNT;
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
            return;
        } else if (err == FNERR_INVALIDFILENAME) {
            DEBUG_PRINT("Invalid file name for output path!\n");
            // TODO: handle?
        } else {
            DEBUG_PRINT("Cancelled pick output path dialog!\n");
        }
    }
}

// -----------------------------------------------------------------------------
// ImGui
// -----------------------------------------------------------------------------

static const char*
StatusText(JobStatus s) {
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
    } else if (value > max) {
        return max;
    }

    return value;
}

// TODO: allow canceling the current compression also, ending the compression?
static void
CancelAfterCurrent(AppState* appState) {
    _InterlockedExchange(&appState->cancelRequested, 1);
    DEBUG_PRINT("Cancel after current pressed!\n");
}

static void
DrawUi(AppState* appState, HINSTANCE hInstance, HWND hWnd) {
    UIState* uiState = &appState->uiState;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("EasyCompressor", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Add files")) {
                DEBUG_PRINT("File -> Add files\n");
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

    const i32 sliderWidth = 190;

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

    ImGui::SetNextItemWidth(sliderWidth);
    ImGui::InputFloat("##mb_input_default", defaultTargetSize, 0.0f, 0.0f, "%.2f MB");
    *defaultTargetSize = ClampF32(*defaultTargetSize, MIN_TARGET_SIZE, MAX_TARGET_SIZE);

    // TODO: read from config file and be able to save a preset for the default?
    if (ImGui::Button("5 MB##default", ImVec2(80, 0))) {
        *defaultTargetSize = 5.0f;
    }

    ImGui::SameLine();
    if (ImGui::Button("10 MB##default", ImVec2(80, 0))) {
        *defaultTargetSize = 10.0f;
    }

    ImGui::SameLine();
    if (ImGui::Button("25 MB##default", ImVec2(80, 0))) {
        *defaultTargetSize = 25.0f;
    }

    ImGui::SameLine();
    if (ImGui::Button("50 MB##default", ImVec2(80, 0))) {
        *defaultTargetSize = 50.0f;
    }

    ImGui::SameLine();
    if (ImGui::Button("100 MB##default", ImVec2(80, 0))) {
        *defaultTargetSize = 100.0f;
    }

    /// Table
    // TODO: allow sorting based on input/output?
    // This is just an idea and in no means a necessary feature
    // At least for file info (size or duration or both???, both might not be feasible or even
    // necessary), target size and status

    if (ImGui::BeginTable("jobs", 6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn(
            "#", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 20);
        ImGui::TableSetupColumn("Input/Output", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("File info", ImGuiTableColumnFlags_WidthFixed, 127);
        ImGui::TableSetupColumn("Target size", ImGuiTableColumnFlags_WidthFixed, sliderWidth);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 168);
        ImGui::TableSetupColumn("Remove?", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        //i32 moveFrom = -1, moveTo = -1, removeIdx = -1;
        i32 removeIndex = -1;

        for (i32 i = 0; i < appState->jobCount; ++i) {
            UIJob* job = &appState->jobs[i];

            bool32 jobRunning = job->status == JobStatus::RUNNING_PROBE ||
                                job->status == JobStatus::RUNNING_COMPRESS;
            compressing |= jobRunning;

            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", i + 1);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(job->input);

            ImGui::BeginDisabled(jobRunning);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.7f, 1.0f));
            ImGui::Selectable(job->output);
            ImGui::PopStyleColor();

            ImGui::EndDisabled();

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                //ImGui::Text("Input:\n%s", job->input);
                //ImGui::Text("Output:\n%s", job->output);
                //ImGui::Separator();
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
                OpenInExplorer(hWnd, job->input);
            } else if (ImGui::BeginPopupContextItem("job_context_menu")) {
                if (ImGui::MenuItem("Open input in explorer...")) {
                    OpenInExplorer(hWnd, job->input);
                }

                // TODO: more?
                //if (ImGui::MenuItem("Reset to default output")) {
                //DeriveOutputPath(job->input, job->output, sizeof(job->output));
                //}

                ImGui::EndPopup();
            }

            // Drag-drop reorder
            //if (!busy && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover))
            //{
            //    ImGui::SetDragDropPayload("JOB_ROW", &i, sizeof(int));
            //    ImGui::Text("Move %s", appState->jobs[i].input);
            //    ImGui::EndDragDropSource();
            //}

            //if (ImGui::BeginDragDropTarget()) {
            //    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JOB_ROW")) {
            //        moveFrom = *(const i32*)p->Data;
            //        moveTo = i;
            //    }

            //    ImGui::EndDragDropTarget();
            //}

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
                ImGui::TextUnformatted(
                    "Warning: target size greater than file size! File will not be compressed!");
                ImGui::EndTooltip();
            }

            ImGui::SetNextItemWidth(sliderWidth);
            ImGui::InputFloat("##mb_input", targetSize, 0.0f, 0.0f, "%.2f MB");
            if (tooLargeBefore && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(
                    "Warning: target size greater than file size! File will not be compressed!");
                ImGui::EndTooltip();
            }

            if (tooLargeBefore) {
                ImGui::PopStyleColor(5);
            }

            *targetSize = ClampF32(*targetSize, MIN_TARGET_SIZE, MAX_TARGET_SIZE);
            ImGui::EndDisabled();

            ImGui::TableSetColumnIndex(4);

            const char* statusText = StatusText(static_cast<JobStatus>(job->status));
            ImGui::TextUnformatted(statusText);
            if (job->status == JobStatus::DONE_COMPRESS) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Open")) {
                    OpenInExplorer(hWnd, job->output);
                }

                ImGui::Text("Result size: %.1f MB", job->resultFileSize);
            }

            // TODO: show progress on probe duration and compression
            //if (job->durationSeconds != 0.0f) {
            //    ImGui::Text("Duration: %.1f s", job->durationSeconds);
            //} else {
            //    ImGui::TextUnformatted("Calculating...");
            //}

            ImGui::TableSetColumnIndex(5);
            // TODO: If we use compressing here
            // we disable removing any file while compressing any file
            // It would be a bit tricky to allow only removing newly added jobs if they are added
            // during compressing. Current workflow is canceling the compressing and then removing
            // them, which is fine
            ImGui::BeginDisabled(compressing || jobRunning);
            if (ImGui::SmallButton("X")) {
                removeIndex = i;
            }

            ImGui::EndDisabled();
            ImGui::PopID();
        }

        ImGui::EndTable();

        //if (moveFrom != -1) {
        //    MoveJob(moveFrom, moveTo);
        //}

        if (removeIndex != -1) {
            RemoveJob(appState, removeIndex);
        }
    }

    ImGui::BeginDisabled(compressing || noJobs);
    if (ImGui::Button("Start", ImVec2(100, 0))) {
        StartBatch(appState);
    }

    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!compressing || (compressing && appState->jobCount == 1) || cancelled ||
                         noJobs);
    if (ImGui::Button("Cancel after current")) {
        CancelAfterCurrent(appState);
    }

    ImGui::EndDisabled();
    ImGui::Separator();

    ImGui::BeginDisabled(compressing || noJobs);
    if (ImGui::Button("Clear", ImVec2(80, 0))) {
        DEBUG_PRINT("Clear\n");
        appState->jobCount = 0;
    }

    ImGui::EndDisabled();
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

void
CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    gSwap->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    gDevice->CreateRenderTargetView(pBackBuffer, nullptr, &gRtv);
    pBackBuffer->Release();
}

void
CleanupRenderTarget() {
    if (gRtv) {
        gRtv->Release();
        gRtv = nullptr;
    }
}

bool
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

void
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
        for (i32 i = 0; i < fileCount && i < MAX_JOBS; ++i) {
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
    }

    case WM_SIZE:
        if (gDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            gSwap->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }

        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

// Frequency
static i64 gPerfFreq;

static LARGE_INTEGER
GetWallClock() {
    LARGE_INTEGER result;
    QueryPerformanceCounter(&result);
    return result;
}

static f64
GetMsElapsed(LARGE_INTEGER start, LARGE_INTEGER end) {
    f64 result = (static_cast<f64>(end.QuadPart - start.QuadPart) / gPerfFreq) * 1000.0;
    return result;
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
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
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(gDevice, gContext);

    //ImVec4 clearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    AppState appState = {};
    //GetFFMpegPath(&pathInfo);

    appState.defaultTargetSize = DEFAULT_TARGET_SIZE;
    gAppState = &appState;

    // TODO: support package managers so read from PATH
    snprintf(appState.ffmpegPath, sizeof(appState.ffmpegPath), "vendor\\ffmpeg\\");

    // Start worker thread
    HANDLE workerThread = CreateThread(0, 0, WorkerThread, &appState, 0, 0);
    if (!workerThread) {
        DEBUG_PRINT("Couldn't create WorkerThread");
        return 0;
    }

    appState.workerThread = workerThread;

    // Test data
#if COMPRESSOR_INTERNAL
    DEBUG_PRINT("Adding test files at startup...\n");
    GetExeDirectory(&appState);

    char testPath1[MAX_PATH_COUNT];
    char testPath2[MAX_PATH_COUNT];

    snprintf(testPath1, sizeof(testPath1), "%s..\\test_file1_large.mp4", appState.exeDir);
    snprintf(testPath2, sizeof(testPath2), "%s..\\test_file2.mp4", appState.exeDir);

    AddJob(&appState, testPath1);
    AddJob(&appState, testPath2);
#endif

    /// Performance statistics
    LARGE_INTEGER freqCounter;
    QueryPerformanceFrequency(&freqCounter);
    gPerfFreq = freqCounter.QuadPart;

    auto lastCounter = GetWallClock();
    u64 lastCycleCount{ __rdtsc() };

    bool32 running = true;

    while (running) {
        auto frameStart = GetWallClock();

        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            if (msg.message == WM_QUIT) {
                running = false;
            }
        }

        auto msgEnd = GetWallClock();
        f64 msgMs = GetMsElapsed(frameStart, msgEnd);

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

        DrawUi(&appState, hInstance, hWnd);
        auto uiEnd = GetWallClock();
        f64 uiMs = GetMsElapsed(uiStart, uiEnd);

        auto renderStart = GetWallClock();
        ImGui::Render();

        //const float clearColorWithAlpha[4] = { clearColor.x * clearColor.w,
        //                                       clearColor.y * clearColor.w,
        //                                       clearColor.z * clearColor.w, clearColor.w };
        gContext->OMSetRenderTargets(1, &gRtv, nullptr);
        //gContext->ClearRenderTargetView(gRtv, clearColorWithAlpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        auto renderEnd = GetWallClock();
        f64 renderMs = GetMsElapsed(renderStart, renderEnd);

        /// Frame work end

        auto frameEnd = GetWallClock();
        f64 frameMs = GetMsElapsed(frameStart, frameEnd);
        //lastCounter = frameEnd;
        f64 fps = 1000.0 / frameMs;

        // RDTSC
        u64 endCycleCount = __rdtsc();
        f64 cycleElapsedM = static_cast<f64>(endCycleCount - lastCycleCount) / (1000 * 1000);
        lastCycleCount = endCycleCount;

        auto presentStart = GetWallClock();
        gSwap->Present(1, 0); // (1, 0) -> vsync, otherwise we hog the cpu quite a lot
        auto presentEnd = GetWallClock();
        f64 presentMs = GetMsElapsed(presentStart, presentEnd);

#if 0
        PRINTF("frame: %.5f ms | msg: %.5f ms | ui: %.5f ms | render: %.5f ms | present: "
               "%.5f ms\nFPS: %.0f | cycles: %.4f M\n",
               frameMs, msgMs, uiMs, renderMs, presentMs, fps, cycleElapsedM);
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
