//
//   - This file lives in the PLATFORM layer (the .exe), not the DLL.
//   - It owns the queue and the worker thread.
//   - It calls compressor.compress(&memory, &params) per job, exactly like
//     your current main() does.
//
// File drop from Explorer: WM_DROPFILES via DragAcceptFiles().
// Internal reorder: ImGui drag-drop payload between table rows.

#if COMPRESSOR_WIN32
#    define UNICODE
#    define WIN32_LEAN_AND_MEAN
#    define POPEN _popen
#    define PCLOSE _pclose
#    define PATH_SEP '\\'
#    define NULL_DEV "NUL"

#    include <windows.h>

#    include <d3d11.h>
#    include <io.h>
#    include <process.h>  // _beginthreadex
#    include <shellapi.h> // DragAcceptFiles, DragQueryFileA, DragFinish
#    include <stdio.h>
#    include <tchar.h>

#endif

// TODO: test just unity build

#include "imgui_draw.cpp"
#include "imgui_tables.cpp"
#include "imgui_widgets.cpp"

#include "imgui_demo.cpp"

#include "imgui.cpp"

#include "backends/imgui_impl_dx11.cpp"
#include "backends/imgui_impl_win32.cpp"

//#include "backends/imgui_impl_dx11.h"
//#include "backends/imgui_impl_win32.h"
//#include "imgui.h"

extern "C" {
#include "compressor.h"

#include "win32_compressor.h"
}

// -----------------------------------------------------------------------------
// Queue model
// -----------------------------------------------------------------------------

enum JobStatus : int {
    JOB_QUEUED = 0,
    JOB_RUNNING,
    JOB_DONE,
    JOB_ERROR
};

struct UiJob {
    char input[MAX_PATH_COUNT];
    char output[MAX_PATH_COUNT];
    float targetSizeMb;
    volatile long status;      // JobStatus, written across threads via Interlocked*
    volatile long progressPct; // 0..100, optional (parse from ffmpeg -stats if you want)
};

static const int MAX_JOBS = 10;

struct AppState {
    UiJob jobs[MAX_JOBS];
    int jobCount = 0;
    volatile long workerRunning = 0; // 0/1
    volatile long cancelRequested = 0;
    HANDLE workerThread = nullptr;

    // compressor DLL handles (your existing CompressorCode, simplified here)
    compressor_impl* compress = nullptr;
    Memory memory = {};
    char ffmpegPath[64] = {};
};

static AppState gAppState;

// Default output path: <input>_compressed.mp4 next to the source.
static void
DeriveOutputPath(const char* in, char* out, size_t cap) {
    const char* dot = strrchr(in, '.');
    size_t baseLen = dot ? (size_t)(dot - in) : strlen(in);
    if (baseLen > cap - 32) {
        baseLen = cap - 32;
    }
    memcpy(out, in, baseLen);
    snprintf(out + baseLen, cap - baseLen, "_compressed.mp4");
}

static void
AddJob(const char* path) {
    if (gAppState.jobCount >= MAX_JOBS) {
        return;
    }
    UiJob& j = gAppState.jobs[gAppState.jobCount++];
    memset(&j, 0, sizeof(j));
    snprintf(j.input, sizeof(j.input), "%s", path);
    DeriveOutputPath(j.input, j.output, sizeof(j.output));
    j.targetSizeMb = 10.0f;
    j.status = JOB_QUEUED;
}

static void
RemoveJob(int idx) {
    if (idx < 0 || idx >= gAppState.jobCount) {
        return;
    }
    for (int i = idx; i < gAppState.jobCount - 1; ++i) {
        gAppState.jobs[i] = gAppState.jobs[i + 1];
    }
    gAppState.jobCount--;
}

static void
MoveJob(int from, int to) {
    if (from == to || from < 0 || to < 0 || from >= gAppState.jobCount ||
        to >= gAppState.jobCount) {
        return;
    }
    UiJob tmp = gAppState.jobs[from];
    if (from < to) {
        for (int i = from; i < to; ++i) {
            gAppState.jobs[i] = gAppState.jobs[i + 1];
        }
    } else {
        for (int i = from; i > to; --i) {
            gAppState.jobs[i] = gAppState.jobs[i - 1];
        }
    }
    gAppState.jobs[to] = tmp;
}

// -----------------------------------------------------------------------------
// Worker thread — runs jobs sequentially. For parallel encoding, spawn N of these
// and have them pop jobs off a shared index with InterlockedIncrement.
// -----------------------------------------------------------------------------

static unsigned __stdcall
WorkerThread(void*) {
    for (int i = 0; i < gAppState.jobCount; ++i) {
        if (InterlockedCompareExchange(&gAppState.cancelRequested, 0, 0)) {
            break;
        }

        InterlockedExchange(&gAppState.jobs[i].status, JOB_RUNNING);

        CompressorParams params = {};
        params.ffmpegPath = gAppState.ffmpegPath;
        params.input = gAppState.jobs[i].input;
        params.output = gAppState.jobs[i].output;
        params.targetSizeMb = gAppState.jobs[i].targetSizeMb;

        // NOTE: hot-reload check belongs HERE (between jobs), never during one.
        // if (DllChangedOnDisk()) ReloadCompressorCode(&gAppState.compress, ...);

        if (gAppState.compress) {
            gAppState.compress(&gAppState.memory, &params);
            InterlockedExchange(&gAppState.jobs[i].status, JOB_DONE);
        } else {
            InterlockedExchange(&gAppState.jobs[i].status, JOB_ERROR);
        }
    }
    InterlockedExchange(&gAppState.workerRunning, 0);
    return 0;
}

