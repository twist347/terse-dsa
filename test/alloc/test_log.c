#include "tda/alloc/log.h"

#include "support/probe.h"

#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ========== probe allocator ==========
 *
 * The wrapped allocator, the shared probe with all five hooks. Its counters prove that
 * the log layer forwards every operation instead of merely reporting it.
 */

static tda_TestProbe probe;
static FILE *log_stream;

static tda_Al full_al() {
    return tda_test_probe_full(&probe);
}

// a fixed 8, not max_align_t, so the lines checked below read the same on every platform
static tda_Layout bytes(size_t size) {
    return (tda_Layout){.size = size, .align = 8};
}

// snapshot of everything written to the log so far
static void log_text(char *buf, size_t cap) {
    fflush(log_stream);
    rewind(log_stream);

    const size_t n = fread(buf, 1, cap - 1, log_stream);
    buf[n] = '\0';

    fseek(log_stream, 0, SEEK_END);
}

void setUp() {
    tda_test_probe_reset(&probe);
    log_stream = tmpfile();
    TEST_ASSERT_NOT_NULL(log_stream);
}

void tearDown() {
    if (log_stream) {
        fclose(log_stream);
        log_stream = nullptr;
    }
}

/* ========== lifetime ========== */

static void test_init_announces_itself() {
    tda_Al parent = full_al();

    tda_Log log;
    tda_log_init(&parent, log_stream, &log);

    char buf[512];
    log_text(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "log allocator created"));

    tda_log_deinit(&log);
}

static void test_init_provides_every_hook() {
    tda_Al parent = full_al();

    tda_Log log;
    tda_log_init(&parent, log_stream, &log);
    const tda_Al *al = TDA_AL(&log);

    TEST_ASSERT_NOT_NULL(al->alloc);
    TEST_ASSERT_NOT_NULL(al->alloc_zeroed);
    TEST_ASSERT_NOT_NULL(al->resize);
    TEST_ASSERT_NOT_NULL(al->remap);
    TEST_ASSERT_NOT_NULL(al->dealloc);

    tda_log_deinit(&log);
}

// the log is a value the caller holds, so it takes nothing of the wrapped allocator's
static void test_the_log_takes_nothing_of_its_own() {
    tda_Al parent = full_al();

    tda_Log log;
    tda_log_init(&parent, log_stream, &log);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);

    tda_log_deinit(&log);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);

    char buf[512];
    log_text(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "log allocator destroyed"));
}

static void test_deinit_of_an_inert_log_is_noop() {
    tda_Log log = {};
    tda_log_deinit(&log);
    tda_log_deinit(&log);
}

/* ========== forwarding ========== */

