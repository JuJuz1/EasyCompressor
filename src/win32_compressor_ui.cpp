//
//   - This file lives in the PLATFORM layer (the .exe), not the DLL.
//   - It owns the queue and the worker thread.
//   - It calls compressor.compress(&memory, &params) per job, exactly like
//     your current main() does.
//
// File drop from Explorer: WM_DROPFILES via DragAcceptFiles().
// Internal reorder: ImGui drag-drop payload between table rows.

// TODO: UNICODE SUPPORT?

#if COMPRESSOR_WIN32
#    define UNICODE
#    define WIN32_LEAN_AND_MEAN
#    define POPEN _popen
#    define PCLOSE _pclose
#    define PATH_SEP '\\'
#    define NULL_DEV "NUL"

#    include <windows.h>

#    include <cderr.h>
#    include <commdlg.h> // GetSaveFileNameA
#    include <d3d11.h>
#    include <io.h>
#    include <process.h>  // _beginthreadex
#    include <shellapi.h> // DragAcceptFiles, DragQueryFileA, DragFinish
#    include <stdio.h>
#    include <tchar.h>

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

//extern "C" {
#include "compressor.h"

#include "win32_compressor.h"
//}

// -----------------------------------------------------------------------------
// Queue model
// -----------------------------------------------------------------------------

static AppState gAppState;

static void
AddJob(const char* path) {
    if (gAppState.jobCount >= MAX_JOBS) {
        OutputDebugStringA("Jobs full!\n");
        return;
    }

    UIJob* j = &gAppState.jobs[gAppState.jobCount++];
    *j = UIJob{};

    j->status = JobStatus::QUEUED;
    j->targetSizeMb = gAppState.defaultTargetSize;

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
        OutputDebugStringA("Couldn't find file extension, constructed default path!\n");
        snprintf(j->output, sizeof(j->output), "%s_compressed", j->input);
    } else {
        i32 extensionLen = StrLength(lastDot);
        i32 inputLen = StrLength(j->input);
        i32 baseLen = inputLen - extensionLen; // Without extension

        char base[MAX_PATH_COUNT];
        memcpy(base, j->input, baseLen);
        base[baseLen] = '\0';

        snprintf(j->output, sizeof(j->output), "%s_compressed%s", base, lastDot);
    }

    char buf[(MAX_PATH_COUNT * 2) + 256];
    snprintf(buf, sizeof(buf),
             "Added job: index = %d, input = %s,\ntarget size = %.2f MB, output = %s\n",
             gAppState.jobCount - 1, j->input, j->targetSizeMb, j->output);
    OutputDebugStringA(buf);
}

static void
RemoveJob(i32 index) {
    ASSERT(index >= 0 && index < gAppState.jobCount);
    if (index < 0 || index >= gAppState.jobCount) {
        // bad!
        return;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "Removed job %d\n", index);
    OutputDebugStringA(buf);

    for (i32 i = index; i < gAppState.jobCount - 1; ++i) {
        gAppState.jobs[i] = gAppState.jobs[i + 1];
    }

    gAppState.jobCount--;
}

//static void
//MoveJob(int from, int to) {
//    if (from == to || from < 0 || to < 0 || from >= gAppState.jobCount ||
//        to >= gAppState.jobCount) {
//        return;
//    }

//    UIJob tmp = gAppState.jobs[from];
//    if (from < to) {
//        for (int i = from; i < to; ++i) {
//            gAppState.jobs[i] = gAppState.jobs[i + 1];
//        }
//    } else {
//        for (int i = from; i > to; --i) {
//            gAppState.jobs[i] = gAppState.jobs[i - 1];
//        }
//    }

//    gAppState.jobs[to] = tmp;
//}

// -----------------------------------------------------------------------------
// Worker thread — runs jobs sequentially. For parallel encoding, spawn N of these
// and have them pop jobs off a shared index with InterlockedIncrement.
// -----------------------------------------------------------------------------

static unsigned __stdcall
WorkerThread(void*) {
    for (i32 i = 0; i < gAppState.jobCount; ++i) {
        if (InterlockedCompareExchange(&gAppState.cancelRequested, 0, 0)) {
            break;
        }

        InterlockedExchange(&gAppState.jobs[i].status, JobStatus::RUNNING);

        //CompressorParams params = {};
        //params.ffmpegPath = gAppState.ffmpegPath;
        //params.input = gAppState.jobs[i].input;
        //params.output = gAppState.jobs[i].output;
        //params.targetSizeMb = gAppState.jobs[i].targetSizeMb;

        // NOTE: hot-reload check belongs HERE (between jobs), never during one.
        // if (DllChangedOnDisk()) ReloadCompressorCode(&gAppState.compress, ...);

        //if (gAppState.compress) {
        //gAppState.compress(&gAppState.memory, &params);
        //Compress();
        InterlockedExchange(&gAppState.jobs[i].status, JobStatus::DONE);
        //}
        //else {
        //    InterlockedExchange(&gAppState.jobs[i].status, static_cast<i32>(JobStatus::ERROR));
        //}
    }

    InterlockedExchange(&gAppState.workerRunning, 0);
    return 0;
}

