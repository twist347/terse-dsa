// for @snippet

#include "tda/alloc/alloc.h"
#include "tda/alloc/default.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/// [custom]
// a table must fill in alloc and dealloc; alloc_zeroed, resize and remap may be null, and
// the wrappers then make do with the other two. ctx is the allocator's state, passed to
// every hook, and every hook gets the layout the block was made with
typedef struct {
    tda_Al parent;
    size_t live;
} Counting;

static void *counting_alloc(void *ctx, tda_Layout layout) {
    Counting *self = ctx;

    void *ptr = tda_alloc(self->parent, layout);
    if (ptr) {
        self->live += layout.size;
    }

    return ptr;
}

static void counting_dealloc(void *ctx, void *ptr, tda_Layout layout) {
    Counting *self = ctx;

    tda_dealloc(self->parent, ptr, layout);
    self->live -= layout.size;
}

// one table for every Counting; a view pairs it with one of them
static const tda_AlVtable COUNTING_VTABLE = {
    .alloc = counting_alloc,
    .dealloc = counting_dealloc,
};

static tda_Al counting_al(Counting *self) {
    return (tda_Al){.ctx = self, .vt = &COUNTING_VTABLE};
}
/// [custom]

int main() {
    /// [use]
    tda_Al al = tda_al_default();

    // the macros take the layout from T, sizeof and alignof, and check the multiplication,
    // so a count that would overflow gives null instead of a wrapped size
    int32_t *nums = TDA_ALLOC(int32_t, al, 4);
    if (!nums) {
        return 1;
    }

    nums[0] = 7;
    printf("%" PRId32 "\n", nums[0]); // 7

    TDA_DEALLOC(int32_t, al, nums, 4);
    /// [use]

    /// [wrap]
    // an allocator built over another borrows it: the default one outlives this and
    // nothing here owns it
    Counting state = {.parent = tda_al_default(), .live = 0};
    tda_Al counting = counting_al(&state);

    // alloc_zeroed is null in that table, so the wrapper does alloc plus memset
    int32_t *zeros = TDA_ALLOC_ZEROED(int32_t, counting, 4);
    if (!zeros) {
        return 1;
    }
    printf("%" PRId32 ", %zu bytes live\n", zeros[0], state.live); // 0, 16 bytes live

    TDA_DEALLOC(int32_t, counting, zeros, 4);
    printf("%zu bytes live\n", state.live); // 0 bytes live
    /// [wrap]

    return 0;
}
