# EasyCompress

A simple C CLI that compresses any video to a target file size using FFmpeg

```
compress <input> <output> <target_size_MB>
```

The end goal for this project is to act as a native desktop application replacement for this
compression website I have used https://www.freeconvert.com/video-compressor

## Requirements

TODO: redistribute the binaries with a release build, or offer both with and without?

### FFMpeg

Version used: 8.1

- The program depends on ffmpeg and ffprobe. The easiest way to get these on Windows: from the git
master or release builds section of https://www.gyan.dev/ffmpeg/builds/ Download a full_build of any
kind, such as ffmpeg-2026-04-26-git-4867d251ad-full_build.7z. Make a folder next to compress.exe
named "vendor" and inside that named "ffmpeg". Then copy all the contents of the downloaded zip
"bin" folder (10 items) to "vendor/ffmpeg"

### ImGui

Version used: 1.92.7-docking

- Download from: https://github.com/ocornut/imgui/tags and drop into "vendor/imgui". Make sure
  the folder names are correct, so remove the version

## Build

### Windows

[Visual Studio](https://visualstudio.microsoft.com/) installed

Run x64 Native Tools Command Prompt for VS (version optional)

```
build.bat
```

## Notes / limits

- If the requested size is too small for the video's length (would force video bitrate < 50 kbps),
  the tool aborts rather than produce garbage
- Audio is re-encoded to AAC at 128 kbps (scaled to 64/32 kbps for very small targets)
- Output container is inferred from the output file extension. `.mp4` is the safe default
- Two passlog files (`compress_2pass-0.log*`) are created in the working directory during encoding
  and removed on success
