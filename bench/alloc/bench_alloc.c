// BLOCKS small blocks of one size, the case a pool is built for; libc is malloc and free
// called directly, so "libc" against "default" is the cost of going through tda_Al

#include "tda/algo/permute.h"
#include "tda/alloc/alloc.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/alloc/pool.h"
#include "tda/core/rng.h"
#include "tda/core/span.h"

#include <ubench.h>

#include <stdint.h>
#include <stdlib.h>

static constexpr size_t BLOCKS = 10'000;
static constexpr size_t BLOCK_SIZE = 32;
static constexpr uint64_t SEED = 0x5eed'1234'5678'9abc;

static void *ptrs[BLOCKS];
static size_t order[BLOCKS];

/// 0..BLOCKS-1 shuffled, the order the shuffled_batch cases free in
static void fill_order() {
    for (size_t i = 0; i < BLOCKS; ++i) {
        order[i] = i;
    }

    tda_Rng rng = tda_rng_from_seed(SEED);
    tda_span_shuffle(TDA_SPAN_FROM_DATA_MUT(size_t, order, BLOCKS), &rng);
}

// at malloc's own alignment, so every allocator is asked what libc gives
static constexpr tda_Layout BLOCK = {.size = BLOCK_SIZE, .align = alignof(max_align_t)};

// every pattern is written once against these, so libc and tda_Al run the same loop
#define ALLOC(al) ((al).vt ? tda_alloc((al), BLOCK) : malloc(BLOCK_SIZE))
#define DEALLOC(al, ptr) ((al).vt ? tda_dealloc((al), (ptr), BLOCK) : free(ptr))

/// one block taken and given back, BLOCKS times: the free list never grows past one
static void churn(tda_Al al) {
    for (size_t i = 0; i < BLOCKS; ++i) {
        void *ptr = ALLOC(al);
        UBENCH_DO_NOTHING(ptr);
        DEALLOC(al, ptr);
    }
}

/// BLOCKS blocks taken, then given back in the order they came
static void batch(tda_Al al) {
    for (size_t i = 0; i < BLOCKS; ++i) {
        ptrs[i] = ALLOC(al);
    }
    UBENCH_DO_NOTHING(ptrs);
    for (size_t i = 0; i < BLOCKS; ++i) {
        DEALLOC(al, ptrs[i]);
    }
}

/// as batch, but given back shuffled and then taken and given back once more, so the
/// second round walks a free list in no particular order
static void shuffled_batch(tda_Al al) {
    for (size_t i = 0; i < BLOCKS; ++i) {
        ptrs[i] = ALLOC(al);
    }
    for (size_t i = 0; i < BLOCKS; ++i) {
        DEALLOC(al, ptrs[order[i]]);
    }
    batch(al);
}

#define BENCH_LIBC(PATTERN)      \
    UBENCH_EX(PATTERN, libc) {   \
        fill_order();            \
        UBENCH_DO_BENCHMARK() {  \
            PATTERN((tda_Al){}); \
        }                        \
    }

#define BENCH_DEFAULT(PATTERN)         \
    UBENCH_EX(PATTERN, default) {      \
        fill_order();                  \
        UBENCH_DO_BENCHMARK() {        \
            PATTERN(tda_al_default()); \
        }                              \
    }

// built once outside the timed loop: tda_pool_init_cap threads every block, @bigo{n}
#define BENCH_POOL(PATTERN)                                                                 \
    UBENCH_EX(PATTERN, pool) {                                                              \
        fill_order();                                                                       \
        tda_Pool pool;                                                                      \
        if (TDA_STATUS_IS_ERR(tda_pool_init_cap(BLOCKS, BLOCK, tda_al_default(), &pool))) { \
            return;                                                                         \
        }                                                                                   \
        UBENCH_DO_BENCHMARK() {                                                             \
            PATTERN(tda_pool_to_al(&pool));                                                 \
        }                                                                                   \
        tda_pool_deinit(&pool);                                                             \
    }

// dealloc is a no-op, so the reset that takes everything back is timed with the pattern
#define BENCH_ARENA(PATTERN)                                                              \
    UBENCH_EX(PATTERN, arena) {                                                           \
        fill_order();                                                                     \
        tda_Arena arena;                                                                  \
        if (TDA_STATUS_IS_ERR(                                                            \
                tda_arena_init_cap(2 * BLOCKS * BLOCK_SIZE, tda_al_default(), &arena))) { \
            return;                                                                       \
        }                                                                                 \
        UBENCH_DO_BENCHMARK() {                                                           \
            PATTERN(tda_arena_to_al(&arena));                                             \
            tda_arena_reset(&arena);                                                      \
        }                                                                                 \
        tda_arena_deinit(&arena);                                                         \
    }

BENCH_LIBC(churn)
BENCH_DEFAULT(churn)
BENCH_POOL(churn)
BENCH_ARENA(churn)

BENCH_LIBC(batch)
BENCH_DEFAULT(batch)
BENCH_POOL(batch)
BENCH_ARENA(batch)

BENCH_LIBC(shuffled_batch)
BENCH_DEFAULT(shuffled_batch)
BENCH_POOL(shuffled_batch)
BENCH_ARENA(shuffled_batch)

#undef BENCH_ARENA
#undef BENCH_POOL
#undef BENCH_DEFAULT
#undef BENCH_LIBC
#undef DEALLOC
#undef ALLOC

UBENCH_MAIN()
