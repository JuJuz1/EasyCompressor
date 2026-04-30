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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#    include <io.h>
#    include <windows.h>
#    define POPEN _popen
#    define PCLOSE _pclose
#    define PATH_SEP '\\'
#    define NULL_DEV "NUL"
#else
#    include <limits.h>
#    include <unistd.h>
#    define POPEN popen
#    define PCLOSE pclose
#    define PATH_SEP '/'
#    define NULL_DEV "/dev/null"
#endif

/* --- small utils ---------------------------------------------------------- */

static void
die(const char* msg) {
    fprintf(stderr, "compress: error: %s\n", msg);
    exit(1);
}

/* Quote an argument for a shell command line.
 * Returns malloc'd string. Wraps in double quotes and escapes embedded ".
 * Good enough for file paths on Windows cmd.exe and POSIX /bin/sh. */
static char*
shquote(const char* s) {
    size_t n = strlen(s);
    /* worst case: every char becomes 2, plus 2 quotes + NUL */
    char* out = (char*)malloc(n * 2 + 3);
    if (!out) {
        die("out of memory");
    }
    char* p = out;
    *p++ = '"';
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '"' || c == '\\') {
            *p++ = '\\';
        }
        *p++ = c;
    }
    *p++ = '"';
    *p = '\0';
    return out;
}

/* Run a command, return its trimmed stdout (malloc'd). NULL on failure. */
static char*
run_capture(const char* cmd) {
    FILE* fp = POPEN(cmd, "r");
    if (!fp) {
        return NULL;
    }

    size_t cap = 256, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) {
        PCLOSE(fp);
        return NULL;
    }

    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) {
                free(buf);
                PCLOSE(fp);
                return NULL;
            }
            buf = nb;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    PCLOSE(fp);

    /* trim trailing whitespace */
    while (len > 0 && isspace((unsigned char)buf[len - 1])) {
        buf[--len] = '\0';
    }
    return buf;
}

/* --- core ---------------------------------------------------------------- */

static double
probe_duration(const char* input, const char* ffmpegPath) {
    char* q = shquote(input);
    /* -v error: silence banner; show_entries format=duration; default=noprint_wrappers=1:nokey=1 */
    size_t cmd_len = strlen(q) + 256;
    char* cmd = (char*)malloc(cmd_len);
    if (!cmd) {
        die("out of memory");
    }

    snprintf(cmd, cmd_len,
             "%sffprobe -v error -show_entries format=duration "
             "-of default=noprint_wrappers=1:nokey=1 %s",
             ffmpegPath, q);
    free(q);

    char* out = run_capture(cmd);
    free(cmd);
    if (!out || !*out) {
        free(out);
        die("ffprobe failed (is ffprobe on PATH or next to compress?)");
    }

    double d = atof(out);
    free(out);
    if (d <= 0.0) {
        die("could not determine video duration");
    }
    return d;
}

/* Build & run an ffmpeg command. Returns process exit code. */
static int
run_ffmpeg(const char* cmd) {
    fprintf(stderr, "+ %s\n", cmd);
    int rc = system(cmd);
    return rc;
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

    const char* input = argv[1];
    const char* output = argv[2];
    double target_mb = atof(argv[3]);
    if (target_mb <= 0.0) {
        die("target size must be > 0 MB");
    }

    // TODO: support package managers so read from PATH
    char ffmpegPath[64];
    snprintf(ffmpegPath, sizeof(ffmpegPath), "vendor%cffmpeg%c", PATH_SEP, PATH_SEP);

    /* 1. duration */
    double duration = probe_duration(input, ffmpegPath);
    fprintf(stderr, "duration: %.3f s\n", duration);

    /* 2. bitrate budget (bits/sec). 1 MB = 1024*1024 bytes per the user's intent. */
    double total_bits = target_mb * 1024.0 * 1024.0 * 8.0;
    double total_kbps = (total_bits / duration) / 1000.0;

    /* Reserve audio. Scale down for very small targets so we don't go negative. */
    double audio_kbps = 128.0;
    if (total_kbps < 256.0) {
        audio_kbps = 64.0;
    }
    if (total_kbps < 128.0) {
        audio_kbps = 32.0;
    }

    /* Apply a small safety margin (~7%) so muxer overhead doesn't push us over. */
    double video_kbps = (total_kbps - audio_kbps) * 0.97;
    if (video_kbps < 50.0) {
        die("target size too small for this video duration "
            "(video bitrate would be < 50 kbps)");
    }

    fprintf(stderr, "target: %.2f MB | total: %.1f kbps | video: %.1f kbps | audio: %.1f kbps\n",
            target_mb, total_kbps, video_kbps, audio_kbps);

    /* 3. two-pass encode */
    char* qin = shquote(input);
    char* qout = shquote(output);

    /* Use a passlog file in the current directory; ffmpeg appends -0.log etc. */
    const char* passlog = "compress_2pass";

    /* Pass 1: video only, no audio, output to null muxer. */
    {
        size_t n = strlen(qin) + 512;
        char* cmd = (char*)malloc(n);
        if (!cmd) {
            die("out of memory");
        }

        snprintf(cmd, n,
                 "%sffmpeg -y -hide_banner -loglevel error -stats "
                 "-i %s -c:v libx264 -preset medium -b:v %.0fk "
                 "-pass 1 -passlogfile %s -an -f null %s",
                 ffmpegPath, qin, video_kbps, passlog, NULL_DEV);
        int rc = run_ffmpeg(cmd);
        free(cmd);
        if (rc != 0) {
            free(qin);
            free(qout);
            die("ffmpeg pass 1 failed");
        }
    }

    /* Pass 2: real output, with audio. */
    {
        size_t n = strlen(qin) + strlen(qout) + 512;
        char* cmd = (char*)malloc(n);
        if (!cmd) {
            die("out of memory");
        }
        snprintf(cmd, n,
                 "%sffmpeg -y -hide_banner -loglevel error -stats "
                 "-i %s -c:v libx264 -preset medium -b:v %.0fk "
                 "-pass 2 -passlogfile %s "
                 "-c:a aac -b:a %.0fk -movflags +faststart %s",
                 ffmpegPath, qin, video_kbps, passlog, audio_kbps, qout);
        int rc = run_ffmpeg(cmd);
        free(cmd);
        if (rc != 0) {
            free(qin);
            free(qout);
            die("ffmpeg pass 2 failed");
        }
    }

    free(qin);
    free(qout);

    /* 4. cleanup passlog files (best-effort) */
    //{
    //    char path[512];
    //    snprintf(path, sizeof(path), "%s-0.log", passlog);
    //    remove(path);
    //    snprintf(path, sizeof(path), "%s-0.log.mbtree", passlog);
    //    remove(path);
    //}

    fprintf(stderr, "done -> %s\n", output);
    return 0;
}
