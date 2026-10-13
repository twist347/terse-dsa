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

/// pushes PUSHES elems into a fresh vec on 'al'. A non-zero 'cap' reserves up front
static void push_all(tda_Al *al, size_t cap) {
    tda_Vec *v = nullptr;
    if (TDA_STATUS_IS_ERR(TDA_VEC_NEW(int32_t, al, &v))) {
        return;
    }

    if (cap > 0 && TDA_STATUS_IS_ERR(tda_vec_reserve(v, cap))) {
        tda_vec_drop(v);
        return;
    }

    for (size_t i = 0; i < PUSHES; ++i) {
        if (TDA_STATUS_IS_ERR(TDA_VEC_PUSH(int32_t, v, (int32_t) i))) {
            break;
        }
    }

    UBENCH_DO_NOTHING(tda_vec_data_mut(v));
    tda_vec_drop(v);
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
    tda_Al *arena = tda_al_arena_new(tda_al_default(), ARENA_CAP);
    if (!arena) {
        return;
    }

    push_all(arena, 0);
    tda_al_arena_drop(arena);
}

UBENCH(push, arena_reserved) {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), ARENA_CAP);
    if (!arena) {
        return;
    }

    push_all(arena, PUSHES);
    tda_al_arena_drop(arena);
}

UBENCH_MAIN()
