#include "tda/tda.h"

#include <stdio.h>

int main() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024); // alloc
    if (!arena) {
        return 1;
    }

    int rc = 1;
    tda_Vec *vec = nullptr; // ds
    if (TDA_STATUS_IS_ERR(TDA_VEC_OF(int32_t, arena, &vec, 5, 3, 1, 4, 2))) {
        goto out;
    }

    tda_span_sort(tda_vec_to_span_mut(vec), tda_cmp_i32, nullptr); // algo, core/cmp

    size_t idx; // core/span
    if (!tda_span_binary_search(tda_vec_to_span(vec), &(int32_t){4}, tda_cmp_i32, nullptr, &idx)) {
        goto out;
    }

    tda_Rng rng = tda_rng_from_seed(1); // core/rng
    tda_span_shuffle(tda_vec_to_span_mut(vec), &rng);

    printf("consumer ok: len %zu, 4 sorted at %zu\n", tda_vec_len(vec), idx);
    rc = 0;

out:
    tda_vec_drop(vec);
    tda_al_arena_drop(arena);

    return rc;
}