// logging is only a side effect; the memory must come from the wrapped allocator
static void test_alloc_is_forwarded_and_usable() {
    tda_Al parent = full_al();
    tda_Log log;
    tda_log_init(&parent, log_stream, &log);

    // the wrapper itself allocated from the probe, so count from here
    const size_t base = probe.alloc_calls;

    unsigned char *p = tda_alloc(TDA_AL(&log), (tda_Layout){.size = 32, .align = 64});
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(base + 1, probe.alloc_calls);
    TEST_ASSERT_EQUAL_size_t(64, probe.last_alloc.align);

    memset(p, 0x6D, 32);
    TEST_ASSERT_EQUAL_UINT8(0x6D, p[31]);

    tda_dealloc(TDA_AL(&log), p, (tda_Layout){.size = 32, .align = 64});
    TEST_ASSERT_EQUAL_size_t(64, probe.last_dealloc.align);
    TEST_ASSERT_EQUAL_size_t(1, probe.dealloc_calls);

    tda_log_deinit(&log);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_alloc_zeroed_is_forwarded() {
    tda_Al parent = full_al();
    tda_Log log;
    tda_log_init(&parent, log_stream, &log);

    unsigned char *p = tda_alloc_zeroed(TDA_AL(&log), bytes(32));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(1, probe.zeroed_calls);
    for (size_t i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, p[i]);
    }

    tda_dealloc(TDA_AL(&log), p, bytes(32));
    tda_log_deinit(&log);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_realloc_is_forwarded_and_keeps_the_contents() {
    tda_Al parent = full_al();
    tda_Log log;
    tda_log_init(&parent, log_stream, &log);

    unsigned char *p = tda_alloc(TDA_AL(&log), bytes(16));
    TEST_ASSERT_NOT_NULL(p);
    for (size_t i = 0; i < 16; ++i) {
        p[i] = (unsigned char) (i + 1);
    }

    // the probe never grows in place, so the growth passes resize on to remap
    unsigned char *q = tda_realloc(TDA_AL(&log), p, bytes(16), 64);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_size_t(1, probe.resize_calls);
    TEST_ASSERT_EQUAL_size_t(1, probe.remap_calls);
    for (size_t i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT8((unsigned char) (i + 1), q[i]);
    }

    tda_dealloc(TDA_AL(&log), q, bytes(64));
    tda_log_deinit(&log);
}

// a failure in the wrapped allocator passes through unchanged
static void test_failure_is_forwarded() {
    tda_Al parent = full_al();
    tda_Log log;
    tda_log_init(&parent, log_stream, &log);

    tda_test_probe_fail_after_next(&probe, 0); // fail from here on
    TEST_ASSERT_NULL(tda_alloc(TDA_AL(&log), bytes(32)));

    probe.fail_after = SIZE_MAX;
    tda_log_deinit(&log);
}

/* ========== the log itself ========== */

static void test_every_operation_is_recorded() {
    tda_Al parent = full_al();
    tda_Log log;
    tda_log_init(&parent, log_stream, &log);

    void *p = tda_alloc(TDA_AL(&log), bytes(32));
    void *q = tda_realloc(TDA_AL(&log), p, bytes(32), 64);
    void *r = tda_realloc(TDA_AL(&log), q, bytes(64), 16); // the probe shrinks in place
    TEST_ASSERT_EQUAL_PTR(q, r);
    tda_dealloc(TDA_AL(&log), r, bytes(16));

    void *c = tda_alloc_zeroed(TDA_AL(&log), bytes(32));
    tda_dealloc(TDA_AL(&log), c, bytes(32));

    char buf[2048];
    log_text(buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "] alloc size = 32 align = 8"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "] alloc_zeroed size = 32 align = 8"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "size = 32 -> 64 align = 8: refused"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "size = 64 -> 16 align = 8: in place"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "] remap "));
    TEST_ASSERT_NOT_NULL(strstr(buf, "] dealloc "));

    tda_log_deinit(&log);
}

// the log is flushed as it goes, so it survives a crash before drop
static void test_the_log_is_flushed_eagerly() {
    tda_Al parent = full_al();
    tda_Log log;
    tda_log_init(&parent, log_stream, &log);

    void *p = tda_alloc(TDA_AL(&log), bytes(128));

    char buf[1024];
    log_text(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "alloc size = 128"));

    tda_dealloc(TDA_AL(&log), p, bytes(128));
    tda_log_deinit(&log);
}

// deinit leaves the inert state, so a second one is safe
static void test_deinit_twice_is_noop() {
    tda_Log x = {};
    tda_log_init(tda_al_default(), stderr, &x);

    tda_log_deinit(&x);
    TEST_ASSERT_NULL(x.al_.alloc);

    tda_log_deinit(&x);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_init_announces_itself);
    RUN_TEST(test_init_provides_every_hook);
    RUN_TEST(test_the_log_takes_nothing_of_its_own);
    RUN_TEST(test_deinit_of_an_inert_log_is_noop);

    RUN_TEST(test_alloc_is_forwarded_and_usable);
    RUN_TEST(test_alloc_zeroed_is_forwarded);
    RUN_TEST(test_realloc_is_forwarded_and_keeps_the_contents);
    RUN_TEST(test_failure_is_forwarded);

    RUN_TEST(test_every_operation_is_recorded);
    RUN_TEST(test_the_log_is_flushed_eagerly);

    RUN_TEST(test_deinit_twice_is_noop);

    return UNITY_END();
}
