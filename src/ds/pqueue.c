#include "tda/ds/pqueue.h"

#include "tda/algo/heap.h"
#include "tda/core/check.h"
#include "tda/core/util.h"
#include "tda/ds/vec.h"

#include "internal/heap.h"

#include <assert.h>

/* ========== internals ========== */

// the vec asserts the rest of its invariants on every call
#define ASSERT_PQUEUE(q)       \
    (assert(q),                \
     assert((q)->vec_.al_.vt), \
     assert((q)->cmp_))

// two queues whose elems can pass between them: one elem layout
#define EXPECT_SAME_PQUEUE(a, b) \
    (TDA_EXPECT(tda_layout_eq(tda_vec_elem_layout(&(a)->vec_), tda_vec_elem_layout(&(b)->vec_))))

// A vec plus its comparator. Every mutation keeps the vec a heap (algo/heap); growth
// and allocator are the vec's.

/* ========== lifetime ========== */

tda_PQueue tda_pqueue_init(tda_Layout elem, tda_Cmp cmp, void *ctx, tda_Al al) {
    assert(tda_layout_is_type(elem));
    assert(cmp);
    assert(al.vt);

    return (tda_PQueue){.vec_ = tda_vec_init(elem, al), .cmp_ = cmp, .ctx_ = ctx};
}

