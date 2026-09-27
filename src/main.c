/* main.c — mempress CLI. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mempress.h"

static volatile int g_interrupt = 0;

static void on_interrupt(int sig) {
    (void)sig;
    g_interrupt = 1;
}

static void usage(FILE *out) {
    fputs(
        "mempress " MEMPRESS_VERSION
        " — controlled memory-pressure generator for OOM tests\n\n"
        "usage:\n"
        "  mempress --target <SIZE> [options]\n\n"
        "options:\n"
        "  --target SIZE     total memory to hold (512MB, 1GB, 1024KB, bytes)\n"
        "  --rate SIZE/S     growth speed (64MB/s); default: as fast as possible\n"
        "  --hold TIME       stay at target after reaching it (30s, 2m; default 5s)\n"
        "  --report TIME     progress line interval (1s; 0 = silent)\n"
        "  --no-touch        allocate without writing pages (no real pressure)\n"
        "  --keep            do not free at exit (observe reclaim/OOM kill)\n"
        "  --json            final report as JSON\n"
        "  -h | --help       this text\n\n"
        "exit codes: 0 ok, 1 allocation failure, 2 usage error, 130 interrupted\n",
        out);
}

int main(int argc, char **argv) {
    mempress_config cfg;
    mempress_config_defaults(&cfg);

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stderr);
            return 2;
        } else if (strcmp(arg, "--target") == 0 && i + 1 < argc) {
            int rc = mempress_parse_bytes(argv[++i], &cfg.target_bytes);
            if (rc != 0) {
                fprintf(stderr, "mempress: bad size '%s'\n", argv[i]);
                return 2;
            }
        } else if (strcmp(arg, "--rate") == 0 && i + 1 < argc) {
            char *spec = argv[++i];
            size_t slash = 0;
            char unit = 's';
            char buffer[64];
            size_t len = strlen(spec);
            if (len < 3 || len >= sizeof buffer) {
                fprintf(stderr, "mempress: bad rate '%s'\n", spec);
                return 2;
            }
            for (size_t k = 0; k < len; k++) {
                if (spec[k] == '/') {
                    slash = k;
                    break;
                }
            }
            if (slash == 0 || slash != len - 2) {
                fprintf(stderr, "mempress: rate needs SIZE/S form, got '%s'\n", spec);
                return 2;
            }
            unit = spec[len - 1];
            if (unit != 's' && unit != 'S') {
                fprintf(stderr, "mempress: rate unit must be /s, got '%s'\n", spec);
                return 2;
            }
            memcpy(buffer, spec, slash);
            buffer[slash] = '\0';
            size_t per_second = 0;
            if (mempress_parse_bytes(buffer, &per_second) != 0) {
                fprintf(stderr, "mempress: bad rate '%s'\n", spec);
                return 2;
            }
            cfg.rate_bps = (double)per_second;
        } else if (strcmp(arg, "--hold") == 0 && i + 1 < argc) {
            if (mempress_parse_seconds(argv[++i], &cfg.hold_seconds) != 0) {
                fprintf(stderr, "mempress: bad time '%s'\n", argv[i]);
                return 2;
            }
        } else if (strcmp(arg, "--report") == 0 && i + 1 < argc) {
            if (mempress_parse_seconds(argv[++i], &cfg.report_interval) != 0) {
                fprintf(stderr, "mempress: bad time '%s'\n", argv[i]);
                return 2;
            }
        } else if (strcmp(arg, "--no-touch") == 0) {
            cfg.touch = 0;
        } else if (strcmp(arg, "--keep") == 0) {
            cfg.keep = 1;
        } else if (strcmp(arg, "--json") == 0) {
            cfg.json = 1;
        } else if (strcmp(arg, "--version") == 0) {
            puts("mempress " MEMPRESS_VERSION);
            return 0;
        } else {
            fprintf(stderr, "mempress: unknown or incomplete option '%s'\n", arg);
            usage(stderr);
            return 2;
        }
    }

    if (cfg.target_bytes == 0) {
        fprintf(stderr, "mempress: --target is required\n");
        return 2;
    }

    signal(SIGINT, on_interrupt);
    signal(SIGTERM, on_interrupt);
    return mempress_run(&cfg, stdout, mempress_now, &g_interrupt);
}