static void
StartBatch() {
    OutputDebugStringA("Start batch\n");
    for (i32 i = 0; i < gAppState.jobCount; ++i) {
        gAppState.jobs[i].status = JobStatus::RUNNING;
    }

    //if (InterlockedCompareExchange(&gAppState.workerRunning, 1, 0) != 0) {
    //    return; // already running
    //}

    //InterlockedExchange(&gAppState.cancelRequested, 0);
    //for (int i = 0; i < gAppState.jobCount; ++i) {
    //    InterlockedExchange(&gAppState.jobs[i].status, static_cast<i32>(JobStatus::QUEUED));
    //}

    //gAppState.workerThread = (HANDLE)_beginthreadex(nullptr, 0, WorkerThread, nullptr, 0,
    //nullptr);
}

static void
GetExeDirectory(PathInfo* pathInfo) {
    GetModuleFileNameA(0, pathInfo->exeDir, sizeof(pathInfo->exeDir));

    i32 lastSlashIndex = -1;
    char* scan = pathInfo->exeDir;
    for (i32 i = 0; *scan; ++scan, ++i) {
        if (*scan == PATH_SEP) {
            lastSlashIndex = i;
        }
    }

    if (lastSlashIndex >= 0) {
        pathInfo->exeDir[lastSlashIndex + 1] = '\0';
    }
}

