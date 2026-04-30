#include <compressor.h>

#include <stdio.h>
#include <stdlib.h>

#if COMPRESSOR_WIN32
#    define POPEN _popen
#    define PCLOSE _pclose
#    define NULL_DEV "NUL"
#endif

/* Quote an argument for a shell command line.
 * Returns malloc'd string. Wraps in f64 quotes and escapes embedded ".
 * Good enough for file paths on Windows cmd.exe and POSIX /bin/sh. */
//static char*
//shquote(const char* s) {
//    size_t n = strlen(s);
//    /* worst case: every char becomes 2, plus 2 quotes + NUL */
//    char* out = (char*)malloc(n * 2 + 3);
//    if (!out) {
//        printf("out of memory");
//    }

//    char* p = out;
//    *p++ = '"';
//    for (size_t i = 0; i < n; i++) {
//        char c = s[i];
//        if (c == '"' || c == '\\') {
//            *p++ = '\\';
//        }
//        *p++ = c;
//    }
//    *p++ = '"';
//    *p = '\0';
//    return out;
//}

/* Run a command, return its trimmed stdout (malloc'd). -1 on failure. */
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

    i32 c;
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

typedef struct F64ErrorMsg {
    const char* errorMsg;
    f64 value;
} F64ErrorMsg;

static F64ErrorMsg
probe_duration(const char* input, const char* ffmpegPath) {
    //char* q = shquote(input);
    /* -v error: silence banner; show_entries format=duration; default=noprint_wrappers=1:nokey=1 */
    //size_t cmd_len = strlen(q) + 256;
    //char* cmd = (char*)malloc(cmd_len);
    //if (!cmd) {
    //    printf("out of memory");
    //}

    F64ErrorMsg ret = { .value = -1 };

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "%sffprobe -v error -show_entries format=duration "
             "-of default=noprint_wrappers=1:nokey=1 %s",
             ffmpegPath, input);
    //free(q);

    char* out = run_capture(cmd);
    //free(cmd);
    if (!out || !*out) {
        free(out);
        ret.errorMsg = "ffprobe failed (is ffprobe on PATH or next to compress?)";
        return ret;
    }

    f64 d = atof(out);
    free(out);
    if (d <= 0.0) {
        ret.errorMsg = "could not determine video duration";
    }

    ret.value = d;
    return ret;
}

/* Build & run an ffmpeg command. Returns process exit code. */
static i32
run_ffmpeg(const char* cmd) {
    fprintf(stderr, "+ %s\n", cmd);
    i32 rc = system(cmd);
    return rc;
}

COMPRESS_IMPL(Compress) {
    /* 1. duration */
    F64ErrorMsg duration = probe_duration(params->input, params->ffmpegPath);
    if (duration.value == -1) {
        printf(duration.errorMsg);
        return 0;
    }

    fprintf(stderr, "duration: %.3f s\n", duration.value);

    /* 2. bitrate budget (bits/sec). 1 MB = 1024*1024 bytes per the user's intent. */
    f64 total_bits = params->targetSizeMb * 1024.0 * 1024.0 * 8.0;
    f64 total_kbps = (total_bits / duration.value) / 1000.0;

    /* Reserve audio. Scale down for very small targets so we don't go negative. */
    f64 audio_kbps = 128.0;
    if (total_kbps < 256.0) {
        audio_kbps = 64.0;
    }
    if (total_kbps < 128.0) {
        audio_kbps = 32.0;
    }

    // TODO: 4-5% is enough, 3% might be cutting it too close
    /* Apply a small safety margin (~3%) so muxer overhead doesn't push us over. */
    f64 video_kbps = (total_kbps - audio_kbps) * 0.97;
    if (video_kbps < 50.0) {
        printf("target size too small for this video duration "
               "(video bitrate would be < 50 kbps)");
    }

    fprintf(stderr, "target: %.2f MB | total: %.1f kbps | video: %.1f kbps | audio: %.1f kbps\n",
            params->targetSizeMb, total_kbps, video_kbps, audio_kbps);

    /* 3. two-pass encode */
    //char* qin = shquote(params->input);
    //char* qout = shquote(params->output);
    const char* qin = params->input;
    const char* qout = params->output;

    /* Use a passlog file in the current directory; ffmpeg appends -0.log etc. */
    const char* passlog = "compress_2pass";

    /* Pass 1: video only, no audio, output to null muxer. */
    {
        //size_t n = strlen(qin) + 512;
        //char* cmd = (char*)malloc(n);
        //if (!cmd) {
        //    printf("out of memory\n");
        //    return 0;
        //}

        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "%sffmpeg -y -hide_banner -loglevel error -stats "
                 "-i %s -c:v libx264 -preset medium -b:v %.0fk "
                 "-pass 1 -passlogfile %s -an -f null %s",
                 params->ffmpegPath, qin, video_kbps, passlog, NULL_DEV);

        i32 rc = run_ffmpeg(cmd);
        //free(cmd);
        if (rc != 0) {
            //free(qin);
            //free(qout);
            printf("ffmpeg pass 1 failed");
            return 0;
        }
    }

    /* Pass 2: real output, with audio. */
    {
        //size_t n = strlen(qin) + strlen(qout) + 512;
        //char* cmd = (char*)malloc(n);
        //if (!cmd) {
        //    printf("out of memory");
        //}

        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "%sffmpeg -y -hide_banner -loglevel error -stats "
                 "-i %s -c:v libx264 -preset medium -b:v %.0fk "
                 "-pass 2 -passlogfile %s "
                 "-c:a aac -b:a %.0fk -movflags +faststart %s",
                 params->ffmpegPath, qin, video_kbps, passlog, audio_kbps, qout);

        i32 rc = run_ffmpeg(cmd);
        //free(cmd);
        if (rc != 0) {
            //free(qin);
            //free(qout);
            printf("ffmpeg pass 2 failed");
            return 0;
        }
    }

    //free(qin);
    //free(qout);

    /* 4. cleanup passlog files (best-effort) */
    //{
    //    char path[512];
    //    snprintf(path, sizeof(path), "%s-0.log", passlog);
    //    remove(path);
    //    snprintf(path, sizeof(path), "%s-0.log.mbtree", passlog);
    //    remove(path);
    //}

    // TODO: show the compressed file size

    fprintf(stderr, "done -> %s\n", params->output);
    return 0;
}
