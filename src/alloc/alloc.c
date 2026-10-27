#include "tda/alloc/alloc.h"

#include "tda/core/align.h"
#include "tda/core/check.h"

#include <assert.h>
#include <string.h>

/* ========== internals ========== */

// a view with both required hooks; '= {}', no allocator, is not one
#define ASSERT_AL(al) \
    (assert((al).vt), assert((al).vt->alloc), assert((al).vt->dealloc))

/* ========== wrappers ========== */

void *tda_alloc(tda_Al al, tda_Layout layout) {
    ASSERT_AL(al);
    assert(tda_layout_is_align(layout.align));

    if (layout.size == 0) {
        return nullptr;
    }

    void *ptr = al.vt->alloc(al.ctx, layout);
    // the one place every block passes through: a hook that misses the alignment would
    // leave each container on it reading misaligned elems
    TDA_EXPECT(!ptr || tda_align_meets(ptr, layout.align));

    return ptr;
}

void *tda_alloc_zeroed(tda_Al al, tda_Layout layout) {
    ASSERT_AL(al);
    assert(tda_layout_is_align(layout.align));

    if (layout.size == 0) {
        return nullptr;
    }

    if (al.vt->alloc_zeroed) {
        void *ptr = al.vt->alloc_zeroed(al.ctx, layout);
        TDA_EXPECT(!ptr || tda_align_meets(ptr, layout.align));

        return ptr;
    }

    void *ptr = tda_alloc(al, layout);
    if (ptr) {
        memset(ptr, 0, layout.size);
    }

    return ptr;
}

bool tda_resize(tda_Al al, void *ptr, tda_Layout old, size_t new_size) {
    ASSERT_AL(al);
    assert(ptr);
    assert(old.size > 0 && new_size > 0);
    assert(tda_layout_is_align(old.align));

    return al.vt->resize && al.vt->resize(al.ctx, ptr, old, new_size);
}

void *tda_remap(tda_Al al, void *ptr, tda_Layout old, size_t new_size) {
    ASSERT_AL(al);
    assert(ptr);
    assert(old.size > 0 && new_size > 0);
    assert(tda_layout_is_align(old.align));

    if (!al.vt->remap) {
        return nullptr;
    }

    void *moved = al.vt->remap(al.ctx, ptr, old, new_size);
    TDA_EXPECT(!moved || tda_align_meets(moved, old.align));

    return moved;
}

void *tda_realloc(tda_Al al, void *ptr, tda_Layout old, size_t new_size) {
    ASSERT_AL(al);
    assert((ptr == nullptr) == (old.size == 0)); // no block has size 0
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

void tda_dealloc(tda_Al al, void *ptr, tda_Layout layout) {
    ASSERT_AL(al);
    assert(!ptr || layout.size > 0);
    assert(tda_layout_is_align(layout.align));

    if (!ptr) {
        return;
    }

    al.vt->dealloc(al.ctx, ptr, layout);
}

/* ========== compare ========== */

bool tda_al_eq(tda_Al a, tda_Al b) {
    return a.ctx == b.ctx && a.vt == b.vt;
}