static void
OpenInExplorer(HWND hWnd, const char* path) {
    char cmd[MAX_PATH_COUNT];
    snprintf(cmd, sizeof(cmd), "/select,\"%s\"", path);
    ShellExecuteA(hWnd, "open", "explorer.exe", cmd, NULL, SW_SHOWNORMAL);
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
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "mp4";

    // TODO: throws some weird exceptions via explorerframe.dll
    // Error codes 80004001, 80070057
    // Works correctly though
    // TODO: consider using the more modern IFileDialog
    // https://learn.microsoft.com/en-us/windows/win32/shell/common-file-dialog
    // But taking a look at it, it's just so messy compared to this... (not a surprise though)
    if (GetSaveFileNameA(&ofn)) {
        memcpy(outPath, ofn.lpstrFile, MAX_PATH_COUNT);
        char buf[MAX_PATH_COUNT + 32];
        snprintf(buf, sizeof(buf), "Picked new output path: %s\n", outPath);
        OutputDebugStringA(buf);
    } else {
        DWORD err = CommDlgExtendedError();
        char buf[64];
        snprintf(buf, sizeof(buf), "CommDlgExtendedError in PickOutputPath: %u\n", err);
        OutputDebugStringA(buf);

        if (err == FNERR_BUFFERTOOSMALL) {
            OutputDebugStringA("Buffer too small for output path!\n");
            return;
        } else if (err == FNERR_INVALIDFILENAME) {
            OutputDebugStringA("Invalid file name for output path!\n");
            // TODO: handle?
        } else {
            OutputDebugStringA("Cancelled pick output path dialog!\n");
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
        return "Queued";
    case JobStatus::RUNNING:
        return "Running";
    case JobStatus::DONE:
        return "Done";
    case JobStatus::ERROR:
        return "Error";
    }

    return "";
}

static void
SetTargetSizeForAll(f32 targetSize) {
    char buf[128];
    snprintf(buf, sizeof(buf), "Set target size for all %.2f MB\n", targetSize);
    OutputDebugStringA(buf);

    for (i32 i = 0; i < gAppState.jobCount; ++i) {
        UIJob* job = &gAppState.jobs[i];
        if (job->status != JobStatus::RUNNING) {
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

static void
DrawUi(HINSTANCE hInstance, HWND hWnd) {
    UIState* uiState = &gAppState.uiState;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("EasyCompressor", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            //if (ImGui::MenuItem("Add files")) {
            //    OutputDebugStringA("File -> Add files\n");
            //}

            if (ImGui::MenuItem("Exit")) {
                PostQuitMessage(0);
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                uiState->helpAboutClicked = true;
                OutputDebugStringA("About clicked\n");
            }

            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    /// END MENU

    ImGui::TextDisabled("Drop video files anywhere on this window. Max %d", MAX_JOBS);
    ImGui::Separator();

    const i32 sliderWidth = 190;

    /// Default target size

    ImGui::Text("Default target size:");
    f32* defaultTargetSize = &gAppState.defaultTargetSize;
    ImGui::SetNextItemWidth(sliderWidth);
    ImGui::SliderFloat("##mb_slider_default", defaultTargetSize, MIN_TARGET_SIZE, MAX_TARGET_SIZE,
                       "%.1f MB", ImGuiSliderFlags_Logarithmic);

    ImGui::SameLine();
    ImGui::BeginDisabled(gAppState.jobCount == 0);
    if (ImGui::Button("Apply to all files")) {
        SetTargetSizeForAll(gAppState.defaultTargetSize);
    }

    ImGui::EndDisabled();

    ImGui::SetNextItemWidth(sliderWidth);
    ImGui::InputFloat("##mb_input_default", defaultTargetSize, 0.0f, 0.0f, "%.3f MB");
    *defaultTargetSize = ClampF32(*defaultTargetSize, MIN_TARGET_SIZE, MAX_TARGET_SIZE);

    if (ImGui::Button("5 MB##default", ImVec2(80, 0))) {
        *defaultTargetSize = 5.0f;
    }

    ImGui::SameLine();
    if (ImGui::Button("10 MB##default", ImVec2(80, 0))) {
        *defaultTargetSize = 10.0f;
    }

    ImGui::SameLine();
    if (ImGui::Button("50 MB##default", ImVec2(80, 0))) {
        *defaultTargetSize = 50.0f;
    }

    //bool busy = InterlockedCompareExchange(&gAppState.workerRunning, 0, 0) != 0;
    // TODO: necessary or no?
    bool32 busy = false;

    /// Table

    if (ImGui::BeginTable("queue", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn(
            "#", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 20);
        ImGui::TableSetupColumn("Input/Output", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Target MB", ImGuiTableColumnFlags_WidthFixed, sliderWidth);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Remove?", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        //i32 moveFrom = -1, moveTo = -1, removeIdx = -1;
        i32 removeIndex = -1;

        for (i32 i = 0; i < gAppState.jobCount; ++i) {
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", i + 1);

            UIJob* job = &gAppState.jobs[i];

            ImGui::TableSetColumnIndex(1);
            ImGui::Text(job->input);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.7f, 1.0f));
            ImGui::Selectable(job->output);
            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                //ImGui::Text("Input:\n%s", job->input);
                //ImGui::Text("Output:\n%s", job->output);
                //ImGui::Separator();
                ImGui::Text("Click to change output directory\n");
                ImGui::Text("Right click for more options\n");
                ImGui::EndTooltip();
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                PickOutputPath(hInstance, hWnd, job->output);
                // This is done to reset broken hover state after the dialog closes
                ImGui::GetIO().ClearInputMouse();
            } else if (ImGui::BeginPopupContextItem("job_context_menu")) {
                if (ImGui::MenuItem("Open input in explorer...")) {
                    OpenInExplorer(hWnd, job->input);
                }

                if (ImGui::MenuItem("Reset to default output")) {
                    // TODO:
                    //DeriveOutputPath(job->input, job->output, sizeof(job->output));
                }

                ImGui::EndPopup();
            }

            // Drag-drop reorder
            //if (!busy && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
            //    ImGui::SetDragDropPayload("JOB_ROW", &i, sizeof(int));
            //    ImGui::Text("Move %s", gAppState.jobs[i].input);
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

            f32* targetSize = &gAppState.jobs[i].targetSizeMb;
            ImGui::SetNextItemWidth(sliderWidth);
            ImGui::SliderFloat("##mb_slider", targetSize, MIN_TARGET_SIZE, MAX_TARGET_SIZE,
                               "%.1f MB", ImGuiSliderFlags_Logarithmic);

            ImGui::SetNextItemWidth(sliderWidth);
            ImGui::InputFloat("##mb_input", targetSize, 0.0f, 0.0f, "%.3f MB");
            *targetSize = ClampF32(*targetSize, MIN_TARGET_SIZE, MAX_TARGET_SIZE);

            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(StatusText(static_cast<JobStatus>(
                InterlockedCompareExchange(&gAppState.jobs[i].status, 0, 0))));

            ImGui::TableSetColumnIndex(4);
            ImGui::BeginDisabled(job->status == JobStatus::RUNNING);
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
            RemoveJob(removeIndex);
        }
    }

    ImGui::BeginDisabled(busy || gAppState.jobCount == 0);
    if (ImGui::Button("Start", ImVec2(100, 0))) {
        StartBatch();
    }

    ImGui::EndDisabled();

    //ImGui::SameLine();
    //ImGui::BeginDisabled(!busy);
    //if (ImGui::Button("Cancel after current", ImVec2(180, 0))) {
    //    InterlockedExchange(&gAppState.cancelRequested, 1);
    //}

    //ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(busy || gAppState.jobCount == 0);
    if (ImGui::Button("Clear", ImVec2(80, 0))) {
        OutputDebugStringA("Clear\n");
        gAppState.jobCount = 0;
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
        ImGui::Text("EasyCompressor");
        ImGui::Separator();
        ImGui::Text("Built with ImGui");
        ImGui::Text("Max path length for input/output paths is: %d", MAX_PATH_COUNT);

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
// Win32 / DX11 plumbing (boilerplate from the ImGui example, trimmed)
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
    if (res == DXGI_ERROR_UNSUPPORTED) { // Try high-performance WARP software driver if hardware is
                                         // not available.
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
        // TODO: validate by most common video file extensions
        for (i32 i = 0; i < fileCount; ++i) {
            char path[MAX_PATH_COUNT];
            char buf[MAX_PATH_COUNT + 128];
            // Query the required character amount first, not including null terminator
            // If we don't query we have no way of deducing if the path was truncated or it's
            // exactly MAX_PATH_COUNT long
            UINT required = DragQueryFileA(drop, i, nullptr, 0);
            snprintf(buf, sizeof(buf), "Required %d (not including null terminator)\n", required);
            OutputDebugStringA(buf);

            // Get the path and get the copied amount, not including null terminator
            UINT copied = DragQueryFileA(drop, i, path, sizeof(path));
            snprintf(buf, sizeof(buf), "Copied %d (not including null terminator)\n", copied);
            OutputDebugStringA(buf);

            if (required >= sizeof(path)) {
                snprintf(buf, sizeof(buf),
                         "Path was truncated, didn't add job!\nPath would have been %s\n", path);
                OutputDebugStringA(buf);
                // TODO: show error for user for discarded files
                continue;
            }

            AddJob(path);
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

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    ImGui_ImplWin32_EnableDpiAwareness();
    f32 mainScale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    const auto* name = L"EasyCompressor";
    WNDCLASSEXW windowClass = { sizeof(windowClass),
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

    if (!RegisterClassExW(&windowClass)) {
        OutputDebugStringA("Failed to register windowClass!\n");
        return 0;
    }

    HWND hWnd =
        CreateWindowExW(0, windowClass.lpszClassName, name, WS_OVERLAPPEDWINDOW, 100, 100,
                        static_cast<i32>(1280 * mainScale), static_cast<i32>(800 * mainScale),
                        nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) {
        OutputDebugStringA("Failed to create windowHandle!\n");
        return 0;
    }

    if (!CreateDeviceD3D(hWnd)) {
        CleanupDeviceD3D();
        return 0;
    }

    PathInfo pathInfo = { 0 };
    GetExeDirectory(&pathInfo);
    //GetFFMpegPath(&pathInfo);

    // TODO: support package managers so read from PATH
    //char ffmpegPath[64];
    //snprintf(ffmpegPath, sizeof(ffmpegPath), "vendor%cffmpeg%c", PATH_SEP, PATH_SEP);

    ////////////////////////////////////////////////////////////

    DragAcceptFiles(hWnd, TRUE); // <-- enables WM_DROPFILES
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

    gAppState.defaultTargetSize = DEFAULT_TARGET_SIZE;

    // Test data
#if COMPRESSOR_INTERNAL
    OutputDebugStringA("Adding test files at startup...\n");
    char testPath1[MAX_PATH_COUNT];
    char testPath2[MAX_PATH_COUNT];

    snprintf(testPath1, sizeof(testPath1), "%s..\\test_file1_large.mp4", pathInfo.exeDir);
    snprintf(testPath2, sizeof(testPath2), "%s..\\test_file2.mp4", pathInfo.exeDir);

    AddJob(testPath1);
    AddJob(testPath2);
#endif

    bool32 done = false;

    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
        }

        if (done) {
            break;
        }

        // Handle window being minimized or screen locked
        if (gSwapChainOccluded && gSwap->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10);
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

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        DrawUi(hInstance, hWnd);
        ImGui::Render();

        //const float clearColorWithAlpha[4] = { clearColor.x * clearColor.w,
        //                                       clearColor.y * clearColor.w,
        //                                       clearColor.z * clearColor.w, clearColor.w };
        gContext->OMSetRenderTargets(1, &gRtv, nullptr);
        //gContext->ClearRenderTargetView(gRtv, clearColorWithAlpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        gSwap->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hWnd);
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    return 0;
}
