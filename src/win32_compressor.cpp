/*
 * compress - target-size video compressor (FFmpeg controller)
 *
 * Usage:
 *   compress <input> <output> <target_size_MB>
 *
 * Strategy:
 *   1. ffprobe -> duration (seconds)
 *   2. compute total_bitrate = (target_size_MB * 8 * 1024 * 1024) / duration [bits/s]
 *   3. audio_bitrate = 128 kbps (clamped down for tiny targets)
 *   4. video_bitrate = total_bitrate - audio_bitrate  (with a safety margin)
 *   5. ffmpeg 2-pass encode with libx264 at the computed video bitrate
 *
 */

// TODO: CLEANUP  ! ! ! !

#if COMPRESSOR_WIN32
#    include <io.h>
#    include <windows.h>
#    define POPEN _popen
#    define PCLOSE _pclose
#    define PATH_SEP '\\'
#    define NULL_DEV "NUL"
//#else
//#    include <limits.h>
//#    include <unistd.h>
//#    define POPEN popen
//#    define PCLOSE pclose
//#    define PATH_SEP '/'
//#    define NULL_DEV "/dev/null"
#endif

#include <stdio.h>

#include <compressor.h>

#include <win32_compressor.h>

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

static void
Exit(const char* msg) {
    Print(LogType::Error, "%s\n", msg);
    exit(1);
}

int
main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr,
                "usage: %s <input> <output> <target_size_MB>\n"
                "example: %s input.mp4 output.mp4 10\n",
                argv[0], argv[0]);
        return 2;
    }

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
    params.input = argv[1];
    params.output = argv[2];

    params.targetSizeMb = atof(argv[3]);
    if (params.targetSizeMb <= 0.0) {
        Exit("Target size must be > 0 MB");
    }

    CompressorCode compressor = LoadCompressorCode(dllPath, tempDllPath, lockFilePath);

    if (compressor.compress) {
        compressor.compress(&memory, &params);
    }

    // TODO: Comparing the newly built dll and loading it, don't load while ffmpeg is running!
    //FILETIME newDllWriteTime = GetLastWriteTime(dllPath);
    //if (CompareFileTime(&compressor.lastWritetime, &newDllWriteTime)) {
    //    UnloadCompressorCode(&compressor);
    //    compressor = LoadCompressorCode(dllPath, tempDllPath, lockFilePath);
    //}

    // TODO: Remove the CLI and replace with UI

    return 0;
}
