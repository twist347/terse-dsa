// keys are 0..ENTRIES-1, inserted and looked up in two different shuffles, so neither
// walk follows the order the nodes were allocated in; ENTRIES..2*ENTRIES-1 never go in

#include "tda/algo/permute.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
#include "tda/core/hash.h"
#include "tda/core/rng.h"
#include "tda/core/span.h"
#include "tda/ds/hmap.h"

#include <ubench.h>

#include <stdint.h>
#include <stdlib.h>

static constexpr size_t ENTRIES = 100'000;

static constexpr uint64_t INSERT_SEED = 0x5eed'1234'5678'9abc;
static constexpr uint64_t LOOKUP_SEED = 0x5eed'cba9'8765'4321;

// the arena never frees, so it must fit every node and every bucket array growth leaves
static constexpr size_t ARENA_CAP = 16 * 1024 * 1024;

/// 'first'..'first'+ENTRIES-1 in the order 'seed' shuffles them into; null on no memory
static int32_t *shuffled_keys(int32_t first, uint64_t seed) {
    int32_t *keys = malloc(ENTRIES * sizeof(*keys));
    if (!keys) {
        return nullptr;
    }

    for (size_t i = 0; i < ENTRIES; ++i) {
        keys[i] = first + (int32_t) i;
    }

    tda_Rng rng = tda_rng_from_seed(seed);
    tda_span_shuffle(TDA_SPAN_FROM_DATA_MUT(int32_t, keys, ENTRIES), &rng);

    return keys;
}

/// a map on 'al' holding every key in 'keys'; 'cap' as in tda_hmap_new_cap, 0 for none
static tda_HMap *build(tda_Al *al, size_t cap, const int32_t *keys) {
    tda_HMap *m = nullptr;
    tda_Status st = TDA_HMAP_NEW_CAP(
        int32_t,
        int32_t,
        cap,
        tda_hash_i32,
        tda_eq_i32,
        nullptr,
        al,
        &m
    );
    if (TDA_STATUS_IS_ERR(st)) {
        return nullptr;
    }

    for (size_t i = 0; i < ENTRIES; ++i) {
        st = TDA_HMAP_INSERT(int32_t, int32_t, m, keys[i], keys[i], nullptr);
        if (TDA_STATUS_IS_ERR(st)) {
            tda_hmap_drop(m);
            return nullptr;
        }
    }

    return m;
}

#define BENCH_INSERT(NAME, ARENA, CAP)                                            \
    UBENCH_EX(insert, NAME) {                                                     \
        int32_t *keys = shuffled_keys(0, INSERT_SEED);                            \
        if (!keys) {                                                              \
            return;                                                               \
        }                                                                         \
                                                                                  \
        UBENCH_DO_BENCHMARK() {                                                   \
            tda_Al *arena = ARENA ? tda_al_arena_new(tda_al_default(), ARENA_CAP) \
                                  : nullptr;                                      \
            tda_HMap *m = build(ARENA ? arena : tda_al_default(), CAP, keys);     \
            UBENCH_DO_NOTHING(m);                                                 \
            tda_hmap_drop(m);                                                     \
            tda_al_arena_drop(arena);                                             \
        }                                                                         \
                                                                                  \
        free(keys);                                                               \
    }

BENCH_INSERT(default_grows, false, 0)
BENCH_INSERT(default_reserved, false, ENTRIES)
BENCH_INSERT(arena_grows, true, 0)
BENCH_INSERT(arena_reserved, true, ENTRIES)

// a miss walks its whole bucket, so it is the case the load factor shows in
#define BENCH_FIND(NAME, FIRST)                                          \
    UBENCH_EX(find, NAME) {                                              \
        int32_t *keys = shuffled_keys(0, INSERT_SEED);                   \
        int32_t *lookups = shuffled_keys(FIRST, LOOKUP_SEED);            \
        tda_HMap *m = keys ? build(tda_al_default(), 0, keys) : nullptr; \
        if (!m || !lookups) {                                            \
            tda_hmap_drop(m);                                            \
            free(lookups);                                               \
            free(keys);                                                  \
            return;                                                      \
        }                                                                \
                                                                         \
        UBENCH_DO_BENCHMARK() {                                          \
            size_t found = 0;                                            \
            for (size_t i = 0; i < ENTRIES; ++i) {                       \
                found += TDA_HMAP_CONTAINS(int32_t, m, lookups[i]);      \
            }                                                            \
            UBENCH_DO_NOTHING(&found);                                   \
        }                                                                \
                                                                         \
        tda_hmap_drop(m);                                                \
        free(lookups);                                                   \
        free(keys);                                                      \
    }

BENCH_FIND(hit, 0)
BENCH_FIND(miss, (int32_t) ENTRIES)

#undef BENCH_FIND
#undef BENCH_INSERT

UBENCH_MAIN()