static void
StartBatch() {
    if (InterlockedCompareExchange(&gAppState.workerRunning, 1, 0) != 0) {
        return; // already running
    }
    InterlockedExchange(&gAppState.cancelRequested, 0);
    for (int i = 0; i < gAppState.jobCount; ++i) {
        InterlockedExchange(&gAppState.jobs[i].status, JOB_QUEUED);
    }
    gAppState.workerThread = (HANDLE)_beginthreadex(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
}

// -----------------------------------------------------------------------------
// DLL stuff
// -----------------------------------------------------------------------------

PRINT_IMPL(Print) {
    va_list args;
    va_start(args, fmt);

    switch (type) {
    case LogType::Info: {
        printf("[INFO]: ");
        vprintf(fmt, args);
    } break;
    case LogType::Warn: {
        printf("[WARN]: ");
        vprintf(fmt, args);
    } break;
    case LogType::Error: {
        printf("[ERROR]: ");
        vprintf(fmt, args);
    } break;
    }

    va_end(args);
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

static FILETIME
GetLastWriteTime(const char* filename) {
    FILETIME lastWriteTime = { 0 };

    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExA(filename, GetFileExInfoStandard, &fileInfo)) {
        lastWriteTime = fileInfo.ftLastWriteTime;
    }

    return lastWriteTime;
}

static CompressorCode
LoadCompressorCode(const char* dllPath, const char* tempDllPath, const char* lockFilePath) {
    CompressorCode compressorCode = { 0 };

    WIN32_FILE_ATTRIBUTE_DATA ignored;
    if (GetFileAttributesExA(lockFilePath, GetFileExInfoStandard, &ignored)) {
        return compressorCode;
    }

    CopyFileA(dllPath, tempDllPath, FALSE);
    compressorCode.dll = LoadLibraryA(tempDllPath);
    compressorCode.lastWritetime = GetLastWriteTime(dllPath);

    if (compressorCode.dll) {
        compressorCode.compress = (compressor_impl*)GetProcAddress(compressorCode.dll, "Compress");

        compressorCode.isValid = compressorCode.compress ? TRUE : FALSE;
    } else {
        printf("Failed to load dll!\n");
    }

    if (!compressorCode.isValid) {
        printf("compressorCode is invalid, function pointers are null!\n");
    }

    return compressorCode;
}

static void
UnloadCompressorCode(CompressorCode* compressorCode) {
    if (compressorCode->dll) {
        FreeLibrary(compressorCode->dll);
        compressorCode->dll = 0;
    }

    compressorCode->compress = 0;
    compressorCode->isValid = FALSE;
}

// -----------------------------------------------------------------------------
// ImGui frame
// -----------------------------------------------------------------------------

static const char*
StatusText(JobStatus s) {
    switch (s) {
    case JOB_QUEUED:
        return "queued";
    case JOB_RUNNING:
        return "running";
    case JOB_DONE:
        return "done";
    case JOB_ERROR:
        return "ERROR";
    }
    return "?";
}

static void
DrawUi() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Compressor", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse);

    bool busy = InterlockedCompareExchange(&gAppState.workerRunning, 0, 0) != 0;

    ImGui::TextDisabled("Drop video files anywhere on this window. Max %d.", MAX_JOBS);
    ImGui::Separator();

    if (ImGui::BeginTable("queue", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 24);
        ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Target MB", ImGuiTableColumnFlags_WidthFixed, 180);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();

        int moveFrom = -1, moveTo = -1, removeIdx = -1;

        for (int i = 0; i < gAppState.jobCount; ++i) {
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", i + 1);

            ImGui::TableSetColumnIndex(1);
            ImGui::Selectable(gAppState.jobs[i].input, false, ImGuiSelectableFlags_SpanAllColumns);

            // Drag-drop reorder
            if (!busy && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
                ImGui::SetDragDropPayload("JOB_ROW", &i, sizeof(int));
                ImGui::Text("Move %s", gAppState.jobs[i].input);
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JOB_ROW")) {
                    moveFrom = *(const int*)p->Data;
                    moveTo = i;
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat("##mb", &gAppState.jobs[i].targetSizeMb, 1.0f, 500.0f, "%.1f MB",
                               ImGuiSliderFlags_Logarithmic);

            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(StatusText(static_cast<JobStatus>(
                InterlockedCompareExchange(&gAppState.jobs[i].status, 0, 0))));

            ImGui::TableSetColumnIndex(4);
            if (!busy && ImGui::SmallButton("X")) {
                removeIdx = i;
            }

            ImGui::PopID();
        }
        ImGui::EndTable();

        if (moveFrom != -1) {
            MoveJob(moveFrom, moveTo);
        }
        if (removeIdx != -1) {
            RemoveJob(removeIdx);
        }
    }

    ImGui::Separator();

    ImGui::BeginDisabled(busy || gAppState.jobCount == 0);
    if (ImGui::Button("Start", ImVec2(120, 0))) {
        StartBatch();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!busy);
    if (ImGui::Button("Cancel after current", ImVec2(180, 0))) {
        InterlockedExchange(&gAppState.cancelRequested, 1);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Clear", ImVec2(80, 0))) {
        gAppState.jobCount = 0;
    }
    ImGui::EndDisabled();

    ImGui::End();
}

// -----------------------------------------------------------------------------
// Win32 / DX11 plumbing (boilerplate from the ImGui example, trimmed)
// -----------------------------------------------------------------------------

static ID3D11Device* gDevice;
static ID3D11DeviceContext* gContext;
static IDXGISwapChain* gSwap;
static ID3D11RenderTargetView* gRtv;

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
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
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
        // Files dragged from Explorer.
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        UINT n = DragQueryFileA(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n && gAppState.jobCount < MAX_JOBS; ++i) {
            char path[MAX_PATH_COUNT];
            DragQueryFileA(drop, i, path, sizeof(path));
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

    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    ImGui_ImplWin32_EnableDpiAwareness();
    f32 mainScale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    const auto* name = L"Compressor";
    WNDCLASSEXW windowClass = { sizeof(windowClass),
                                CS_CLASSDC,
                                WndProc,
                                0L,
                                0L,
                                hInstance,
                                nullptr,
                                nullptr,
                                nullptr,
                                nullptr,
                                name,
                                nullptr };

    if (!RegisterClassExW(&windowClass)) {
        Print(LogType::Error, "Failed to register windowClass!\n");
        return 0;
    }

    HWND hWnd =
        CreateWindowExW(0, windowClass.lpszClassName, name, WS_OVERLAPPEDWINDOW, 100, 100,
                        static_cast<i32>(1280 * mainScale), static_cast<i32>(800 * mainScale),
                        nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) {
        Print(LogType::Error, "Failed to create windowHandle!\n");
        return 0;
    }

    if (!CreateDeviceD3D(hWnd)) {
        CleanupDeviceD3D();
        return 0;
    }

    //i32 desiredSchedulerMS{ 1 };
    //bool32 isSleepGranular{ timeBeginPeriod(desiredSchedulerMS) == TIMERR_NOERROR };

    PathInfo pathInfo = { 0 };
    GetExeDirectory(&pathInfo);

    char dllPath[MAX_PATH_COUNT];
    snprintf(dllPath, sizeof(dllPath), "%scompressor.dll", pathInfo.exeDir);
    char tempDllPath[MAX_PATH_COUNT];
    snprintf(tempDllPath, sizeof(dllPath), "%scompressor_temp.dll", pathInfo.exeDir);

    char lockFilePath[MAX_PATH_COUNT];
    snprintf(lockFilePath, sizeof(dllPath), "%slock.tmp", pathInfo.exeDir);

    Memory memory = { 0 };
    memory.exports.print = Print;

    // TODO: support package managers so read from PATH
    char ffmpegPath[64];
    snprintf(ffmpegPath, sizeof(ffmpegPath), "vendor%cffmpeg%c", PATH_SEP, PATH_SEP);

    CompressorParams params = { 0 };
    params.ffmpegPath = ffmpegPath;
    //params.input = argv[1];
    //params.output = argv[2];

    //params.targetSizeMb = atof(argv[3]);
    //if (params.targetSizeMb <= 0.0) {
    //    Exit("Target size must be > 0 MB");
    //}

    CompressorCode compressor = LoadCompressorCode(dllPath, tempDllPath, lockFilePath);

    //if (compressor.compress) {
    //    compressor.compress(&memory, &params);
    //}

    ////////////////////////////////////////////////////////////

    DragAcceptFiles(hWnd, TRUE); // <-- enables WM_DROPFILES
    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(gDevice, gContext);

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

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::ShowDemoWindow();
        {
            static float f = 0.0f;
            static int counter = 0;
            ImVec4 clearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

            ImGui::Begin(
                "Hello, world!"); // Create a window called "Hello, world!" and append into it.

            ImGui::Text("This is some useful text."); // Display some text (you can use a format
                                                      // strings too)

            ImGui::SliderFloat("float", &f, 0.0f,
                               1.0f); // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color",
                              (float*)&clearColor); // Edit 3 floats representing a color

            if (ImGui::Button("Button")) { // Buttons return true when clicked (most widgets return
                                           // true when edited/activated)
                counter++;
            }
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate,
                        io.Framerate);
            ImGui::End();
        }

        //DrawUi();
        ImGui::Render();

        const float clear[4] = { 0.08f, 0.08f, 0.10f, 1.0f };
        gContext->OMSetRenderTargets(1, &gRtv, nullptr);
        gContext->ClearRenderTargetView(gRtv, clear);
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
