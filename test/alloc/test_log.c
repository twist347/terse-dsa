#include "tda/alloc/log.h"

#include "support/probe.h"

#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ========== probe allocator ==========
 *
 * The wrapped allocator, the shared probe with all four hooks. Its counters prove that
 * the log layer forwards every operation instead of merely reporting it.
 */

static tda_TestProbe probe;
static FILE *log_stream;

static tda_Al probe_al() {
    return tda_test_probe_full(&probe);
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

static void test_new_announces_itself() {
    tda_Al parent = probe_al();

    tda_Al *log = tda_al_log_new(&parent, log_stream);
    TEST_ASSERT_NOT_NULL(log);

    char buf[512];
    log_text(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "log allocator created"));

    tda_al_log_drop(log);
}

static void test_new_provides_every_hook() {
    tda_Al parent = probe_al();

    const tda_Al *log = tda_al_log_new(&parent, log_stream);
    TEST_ASSERT_NOT_NULL(log);

    TEST_ASSERT_NOT_NULL(log->alloc);
    TEST_ASSERT_NOT_NULL(log->calloc);
    TEST_ASSERT_NOT_NULL(log->realloc);
    TEST_ASSERT_NOT_NULL(log->dealloc);

    tda_al_log_drop((tda_Al *) log);
}

// the wrapper borrows the allocator it decorates: its own memory comes from there
static void test_drop_returns_everything_to_the_wrapped() {
    tda_Al parent = probe_al();

    tda_Al *log = tda_al_log_new(&parent, log_stream);
    TEST_ASSERT_EQUAL_size_t(2, probe.live); // context and the tda_Al itself

    tda_al_log_drop(log);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);

    char buf[512];
    log_text(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "log allocator destroyed"));
}

static void test_new_cleans_up_after_a_failing_parent() {
    for (size_t fail_at = 0; fail_at < 2; ++fail_at) {
        tda_test_probe_reset(&probe);
        tda_test_probe_fail_after_next(&probe, fail_at);
        tda_Al parent = probe_al();

        TEST_ASSERT_NULL(tda_al_log_new(&parent, log_stream));
        TEST_ASSERT_EQUAL_size_t(0, probe.live);
    }
}

static void test_drop_null_is_noop() {
    tda_al_log_drop(nullptr);
}

/* ========== forwarding ========== */

// logging is only a side effect; the memory must come from the wrapped allocator
static void test_alloc_is_forwarded_and_usable() {
    tda_Al parent = probe_al();
    tda_Al *log = tda_al_log_new(&parent, log_stream);

    // the wrapper itself allocated from the probe, so count from here
    const size_t base = probe.alloc_calls;

    unsigned char *p = tda_alloc(log, 32);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(base + 1, probe.alloc_calls);

    memset(p, 0x6D, 32);
    TEST_ASSERT_EQUAL_UINT8(0x6D, p[31]);

    tda_dealloc(log, p, 32);
    TEST_ASSERT_EQUAL_size_t(1, probe.dealloc_calls);

    tda_al_log_drop(log);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_calloc_is_forwarded() {
    tda_Al parent = probe_al();
    tda_Al *log = tda_al_log_new(&parent, log_stream);

    const unsigned char *p = tda_calloc(log, 4, 8);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(1, probe.calloc_calls);
    for (size_t i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, p[i]);
    }

    tda_dealloc(log, (void *) p, 32);
    tda_al_log_drop(log);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_realloc_is_forwarded_and_keeps_the_contents() {
    tda_Al parent = probe_al();
    tda_Al *log = tda_al_log_new(&parent, log_stream);

    unsigned char *p = tda_alloc(log, 16);
    TEST_ASSERT_NOT_NULL(p);
    for (size_t i = 0; i < 16; ++i) {
        p[i] = (unsigned char) (i + 1);
    }

    unsigned char *q = tda_realloc(log, p, 16, 64);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_size_t(1, probe.realloc_calls);
    for (size_t i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT8((unsigned char) (i + 1), q[i]);
    }

    tda_dealloc(log, q, 64);
    tda_al_log_drop(log);
}

// a failure in the wrapped allocator passes through unchanged
static void test_failure_is_forwarded() {
    tda_Al parent = probe_al();
    tda_Al *log = tda_al_log_new(&parent, log_stream);

    tda_test_probe_fail_after_next(&probe, 0); // fail from here on
    TEST_ASSERT_NULL(tda_alloc(log, 32));

    probe.fail_after = SIZE_MAX;
    tda_al_log_drop(log);
}

/* ========== the log itself ========== */

static void test_every_operation_is_recorded() {
    tda_Al parent = probe_al();
    tda_Al *log = tda_al_log_new(&parent, log_stream);

    void *p = tda_alloc(log, 32);
    void *q = tda_realloc(log, p, 32, 64);
    tda_dealloc(log, q, 64);

    void *c = tda_calloc(log, 4, 8);
    tda_dealloc(log, c, 32);

    char buf[2048];
    log_text(buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "alloc size = 32"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "calloc num = 4 size = 8"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "old size = 32 new_size = 64"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "dealloc"));

    tda_al_log_drop(log);
}

// the log is flushed as it goes, so it survives a crash before drop
static void test_the_log_is_flushed_eagerly() {
    tda_Al parent = probe_al();
    tda_Al *log = tda_al_log_new(&parent, log_stream);

    void *p = tda_alloc(log, 128);

    char buf[1024];
    log_text(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "alloc size = 128"));

    tda_dealloc(log, p, 128);
    tda_al_log_drop(log);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_new_announces_itself);
    RUN_TEST(test_new_provides_every_hook);
    RUN_TEST(test_drop_returns_everything_to_the_wrapped);
    RUN_TEST(test_new_cleans_up_after_a_failing_parent);
    RUN_TEST(test_drop_null_is_noop);

    RUN_TEST(test_alloc_is_forwarded_and_usable);
    RUN_TEST(test_calloc_is_forwarded);
    RUN_TEST(test_realloc_is_forwarded_and_keeps_the_contents);
    RUN_TEST(test_failure_is_forwarded);

    RUN_TEST(test_every_operation_is_recorded);
    RUN_TEST(test_the_log_is_flushed_eagerly);

    return UNITY_END();
}
