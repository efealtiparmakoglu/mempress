/* mempress.h — controlled memory-pressure generator for system tests. */
#ifndef MEMPRESS_H
#define MEMPRESS_H

#include <stddef.h>
#include <stdio.h>

#define MEMPRESS_VERSION "1.0.0"
#define MEMPRESS_CHUNK_MAX (4u * 1024u * 1024u) /* 4 MiB chunks */

typedef struct {
    size_t target_bytes;     /* required: total to hold resident        */
    double rate_bps;         /* bytes/second growth; 0 = as fast as possible */
    double hold_seconds;     /* stay at target after reaching it        */
    double report_interval;  /* progress line cadence (0 = silent)      */
    int touch;               /* 1 = write + readback each chunk         */
    int keep;                /* 1 = do not free at the end (OOM probe)  */
    int json;                /* final report as JSON                    */
} mempress_config;

typedef struct {
    double elapsed_seconds;
    size_t allocated_bytes;
    double peak_rss_bytes;
    int interrupted;
} mempress_progress;

/* Size/time parsing. "512MB", "1GB", "1024KB", "4096"; "30s","2m","500ms".
 * Return 0 on success, -1 on junk, -2 on overflow. */
int mempress_parse_bytes(const char *text, size_t *out);
int mempress_parse_seconds(const char *text, double *out);

/* Default config; target_bytes stays 0 and must be set. */
void mempress_config_defaults(mempress_config *cfg);

/* argv -> config. Returns 0 ok, 2 usage error (message on stderr). */
int mempress_parse_args(int argc, char **argv, mempress_config *cfg);

/* Wall clock, injectable for tests. */
double mempress_now(void);

/* Run the growth/hold/report cycle. Returns 0 ok, 1 allocation failure,
 * 130 interrupted. Progress lines go to `out`. */
int mempress_run(const mempress_config *cfg, FILE *out,
                 double (*now)(void), volatile int *interrupt_flag);

/* Resident set size in bytes (best effort; 0 when unavailable). */
double mempress_current_rss(void);

#endif
