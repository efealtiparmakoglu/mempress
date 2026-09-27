/* mempress.c — config parsing and the allocation/report engine. */
#include "mempress.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#if defined(__linux__)
#include <unistd.h>
#endif

#include <mach/mach.h> /* macOS RSS; Linux build falls back in main? */

double mempress_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

double mempress_current_rss(void) {
#if defined(__APPLE__)
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &count) == KERN_SUCCESS)
        return (double)info.resident_size;
    return 0.0;
#elif defined(__linux__)
    FILE *fp = fopen("/proc/self/statm", "r");
    if (!fp)
        return 0.0;
    unsigned long total = 0, resident = 0;
    int matched = fscanf(fp, "%lu %lu", &total, &resident);
    fclose(fp);
    if (matched != 2)
        return 0.0;
    long page = sysconf(_SC_PAGESIZE);
    return (double)resident * (double)page;
#else
    return 0.0;
#endif
}

int mempress_parse_bytes(const char *text, size_t *out) {
    if (!text || !*text)
        return -1;
    char *end = NULL;
    double value = strtod(text, &end);
    if (end == text)
        return -1;
    while (*end && isspace((unsigned char)*end))
        end++;
    double multiplier = 1.0;
    if (*end == '\0' || (*end | 0x20) == 'b') {
        multiplier = 1.0;
        if (*end)
            end++;
    } else if ((end[0] | 0x20) == 'k' && (end[1] | 0x20) == 'b') {
        multiplier = 1024.0;
        end += 2;
    } else if ((end[0] | 0x20) == 'm' && (end[1] | 0x20) == 'b') {
        multiplier = 1024.0 * 1024.0;
        end += 2;
    } else if ((end[0] | 0x20) == 'g' && (end[1] | 0x20) == 'b') {
        multiplier = 1024.0 * 1024.0 * 1024.0;
        end += 2;
    } else {
        return -1;
    }
    while (*end && isspace((unsigned char)*end))
        end++;
    if (*end)
        return -1;
    if (value < 0)
        return -1;
    double total = value * multiplier;
    if (total > 1e18 || total > (double)SIZE_MAX)
        return -2;
    *out = (size_t)total;
    return 0;
}

int mempress_parse_seconds(const char *text, double *out) {
    if (!text || !*text)
        return -1;
    char *end = NULL;
    double value = strtod(text, &end);
    if (end == text)
        return -1;
    double multiplier = 1.0;
    if (*end == '\0')
        multiplier = 1.0;
    else if ((end[0] | 0x20) == 's' && end[1] == '\0')
        multiplier = 1.0;
    else if ((end[0] | 0x20) == 'm' && end[1] == '\0')
        multiplier = 60.0;
    else if ((end[0] | 0x20) == 'm' && (end[1] | 0x20) == 's')
        multiplier = 0.001;
    else
        return -1;
    if (value < 0)
        return -1;
    *out = value * multiplier;
    return 0;
}

void mempress_config_defaults(mempress_config *cfg) {
    cfg->target_bytes = 0;
    cfg->rate_bps = 0; /* unlimited */
    cfg->hold_seconds = 5.0;
    cfg->report_interval = 1.0;
    cfg->touch = 1;
    cfg->keep = 0;
    cfg->json = 0;
}

static void print_final(const mempress_config *cfg, FILE *out,
                        const mempress_progress *p) {
    if (cfg->json) {
        fprintf(out,
                "{\"allocated_bytes\":%zu,\"elapsed_seconds\":%.3f,"
                "\"peak_rss_bytes\":%.0f,\"interrupted\":%s}\n",
                p->allocated_bytes, p->elapsed_seconds, p->peak_rss_bytes,
                p->interrupted ? "true" : "false");
        return;
    }
    fprintf(out,
            "final: allocated %.1f MiB in %.2fs, peak RSS %.1f MiB%s\n",
            (double)p->allocated_bytes / (1024.0 * 1024.0), p->elapsed_seconds,
            p->peak_rss_bytes / (1024.0 * 1024.0),
            p->interrupted ? " (interrupted)" : "");
}

