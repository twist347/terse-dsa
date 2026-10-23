#include "tda/ds/pqueue.h"

#include "tda/algo/heap.h"
#include "tda/core/check.h"
#include "tda/core/util.h"
#include "tda/ds/vec.h"

#include "internal/heap.h"

#include <assert.h>

/* ========== internals ========== */

#define ASSERT_PQUEUE(q) \
    (assert(q),          \
     assert((q)->vec),   \
     assert((q)->cmp))

// A vec plus its comparator. Every mutation keeps the vec a heap (algo/heap); growth
// and allocator are the vec's.
struct tda_PQueue {
    tda_Vec *vec;
    tda_Cmp cmp;
    void *ctx; // passed to every call of cmp
};

/// takes ownership of 'vec'; on failure it is dropped
[[nodiscard]]
static tda_Status wrap(tda_Vec *vec, tda_Cmp cmp, void *ctx, tda_PQueue **out);

/* ========== lifetime ========== */

tda_Status tda_pqueue_new(tda_Layout elem, tda_Cmp cmp, void *ctx, tda_Al *al, tda_PQueue **out) {
    assert(tda_layout_is_type(elem));
    assert(cmp);
    assert(al);
    assert(out);

    tda_Vec *vec;
    const tda_Status st = tda_vec_new(elem, al, &vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    return wrap(vec, cmp, ctx, out);
}

tda_Status tda_pqueue_new_cap(size_t cap, tda_Layout elem, tda_Cmp cmp, void *ctx, tda_Al *al, tda_PQueue **out) {
    assert(tda_layout_is_type(elem));
    assert(cmp);
    assert(al);
    assert(out);

    tda_Vec *vec;
    const tda_Status st = tda_vec_new_cap(cap, elem, al, &vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    return wrap(vec, cmp, ctx, out);
}

tda_Status tda_pqueue_from_data(
    const void *data, size_t len, tda_Layout elem,
    tda_Cmp cmp, void *ctx,
    tda_Al *al,
    tda_PQueue **out
) {
    assert(tda_layout_is_type(elem));
    assert(cmp);
    assert(al);
    assert(out);

    tda_Vec *vec;
    const tda_Status st = tda_vec_from_data(data, len, elem, al, &vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    tda_span_make_heap(tda_vec_to_span_mut(vec), cmp, ctx);

    return wrap(vec, cmp, ctx, out);
}

tda_Status tda_pqueue_from_span(tda_Span s, tda_Cmp cmp, void *ctx, tda_Al *al, tda_PQueue **out) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    assert(al);
    assert(out);

    return tda_pqueue_from_data(s.data, s.len, s.elem, cmp, ctx, al, out);
}

void tda_pqueue_drop(tda_PQueue *self) {
    if (!self) {
        return;
    }

    ASSERT_PQUEUE(self);

    tda_Al *al_copy = tda_vec_al(self->vec);
    tda_vec_drop(self->vec);
    TDA_DEALLOC(tda_PQueue, al_copy, self, 1);
}

tda_Vec *tda_pqueue_into_vec(tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    tda_Vec *vec = self->vec;
    TDA_DEALLOC(tda_PQueue, tda_vec_al(vec), self, 1);

    return vec;
}

/* ========== copy ========== */

tda_Status tda_pqueue_copy(const tda_PQueue *self, tda_PQueue **out) {
    ASSERT_PQUEUE(self);

    return tda_pqueue_copy_with(self, tda_vec_al(self->vec), out);
}

tda_Status tda_pqueue_copy_with(const tda_PQueue *self, tda_Al *al, tda_PQueue **out) {
    ASSERT_PQUEUE(self);
    assert(al);
    assert(out);

    tda_Vec *vec;
    const tda_Status st = tda_vec_copy_with(self->vec, al, &vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // the copy is already a heap, so no rebuild
    return wrap(vec, self->cmp, self->ctx, out);
}

tda_Status tda_pqueue_copy_assign(tda_PQueue *self, const tda_PQueue *other) {
    ASSERT_PQUEUE(self);
    ASSERT_PQUEUE(other);
    TDA_EXPECT(tda_layout_eq(tda_vec_elem_layout(self->vec), tda_vec_elem_layout(other->vec)));

    if (self == other) {
        return TDA_STATUS_OK;
    }

    const tda_Status st = tda_vec_copy_assign(self->vec, other->vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // the elems are a heap under the comparator of 'other', so take it too
    self->cmp = other->cmp;
    self->ctx = other->ctx;

    ASSERT_PQUEUE(self);

    return TDA_STATUS_OK;
}

tda_Status tda_pqueue_move_assign(tda_PQueue *self, tda_PQueue *other) {
    ASSERT_PQUEUE(self);
    ASSERT_PQUEUE(other);
    TDA_EXPECT(tda_layout_eq(tda_vec_elem_layout(self->vec), tda_vec_elem_layout(other->vec)));

    if (self == other) {
        return TDA_STATUS_OK;
    }

    const tda_Status st = tda_vec_move_assign(self->vec, other->vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // the comparator travels with the elems, as in copy_assign
    self->cmp = other->cmp;
    self->ctx = other->ctx;

    ASSERT_PQUEUE(self);

    return TDA_STATUS_OK;
}

/* ========== info ========== */

size_t tda_pqueue_len(const tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    return tda_vec_len(self->vec);
}

size_t tda_pqueue_cap(const tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    return tda_vec_cap(self->vec);
}

size_t tda_pqueue_elem_size(const tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    return tda_vec_elem_size(self->vec);
}

tda_Layout tda_pqueue_elem_layout(const tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    return tda_vec_elem_layout(self->vec);
}

tda_Al *tda_pqueue_al(const tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    return tda_vec_al(self->vec);
}

tda_Cmp tda_pqueue_cmp(const tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    return self->cmp;
}

void *tda_pqueue_cmp_ctx(const tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    return self->ctx;
}

/* ========== access ========== */

const void *tda_pqueue_top(const tda_PQueue *self) {
    ASSERT_PQUEUE(self);
    TDA_EXPECT(tda_vec_len(self->vec) > 0);

    return tda_vec_front(self->vec);
}

/* ========== mods ========== */

tda_Status tda_pqueue_push(tda_PQueue *self, const void *val) {
    ASSERT_PQUEUE(self);
    assert(val);

    const tda_Status st = tda_vec_push(self->vec, val);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // push_heap expects the new elem last, where the push put it
    tda_span_push_heap(tda_vec_to_span_mut(self->vec), self->cmp, self->ctx);

    return TDA_STATUS_OK;
}

void tda_pqueue_pop(tda_PQueue *self) {
    ASSERT_PQUEUE(self);
    TDA_EXPECT(tda_vec_len(self->vec) > 0);

    // pop_heap moves the greatest elem last and leaves a heap before it; the vec then
    // drops the tail
    tda_span_pop_heap(tda_vec_to_span_mut(self->vec), self->cmp, self->ctx);
    tda_vec_pop(self->vec);
}

void tda_pqueue_pop_to(tda_PQueue *self, void *out) {
    ASSERT_PQUEUE(self);
    assert(out);
    TDA_EXPECT(tda_vec_len(self->vec) > 0);

    // as in tda_pqueue_pop, but the elem moved last is copied out
    tda_span_pop_heap(tda_vec_to_span_mut(self->vec), self->cmp, self->ctx);
    tda_vec_pop_to(self->vec, out);
}

void tda_pqueue_replace_top(tda_PQueue *self, const void *val) {
    ASSERT_PQUEUE(self);
    assert(val);
    TDA_EXPECT(tda_vec_len(self->vec) > 0);

    // only the root can be out of place, so one sift down from it restores the heap.
    // vec_set handles 'val' aliasing the root
    tda_vec_set(self->vec, 0, val);

    const tda_SpanMut s = tda_vec_to_span_mut(self->vec);
    tda_heap_sift_down(s, 0, s.len, self->cmp, self->ctx);
}

void tda_pqueue_clear(tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    tda_vec_clear(self->vec);
}

tda_Status tda_pqueue_reserve(tda_PQueue *self, size_t new_cap) {
    ASSERT_PQUEUE(self);

    return tda_vec_reserve(self->vec, new_cap);
}

tda_Status tda_pqueue_shrink_to_fit(tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    return tda_vec_shrink_to_fit(self->vec);
}

void tda_pqueue_swap(tda_PQueue *self, tda_PQueue *other) {
    ASSERT_PQUEUE(self);
    ASSERT_PQUEUE(other);
    TDA_EXPECT(tda_layout_eq(tda_vec_elem_layout(self->vec), tda_vec_elem_layout(other->vec)));

    if (self == other) {
        return;
    }

    tda_vec_swap(self->vec, other->vec);
    TDA_SWAP(self->cmp, other->cmp);
    TDA_SWAP(self->ctx, other->ctx);
}

/* ========== to span ========== */

tda_Span tda_pqueue_to_span(const tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    return tda_vec_to_span(self->vec);
}

/* ========== print ========== */

void tda_pqueue_fprint(const tda_PQueue *self, FILE *stream, tda_FPrint fprint) {
    ASSERT_PQUEUE(self);

    tda_vec_fprint(self->vec, stream, fprint);
}

void tda_pqueue_print(const tda_PQueue *self, tda_FPrint fprint) {
    ASSERT_PQUEUE(self);

    tda_vec_print(self->vec, fprint);
}

/* ========== internals ========== */

static tda_Status wrap(tda_Vec *vec, tda_Cmp cmp, void *ctx, tda_PQueue **out) {
    assert(vec);
    assert(cmp);
    assert(out);

    tda_PQueue *obj = TDA_ALLOC(tda_PQueue, tda_vec_al(vec), 1);
    if (!obj) {
        tda_vec_drop(vec);
        return TDA_STATUS_ERR_NO_MEM;
    }

    obj->vec = vec;
    obj->cmp = cmp;
    obj->ctx = ctx;

    *out = obj;

    return TDA_STATUS_OK;
}
