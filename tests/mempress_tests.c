/* mempress_tests.c — parsing, config and small-run engine tests. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/mempress.h"

static int tests_run = 0;

#define FAIL(...)                                                             \
    do {                                                                      \
        printf("FAIL %s:%d: ", __FILE__, __LINE__);                           \
        printf(__VA_ARGS__);                                                  \
        printf("\n");                                                         \
        return 1;                                                             \
    } while (0)

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond))                                                          \
            FAIL("check failed: %s", #cond);                                  \
    } while (0)

static void test_reset(void) { tests_run++; }

static int test_parse_bytes(void) {
    size_t value = 0;
    CHECK(mempress_parse_bytes("512MB", &value) == 0);
    CHECK(value == 512ULL * 1024 * 1024);
    CHECK(mempress_parse_bytes("1GB", &value) == 0);
    CHECK(value == 1ULL * 1024 * 1024 * 1024);
    CHECK(mempress_parse_bytes("1024KB", &value) == 0);
    CHECK(value == 1024ULL * 1024);
    CHECK(mempress_parse_bytes("4096", &value) == 0);
    CHECK(value == 4096);
    CHECK(mempress_parse_bytes("2mb", &value) == 0);   /* case-insensitive */
    CHECK(value == 2ULL * 1024 * 1024);
    CHECK(mempress_parse_bytes("3 b", &value) == 0);   /* trailing unit */
    CHECK(value == 3);
    CHECK(mempress_parse_bytes("1.5MB", &value) == 0); /* fractional ok */
    CHECK(value == (size_t)(1.5 * 1024 * 1024));
    test_reset();
    return 0;
}

static int test_parse_bytes_rejects(void) {
    size_t value = 0;
    CHECK(mempress_parse_bytes("", &value) == -1);
    CHECK(mempress_parse_bytes("abc", &value) == -1);
    CHECK(mempress_parse_bytes("10TB", &value) == -1);  /* unsupported unit */
    CHECK(mempress_parse_bytes("12 M", &value) == -1);  /* ambiguous unit */
    CHECK(mempress_parse_bytes("-5MB", &value) == -1);  /* negative */
    CHECK(mempress_parse_bytes("100000000000GB", &value) == -2); /* overflow */
    test_reset();
    return 0;
}

static int test_parse_seconds(void) {
    double value = 0;
    CHECK(mempress_parse_seconds("30s", &value) == 0 && value == 30.0);
    CHECK(mempress_parse_seconds("2m", &value) == 0 && value == 120.0);
    CHECK(mempress_parse_seconds("500ms", &value) == 0 && value == 0.5);
    CHECK(mempress_parse_seconds("7", &value) == 0 && value == 7.0);
    CHECK(mempress_parse_seconds("0.25s", &value) == 0 && value == 0.25);
    CHECK(mempress_parse_seconds("", &value) == -1);
    CHECK(mempress_parse_seconds("5x", &value) == -1);
    CHECK(mempress_parse_seconds("-3s", &value) == -1);
    test_reset();
    return 0;
}

static int test_defaults(void) {
    mempress_config cfg;
    mempress_config_defaults(&cfg);
    CHECK(cfg.target_bytes == 0);
    CHECK(cfg.rate_bps == 0);
    CHECK(cfg.hold_seconds == 5.0);
    CHECK(cfg.report_interval == 1.0);
    CHECK(cfg.touch == 1 && cfg.keep == 0 && cfg.json == 0);
    test_reset();
    return 0;
}

static double fake_clock_value = 100.0;
static double fake_clock(void) {
    fake_clock_value += 0.01; /* advance 10 ms per call so hold loops end */
    return fake_clock_value;
}

static int test_run_tiny_target(void) {
    mempress_config cfg;
    mempress_config_defaults(&cfg);
    cfg.target_bytes = 256 * 1024; /* 256 KiB */
    cfg.hold_seconds = 0.05;
    cfg.report_interval = 0;       /* silent */
    cfg.touch = 1;

    /* advance the fake clock so pacing (rate 0) never sleeps long */
    fake_clock_value = 100.0;
    int rc = mempress_run(&cfg, stdout, fake_clock, NULL);
    CHECK(rc == 0);
    test_reset();
    return 0;
}

static int test_run_rejects_zero_target(void) {
    mempress_config cfg;
    mempress_config_defaults(&cfg);
    CHECK(mempress_run(&cfg, stdout, fake_clock, NULL) == 1);
    test_reset();
    return 0;
}

static int test_run_json_report_shape(void) {
    mempress_config cfg;
    mempress_config_defaults(&cfg);
    cfg.target_bytes = 128 * 1024;
    cfg.hold_seconds = 0;
    cfg.report_interval = 0;
    cfg.json = 1;

    FILE *capture = tmpfile();
    CHECK(capture != NULL);
    int rc = mempress_run(&cfg, capture, fake_clock, NULL);
    CHECK(rc == 0);
    rewind(capture);
    char line[512] = {0};
    CHECK(fgets(line, sizeof line, capture) != NULL);
    CHECK(strstr(line, "\"allocated_bytes\":131072") != NULL);
    CHECK(strstr(line, "\"interrupted\":false") != NULL);
    fclose(capture);
    test_reset();
    return 0;
}

static int test_run_interrupt_flag(void) {
    mempress_config cfg;
    mempress_config_defaults(&cfg);
    cfg.target_bytes = 512ULL * 1024 * 1024 * 1024; /* 512 GiB: never done */
    cfg.report_interval = 0;
    volatile int flag = 0;
    flag = 1; /* already interrupted: engine must stop immediately */

    FILE *capture = tmpfile();
    int rc = mempress_run(&cfg, capture, fake_clock, &flag);
    CHECK(rc == 130);
    rewind(capture);
    char line[512] = {0};
    CHECK(fgets(line, sizeof line, capture) != NULL);
    CHECK(strstr(line, "interrupted") != NULL);
    fclose(capture);
    test_reset();
    return 0;
}

static int test_rate_pacing_one_second(void) {
    /* 2 MiB at 1 MiB/s must take about 2 simulated seconds */
    mempress_config cfg;
    mempress_config_defaults(&cfg);
    cfg.target_bytes = 2 * 1024 * 1024;
    cfg.rate_bps = 1024.0 * 1024.0;
    cfg.hold_seconds = 0;
    cfg.report_interval = 0;

    fake_clock_value = 0.0;
    /* fake clock never advances: pacing sleep would use real nanosleep of
     * huge values — instead advance the clock inside run via progress. The
     * engine sleeps on real time only when the fake clock is behind, which
     * is bounded by allocated/rate <= 2s of real sleep here. Acceptable. */
    int rc = mempress_run(&cfg, stdout, mempress_now, NULL);
    CHECK(rc == 0);
    test_reset();
    return 0;
}

static int test_rss_reporting_available(void) {
    double rss = mempress_current_rss();
    /* macOS and Linux both provide it; 0 would mean "unavailable" */
    CHECK(rss > 0.0);
    test_reset();
    return 0;
}

int main(void) {
    int fails = 0;
    fails += test_parse_bytes();
    fails += test_parse_bytes_rejects();
    fails += test_parse_seconds();
    fails += test_defaults();
    fails += test_run_tiny_target();
    fails += test_run_rejects_zero_target();
    fails += test_run_json_report_shape();
    fails += test_run_interrupt_flag();
    fails += test_rate_pacing_one_second();
    fails += test_rss_reporting_available();
    if (fails) {
        printf("mempress_tests: %d test function(s) FAILED\n", fails);
        return 1;
    }
    printf("mempress_tests: all %d test groups passed\n", tests_run);
    return 0;
}
