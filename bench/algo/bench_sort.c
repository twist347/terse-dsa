#include "tda/algo/heap.h"
#include "tda/algo/sort.h"
#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
#include "tda/core/rng.h"
#include "tda/core/span.h"

#include <ubench.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// qsort's comparator takes two arguments, the library's three
static int qsort_cmp_i32(const void *lhs, const void *rhs) {
    return tda_cmp_i32(lhs, rhs, nullptr);
}

static constexpr size_t LEN = 100'000;
static constexpr size_t BYTES = LEN * sizeof(int32_t);

// one seed for every shape, so runs sort the same numbers
static constexpr uint64_t SEED = 0x5eed'1234'5678'9abc;

static void fill_random(int32_t *a) {
    tda_Rng rng = tda_rng_from_seed(SEED);
    for (size_t i = 0; i < LEN; ++i) {
        a[i] = (int32_t) tda_rng_u32(&rng);
    }
}

static void fill_few_unique(int32_t *a) {
    tda_Rng rng = tda_rng_from_seed(SEED);
    for (size_t i = 0; i < LEN; ++i) {
        a[i] = (int32_t) tda_rng_u32_max(&rng, 2); // 0, 1 or 2
    }
}

static void fill_sorted(int32_t *a) {
    for (size_t i = 0; i < LEN; ++i) {
        a[i] = (int32_t) i;
    }
}

static void fill_reversed(int32_t *a) {
    for (size_t i = 0; i < LEN; ++i) {
        a[i] = (int32_t) (LEN - i);
    }
}

static void sort_quick(int32_t *a) {
    tda_span_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, a, LEN), tda_cmp_i32, nullptr);
}

static void sort_stable(int32_t *a) {
    (void) tda_span_sort_stable(TDA_SPAN_FROM_DATA_MUT(int32_t, a, LEN), tda_cmp_i32, nullptr, tda_al_default());
}

// guaranteed n log n whatever the shape, the floor quicksort has to beat
static void sort_heap(int32_t *a) {
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, a, LEN);
    tda_span_make_heap(s, tda_cmp_i32, nullptr);
    tda_span_sort_heap(s, tda_cmp_i32, nullptr);
}

static void sort_libc(int32_t *a) {
    qsort(a, LEN, sizeof(*a), qsort_cmp_i32);
}

// only the copy and the sort are timed, and both sorters pay for the copy
#define BENCH_SORT(NAME, FILL, SORT)   \
    UBENCH_EX(sort, NAME) {            \
        int32_t *src = malloc(BYTES);  \
        int32_t *work = malloc(BYTES); \
        if (!src || !work) {           \
            free(work);                \
            free(src);                 \
            return;                    \
        }                              \
        FILL(src);                     \
                                       \
        UBENCH_DO_BENCHMARK() {        \
            memcpy(work, src, BYTES);  \
            SORT(work);                \
            UBENCH_DO_NOTHING(work);   \
        }                              \
                                       \
        free(work);                    \
        free(src);                     \
    }

BENCH_SORT(random_quick, fill_random, sort_quick)
BENCH_SORT(random_stable, fill_random, sort_stable)
BENCH_SORT(random_heap, fill_random, sort_heap)
BENCH_SORT(random_libc, fill_random, sort_libc)

BENCH_SORT(few_unique_quick, fill_few_unique, sort_quick)
BENCH_SORT(few_unique_stable, fill_few_unique, sort_stable)
BENCH_SORT(few_unique_heap, fill_few_unique, sort_heap)
BENCH_SORT(few_unique_libc, fill_few_unique, sort_libc)

BENCH_SORT(sorted_quick, fill_sorted, sort_quick)
BENCH_SORT(sorted_stable, fill_sorted, sort_stable)
BENCH_SORT(sorted_heap, fill_sorted, sort_heap)
BENCH_SORT(sorted_libc, fill_sorted, sort_libc)

BENCH_SORT(reversed_quick, fill_reversed, sort_quick)
BENCH_SORT(reversed_stable, fill_reversed, sort_stable)
BENCH_SORT(reversed_heap, fill_reversed, sort_heap)
BENCH_SORT(reversed_libc, fill_reversed, sort_libc)

#undef BENCH_SORT

UBENCH_MAIN()
