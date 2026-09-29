#include "tda/algo/sort.h"
#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
#include "tda/core/rng.h"
#include "tda/core/span.h"

#include <ubench.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static constexpr size_t LEN = 100'000;
static constexpr size_t BYTES = LEN * sizeof(int32_t);

// one seed for every shape, so two runs sort the very same numbers
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

static void sort_tda(int32_t *a) {
    tda_span_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, a, LEN), tda_cmp_i32);
}

static void sort_tda_stable(int32_t *a) {
    (void) tda_span_sort_stable(TDA_SPAN_FROM_DATA_MUT(int32_t, a, LEN), tda_cmp_i32, tda_al_default());
}

static void sort_libc(int32_t *a) {
    qsort(a, LEN, sizeof(*a), tda_cmp_i32);
}

// the pristine input stays untouched outside the timed loop; only the copy and the sort
// are measured, and both sorters pay the same copy
#define BENCH_SORT(NAME, FILL, SORT)          \
    UBENCH_EX(sort, NAME) {                   \
        int32_t *src = malloc(BYTES);         \
        int32_t *work = malloc(BYTES);        \
        if (!src || !work) {                  \
            free(work);                       \
            free(src);                        \
            return;                           \
        }                                     \
        FILL(src);                            \
                                              \
        UBENCH_DO_BENCHMARK() {               \
            memcpy(work, src, BYTES);         \
            SORT(work);                       \
            UBENCH_DO_NOTHING(work);          \
        }                                     \
                                              \
        free(work);                           \
        free(src);                            \
    }

BENCH_SORT(random_tda, fill_random, sort_tda)
BENCH_SORT(random_stable, fill_random, sort_tda_stable)
BENCH_SORT(random_libc, fill_random, sort_libc)

BENCH_SORT(few_unique_tda, fill_few_unique, sort_tda)
BENCH_SORT(few_unique_stable, fill_few_unique, sort_tda_stable)
BENCH_SORT(few_unique_libc, fill_few_unique, sort_libc)

BENCH_SORT(sorted_tda, fill_sorted, sort_tda)
BENCH_SORT(sorted_stable, fill_sorted, sort_tda_stable)
BENCH_SORT(sorted_libc, fill_sorted, sort_libc)

#undef BENCH_SORT

UBENCH_MAIN()