tda_Status tda_pqueue_init_cap(size_t cap, tda_Layout elem, tda_Cmp cmp, void *ctx, tda_Al al, tda_PQueue *out) {
    assert(tda_layout_is_type(elem));
    assert(cmp);
    assert(al.vt);
    assert(out);

    tda_Vec vec;
    const tda_Status st = tda_vec_init_cap(cap, elem, al, &vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    *out = (tda_PQueue){.vec_ = vec, .cmp_ = cmp, .ctx_ = ctx};

    return TDA_STATUS_OK;
}

tda_Status tda_pqueue_from_data(
    const void *data, size_t len, tda_Layout elem,
    tda_Cmp cmp, void *ctx,
    tda_Al al,
    tda_PQueue *out
) {
    assert(data || len == 0);
    assert(tda_layout_is_type(elem));
    assert(cmp);
    assert(al.vt);
    assert(out);

    tda_Vec vec;
    const tda_Status st = tda_vec_from_data(data, len, elem, al, &vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    tda_span_make_heap(tda_vec_to_span_mut(&vec), cmp, ctx);

    *out = (tda_PQueue){.vec_ = vec, .cmp_ = cmp, .ctx_ = ctx};

    return TDA_STATUS_OK;
}

tda_Status tda_pqueue_from_span(tda_Span s, tda_Cmp cmp, void *ctx, tda_Al al, tda_PQueue *out) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    assert(al.vt);
    assert(out);

    return tda_pqueue_from_data(s.data, s.len, s.elem, cmp, ctx, al, out);
}

void tda_pqueue_deinit(tda_PQueue *self) {
    assert(self);

    tda_vec_deinit(&self->vec_);
    *self = (tda_PQueue){};
}

tda_Vec tda_pqueue_into_vec(tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    const tda_Vec inner = self->vec_;
    *self = (tda_PQueue){};

    return inner;
}

/* ========== copy ========== */

tda_Status tda_pqueue_copy(const tda_PQueue *self, tda_PQueue *out) {
    ASSERT_PQUEUE(self);
    assert(out);

    return tda_pqueue_copy_with(self, tda_vec_al(&self->vec_), out);
}

tda_Status tda_pqueue_copy_with(const tda_PQueue *self, tda_Al al, tda_PQueue *out) {
    ASSERT_PQUEUE(self);
    assert(al.vt);
    assert(out);

    tda_Vec vec;
    const tda_Status st = tda_vec_copy_with(&self->vec_, al, &vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // the copy is already a heap, so no rebuild
    *out = (tda_PQueue){.vec_ = vec, .cmp_ = self->cmp_, .ctx_ = self->ctx_};

    return TDA_STATUS_OK;
}

tda_Status tda_pqueue_copy_assign(tda_PQueue *self, const tda_PQueue *other) {
    ASSERT_PQUEUE(self);
    ASSERT_PQUEUE(other);
    EXPECT_SAME_PQUEUE(self, other);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    const tda_Status st = tda_vec_copy_assign(&self->vec_, &other->vec_);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // the elems are a heap under the comparator of 'other', so take it too
    self->cmp_ = other->cmp_;
    self->ctx_ = other->ctx_;

    ASSERT_PQUEUE(self);

    return TDA_STATUS_OK;
}

void tda_pqueue_move_assign(tda_PQueue *self, tda_PQueue *other) {
    assert(self);
    ASSERT_PQUEUE(other);
    // an inert 'self', `= {}`, is a target too: it takes on everything 'other' has
    if (self->vec_.al_.vt) {
        ASSERT_PQUEUE(self);
        EXPECT_SAME_PQUEUE(self, other);
    }

    if (self == other) {
        return;
    }

    tda_vec_move_assign(&self->vec_, &other->vec_);

    // the comparator travels with the elems, as in copy_assign
    self->cmp_ = other->cmp_;
    self->ctx_ = other->ctx_;

    ASSERT_PQUEUE(self);
    ASSERT_PQUEUE(other);
}

/* ========== mods ========== */

tda_Status tda_pqueue_push(tda_PQueue *self, const void *val) {
    ASSERT_PQUEUE(self);
    assert(val);

    const tda_Status st = tda_vec_push(&self->vec_, val);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // push_heap expects the new elem last, where the push put it
    tda_span_push_heap(tda_vec_to_span_mut(&self->vec_), self->cmp_, self->ctx_);

    ASSERT_PQUEUE(self);

    return TDA_STATUS_OK;
}

tda_Status tda_pqueue_extend(tda_PQueue *self, tda_Span src) {
    ASSERT_PQUEUE(self);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(tda_layout_eq(tda_vec_elem_layout(&self->vec_), src.elem));
    // growing may free what 'src' views
    TDA_EXPECT(!tda_span_overlap(tda_vec_to_span(&self->vec_), src));

    const size_t old_len = tda_vec_len(&self->vec_);
    const tda_Status st = tda_vec_extend(&self->vec_, src);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // an empty queue heapifies in O(n); otherwise each new elem sifts up past the ones
    // before it, which are a heap already
    const tda_SpanMut all = tda_vec_to_span_mut(&self->vec_);
    if (old_len == 0) {
        tda_span_make_heap(all, self->cmp_, self->ctx_);
    } else {
        for (size_t len = old_len + 1; len <= all.len; ++len) {
            tda_span_push_heap(tda_span_sub_mut(all, 0, len), self->cmp_, self->ctx_);
        }
    }

    ASSERT_PQUEUE(self);

    return TDA_STATUS_OK;
}

void tda_pqueue_pop(tda_PQueue *self) {
    ASSERT_PQUEUE(self);
    TDA_EXPECT(tda_vec_len(&self->vec_) > 0);

    // pop_heap moves the greatest elem last and leaves a heap before it; the vec then
    // drops the tail
    tda_span_pop_heap(tda_vec_to_span_mut(&self->vec_), self->cmp_, self->ctx_);
    tda_vec_pop(&self->vec_);

    ASSERT_PQUEUE(self);
}

void tda_pqueue_pop_to(tda_PQueue *self, void *out) {
    ASSERT_PQUEUE(self);
    assert(out);
    TDA_EXPECT(tda_vec_len(&self->vec_) > 0);

    // as in tda_pqueue_pop, but the elem moved last is copied out
    tda_span_pop_heap(tda_vec_to_span_mut(&self->vec_), self->cmp_, self->ctx_);
    tda_vec_pop_to(&self->vec_, out);

    ASSERT_PQUEUE(self);
}

void tda_pqueue_replace_top(tda_PQueue *self, const void *val) {
    ASSERT_PQUEUE(self);
    assert(val);
    TDA_EXPECT(tda_vec_len(&self->vec_) > 0);

    // only the root can be out of place, so one sift down from it restores the heap.
    // vec_set handles 'val' aliasing the root
    tda_vec_set(&self->vec_, 0, val);

    const tda_SpanMut s = tda_vec_to_span_mut(&self->vec_);
    tda_heap_sift_down(s, 0, s.len, self->cmp_, self->ctx_);
}

void tda_pqueue_clear(tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    tda_vec_clear(&self->vec_);

    ASSERT_PQUEUE(self);
}

tda_Status tda_pqueue_reserve(tda_PQueue *self, size_t new_cap) {
    ASSERT_PQUEUE(self);

    const tda_Status st = tda_vec_reserve(&self->vec_, new_cap);

    ASSERT_PQUEUE(self);

    return st;
}

tda_Status tda_pqueue_shrink_to_fit(tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    const tda_Status st = tda_vec_shrink_to_fit(&self->vec_);

    ASSERT_PQUEUE(self);

    return st;
}

void tda_pqueue_swap(tda_PQueue *self, tda_PQueue *other) {
    ASSERT_PQUEUE(self);
    ASSERT_PQUEUE(other);
    EXPECT_SAME_PQUEUE(self, other);

    if (self == other) {
        return;
    }

    TDA_SWAP(*self, *other);

    ASSERT_PQUEUE(self);
    ASSERT_PQUEUE(other);
}

/* ========== print ========== */

int tda_pqueue_fprint(const tda_PQueue *self, FILE *stream, tda_FPrint fprint, void *ctx) {
    ASSERT_PQUEUE(self);
    assert(stream);
    assert(fprint);

    return tda_vec_fprint(&self->vec_, stream, fprint, ctx);
}

int tda_pqueue_print(const tda_PQueue *self, tda_FPrint fprint, void *ctx) {
    ASSERT_PQUEUE(self);
    assert(fprint);

    return tda_vec_print(&self->vec_, fprint, ctx);
}
