#include "tda/alloc/alloc.h"

#include <assert.h>
#include <string.h>

/* ========== wrappers ========== */

void *tda_alloc(tda_Al *al, tda_Layout layout) {
    assert(al);
    assert(al->alloc);
    assert(tda_layout_is_align(layout.align));

    if (layout.size == 0) {
        return nullptr;
    }

    return al->alloc(al->ctx, layout);
}

void *tda_alloc_zeroed(tda_Al *al, tda_Layout layout) {
    assert(al);
    assert(tda_layout_is_align(layout.align));

    if (layout.size == 0) {
        return nullptr;
    }

    if (al->alloc_zeroed) {
        return al->alloc_zeroed(al->ctx, layout);
    }

    void *ptr = tda_alloc(al, layout);
    if (ptr) {
        memset(ptr, 0, layout.size);
    }

    return ptr;
}

bool tda_resize(tda_Al *al, void *ptr, tda_Layout old, size_t new_size) {
    assert(al);
    assert(ptr);
    assert(old.size > 0 && new_size > 0);
    assert(tda_layout_is_align(old.align));

    return al->resize && al->resize(al->ctx, ptr, old, new_size);
}

void *tda_remap(tda_Al *al, void *ptr, tda_Layout old, size_t new_size) {
    assert(al);
    assert(ptr);
    assert(old.size > 0 && new_size > 0);
    assert(tda_layout_is_align(old.align));

    return al->remap ? al->remap(al->ctx, ptr, old, new_size) : nullptr;
}

void *tda_realloc(tda_Al *al, void *ptr, tda_Layout old, size_t new_size) {
    assert(al);
    assert(ptr || old.size == 0);
    assert(tda_layout_is_align(old.align));

    if (new_size == 0) {
        tda_dealloc(al, ptr, old);
        return nullptr;
    }

    if (!ptr) {
        return tda_alloc(al, (tda_Layout){.size = new_size, .align = old.align});
    }

    // cheapest first: no move, then the allocator's own move, then a copy
    if (tda_resize(al, ptr, old, new_size)) {
        return ptr;
    }

    void *moved = tda_remap(al, ptr, old, new_size);
    if (moved) {
        return moved;
    }

    void *new_ptr = tda_alloc(al, (tda_Layout){.size = new_size, .align = old.align});
    if (!new_ptr) {
        return nullptr;
    }

    memcpy(new_ptr, ptr, old.size < new_size ? old.size : new_size);
    tda_dealloc(al, ptr, old);

    return new_ptr;
}

void tda_dealloc(tda_Al *al, void *ptr, tda_Layout layout) {
    assert(al);
    assert(al->dealloc);
    assert(tda_layout_is_align(layout.align));

    if (!ptr) {
        return;
    }

    al->dealloc(al->ctx, ptr, layout);
}
