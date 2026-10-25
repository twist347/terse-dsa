// for @snippet

#include "tda/alloc/alloc.h"
#include "tda/core/align.h"

#include <stdio.h>

/// [bump_def]
// a bump allocator over a buffer the caller lends: each block starts at the first
// address past the last block that its alignment allows, and nothing is given back
typedef struct {
    unsigned char *buf;
    size_t cap;
    size_t used;
} Bump;

static void *bump_alloc(void *ctx, tda_Layout layout) {
    Bump *self = ctx;

    // a count first, so no address past the buffer is formed before the room is checked
    const size_t room = self->cap - self->used;
    const size_t pad = tda_align_pad(self->buf + self->used, layout.align);
    if (pad > room || layout.size > room - pad) {
        return nullptr;
    }

    void *ptr = self->buf + self->used + pad;
    self->used += pad + layout.size;

    return ptr;
}

static void bump_dealloc(void *, void *, tda_Layout) {
    // the blocks go all at once, with the buffer
}
/// [bump_def]

int main() {
    /// [bump]
    alignas(64) static unsigned char buf[1024];
    Bump bump = {.buf = buf, .cap = sizeof(buf)};
    tda_Al al = {.ctx = &bump, .alloc = bump_alloc, .dealloc = bump_dealloc};

    // a byte, then a double: the double skips ahead to its own boundary
    unsigned char *byte = TDA_ALLOC(unsigned char, &al, 1);
    double *num = TDA_ALLOC(double, &al, 1);
    if (!byte || !num) {
        return 1;
    }
    printf(
        "%s, %zu bytes used\n",
        tda_align_meets(num, alignof(double)) ? "aligned" : "not aligned",
        bump.used
    ); // aligned, 16 bytes used

    // a size rounded up, as an allocator that hands out whole cache lines would round it
    printf("%zu\n", tda_align_up(100, 64)); // 128
    /// [bump]

    return 0;
}
