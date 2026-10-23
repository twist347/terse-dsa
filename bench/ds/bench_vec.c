// every benchmark pushes the same elems; "grows" minus "reserved" is the cost of growth
// on that allocator

#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/ds/vec.h"

#include <ubench.h>

#include <stdint.h>

static constexpr size_t PUSHES = 100'000;

// the arena never frees, so it must fit every block the growth passes through
static constexpr size_t ARENA_CAP = 4 * 1024 * 1024;

/// pushes PUSHES elems into a fresh vec on 'al'. A non-zero 'reserve' is reserved up front
static void push_all(tda_Al *al, size_t reserve) {
    tda_Vec v;
    TDA_VEC_INIT(int32_t, al, &v);

    if (reserve > 0 && TDA_STATUS_IS_ERR(tda_vec_reserve(&v, reserve))) {
        tda_vec_deinit(&v);
        return;
    }

    for (size_t i = 0; i < PUSHES; ++i) {
        if (TDA_STATUS_IS_ERR(TDA_VEC_PUSH(int32_t, &v, (int32_t) i))) {
            break;
        }
    }

    UBENCH_DO_NOTHING(tda_vec_data_mut(&v));
    tda_vec_deinit(&v);
}

UBENCH(push, default_grows) {
    push_all(tda_al_default(), 0);
}

UBENCH(push, default_reserved) {
    push_all(tda_al_default(), PUSHES);
}

// the arena grows its last block in place, so a vec alone in it pays almost nothing
// for growth; the pair is the baseline for the default allocator above
UBENCH(push, arena_grows) {
    tda_Arena arena;
    if (TDA_STATUS_IS_ERR(tda_arena_init(tda_al_default(), ARENA_CAP, &arena))) {
        return;
    }

    push_all(TDA_AL(&arena), 0);
    tda_arena_deinit(&arena);
}

UBENCH(push, arena_reserved) {
    tda_Arena arena;
    if (TDA_STATUS_IS_ERR(tda_arena_init(tda_al_default(), ARENA_CAP, &arena))) {
        return;
    }

    push_all(TDA_AL(&arena), PUSHES);
    tda_arena_deinit(&arena);
}

UBENCH_MAIN()