int mempress_run(const mempress_config *cfg, FILE *out,
                 double (*now)(void), volatile int *interrupt_flag) {
    if (cfg->target_bytes == 0)
        return 1;

    double start = now();
    double last_report = start;
    size_t allocated = 0;
    size_t peak = 0;
    void **chunks = NULL;
    size_t chunk_count = 0;
    size_t chunk_cap = 0;
    int rc = 0;

    while (allocated < cfg->target_bytes) {
        if (interrupt_flag && *interrupt_flag) {
            rc = 130;
            break;
        }
        size_t remaining = cfg->target_bytes - allocated;
        size_t chunk = remaining < MEMPRESS_CHUNK_MAX ? remaining : MEMPRESS_CHUNK_MAX;
        void *block = malloc(chunk);
        if (!block) {
            fprintf(stderr, "mempress: allocation failed at %.1f MiB\n",
                    (double)allocated / (1024.0 * 1024.0));
            rc = 1;
            break;
        }
        if (cfg->touch) {
            unsigned char *bytes = block;
            memset(bytes, (int)(chunk_count & 0xFF), chunk);
            if (bytes[0] != (chunk_count & 0xFF) ||
                bytes[chunk - 1] != (chunk_count & 0xFF)) {
                fprintf(stderr, "mempress: readback mismatch (pages not resident?)\n");
                free(block);
                rc = 1;
                break;
            }
        }
        if (chunk_count == chunk_cap) {
            chunk_cap = chunk_cap ? chunk_cap * 2 : 64;
            void **grown = realloc(chunks, chunk_cap * sizeof *grown);
            if (!grown) {
                free(block);
                rc = 1;
                break;
            }
            chunks = grown;
        }
        chunks[chunk_count++] = block;
        allocated += chunk;
        if (allocated > peak)
            peak = allocated;

        if (cfg->rate_bps > 0) {
            double target_time = start + (double)allocated / cfg->rate_bps;
            double sleep_for = target_time - now();
            if (sleep_for > 0) {
                struct timespec req = {(time_t)sleep_for,
                                       (long)((sleep_for - (double)(time_t)sleep_for) * 1e9)};
                nanosleep(&req, NULL);
            }
        }
        double current = now();
        if (cfg->report_interval > 0 && current - last_report >= cfg->report_interval) {
            last_report = current;
            fprintf(out, "progress: %.1f / %.1f MiB at %.1fs, RSS %.1f MiB\n",
                    (double)allocated / (1024.0 * 1024.0),
                    (double)cfg->target_bytes / (1024.0 * 1024.0),
                    current - start, mempress_current_rss() / (1024.0 * 1024.0));
        }
    }

    double target_reached = now();
    if (rc == 0 && cfg->hold_seconds > 0) {
        while (now() - target_reached < cfg->hold_seconds) {
            if (interrupt_flag && *interrupt_flag) {
                rc = 130;
                break;
            }
            double current = now();
            if (cfg->report_interval > 0 &&
                current - last_report >= cfg->report_interval) {
                last_report = current;
                fprintf(out, "holding: %.1f MiB, RSS %.1f MiB, %.1fs left\n",
                        (double)allocated / (1024.0 * 1024.0),
                        mempress_current_rss() / (1024.0 * 1024.0),
                        cfg->hold_seconds - (current - target_reached));
            }
            struct timespec req = {0, 20 * 1000 * 1000}; /* 20 ms */
            nanosleep(&req, NULL);
        }
    }

    if (!(cfg->keep && rc == 0)) {
        for (size_t i = 0; i < chunk_count; i++)
            free(chunks[i]);
    }
    free(chunks);

    mempress_progress p = {now() - start, allocated, (double)peak,
                           interrupt_flag && *interrupt_flag};
    print_final(cfg, out, &p);
    return rc;
}
