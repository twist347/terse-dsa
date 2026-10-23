#include "tda/tda.h"

#include "support/status.h"

#include <unity.h>

#include <stdint.h>

void setUp() {
}

void tearDown() {
}

// the umbrella header must reach every module: one left out of it fails the build here,
// not in someone else's project
static void test_the_umbrella_reaches_every_module() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024); // alloc
    TEST_ASSERT_NOT_NULL(arena);

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_OF(int32_t, arena, &v, 5, 3, 1)); // ds

    tda_span_sort(tda_vec_to_span_mut(v), tda_cmp_i32, nullptr); // algo, core

    size_t idx;
    TEST_ASSERT_TRUE(tda_span_binary_search(tda_vec_to_span(v), &(int32_t){5}, tda_cmp_i32, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(2, idx);

    TEST_ASSERT_EQUAL_STRING("TDA_STATUS_OK", tda_status_to_str(TDA_STATUS_OK)); // core

    tda_vec_drop(v);
    tda_al_arena_drop(arena);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_the_umbrella_reaches_every_module);

    return UNITY_END();
}
