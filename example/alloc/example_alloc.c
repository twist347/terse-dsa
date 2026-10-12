// for @snippet

#include "tda/alloc/alloc.h"
#include "tda/alloc/default.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/// [custom]
// a table must fill in alloc and dealloc; calloc and realloc may be null, and the
// wrappers then build them from the other two. ctx is the implementation's state,
// passed to every callback
typedef struct {
    tda_Al *parent;
    size_t live;
} CountingCtx;

static void *counting_alloc(void *ctx, size_t size) {
    CountingCtx *c = ctx;

    void *ptr = tda_alloc(c->parent, size);
    if (ptr) {
        c->live += size;
    }

    return ptr;
}

static void counting_dealloc(void *ctx, void *ptr, size_t size) {
    CountingCtx *c = ctx;

    tda_dealloc(c->parent, ptr, size);
    c->live -= size;
}

/// [custom]

int main() {
    /// [use]
    tda_Al *al = tda_al_default();

    // the macros apply sizeof(T) and check the multiplication, so a count that would
    // overflow gives null instead of a wrapped size
    int32_t *xs = TDA_ALLOC(int32_t, al, 4);
    if (!xs) {
        return 1;
    }

    xs[0] = 7;
    printf("%" PRId32 "\n", xs[0]); // 7

    TDA_DEALLOC(int32_t, al, xs, 4);
    /// [use]

    /// [wrap]
    // an allocator built over another borrows it: the default one outlives this and
    // nothing here owns it
    CountingCtx ctx = {.parent = tda_al_default(), .live = 0};
    tda_Al counting = {
        .ctx = &ctx,
        .alloc = counting_alloc,
        .dealloc = counting_dealloc,
    };

    // calloc is null in that table, so the wrapper does alloc plus memset
    int32_t *zeros = TDA_CALLOC(int32_t, &counting, 4);
    if (!zeros) {
        return 1;
    }
    printf("%" PRId32 ", %zu bytes live\n", zeros[0], ctx.live); // 0, 16 bytes live

    TDA_DEALLOC(int32_t, &counting, zeros, 4);
    printf("%zu bytes live\n", ctx.live); // 0 bytes live
    /// [wrap]

    return 0;
}
