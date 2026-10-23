// for @snippet

#include "tda/alloc/default.h"
#include "tda/alloc/slab.h"
#include "tda/core/cmp.h"
#include "tda/core/hash.h"
#include "tda/ds/hmap.h"
#include "tda/ds/list.h"

#include <stdint.h>
#include <stdio.h>

int main() {
    /// [build]
    // any container runs on a slab: its struct and its nodes go to pools by size, and a
    // buffer past 512 bytes goes to the parent
    tda_Al *al = tda_al_slab_new(tda_al_default());
    if (!al) {
        return 1;
    }

    int rc = 1;
    tda_List *list = nullptr;
    tda_HMap *map = nullptr;
    if (TDA_STATUS_IS_ERR(TDA_LIST_NEW(int32_t, al, &list))
        || TDA_STATUS_IS_ERR(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, al, &map))) {
        goto out;
    }

    for (int32_t i = 0; i < 1000; ++i) {
        if (TDA_STATUS_IS_ERR(TDA_LIST_PUSH_BACK(int32_t, list, i))
            || TDA_STATUS_IS_ERR(TDA_HMAP_INSERT(int32_t, int32_t, map, i, i * i, nullptr))) {
            goto out;
        }
    }

    // each list node and each map entry is one small block, carved from 4 KiB chunks
    const tda_AlSlabStats st = tda_al_slab_stats(al);
    printf("%zu small blocks in %zu chunks\n", st.small_blocks, st.chunks);
    rc = 0;

out:
    tda_list_drop(list);
    tda_hmap_drop(map);
    tda_al_slab_drop(al);
    /// [build]

    return rc;
}
