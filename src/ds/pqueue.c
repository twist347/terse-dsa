#include "tda/ds/pqueue.h"

#include "tda/algo/heap.h"
#include "tda/core/util.h"
#include "tda/ds/vec.h"

#include <assert.h>

/* ========== internals ========== */

#define ASSERT_PQUEUE(q) \
    (assert(q),          \
     assert((q)->vec),   \
     assert((q)->cmp))

// A queue is a vec plus the order it is kept in: every mutation leaves algo/heap's invariant
// standing over the vec's buffer, while the growth and the allocator stay the vec's.
struct tda_PQueue {
    tda_Vec *vec;
    tda_Cmp cmp;
};

/// takes ownership of 'vec' either way: on failure it is dropped, not handed back
[[nodiscard]]
static tda_Status wrap(tda_Vec *vec, tda_Cmp cmp, tda_PQueue **out);

/* ========== lifetime ========== */

tda_Status tda_pqueue_new(size_t elem_size, tda_Cmp cmp, tda_Al *al, tda_PQueue **out) {
    assert(elem_size > 0);
    assert(cmp);
    assert(al);
    assert(out);

    tda_Vec *vec;
    const tda_Status st = tda_vec_new(elem_size, al, &vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    return wrap(vec, cmp, out);
}

tda_Status tda_pqueue_new_cap(size_t cap, size_t elem_size, tda_Cmp cmp, tda_Al *al, tda_PQueue **out) {
    assert(elem_size > 0);
    assert(cmp);
    assert(al);
    assert(out);

    tda_Vec *vec;
    const tda_Status st = tda_vec_new_cap(cap, elem_size, al, &vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    return wrap(vec, cmp, out);
}

tda_Status tda_pqueue_from_data(
    const void *data, size_t len, size_t elem_size,
    tda_Cmp cmp,
    tda_Al *al,
    tda_PQueue **out
) {
    assert(elem_size > 0);
    assert(cmp);
    assert(al);
    assert(out);

    tda_Vec *vec;
    const tda_Status st = tda_vec_from_data(data, len, elem_size, al, &vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    tda_span_make_heap(tda_vec_to_span_mut(vec), cmp);

    return wrap(vec, cmp, out);
}

tda_Status tda_pqueue_from_span(tda_Span s, tda_Cmp cmp, tda_Al *al, tda_PQueue **out) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    assert(al);
    assert(out);

    return tda_pqueue_from_data(s.data, s.len, s.elem_size, cmp, al, out);
}

void tda_pqueue_drop(tda_PQueue *self) {
    if (!self) {
        return;
    }

    ASSERT_PQUEUE(self);

    tda_Al *al_copy = tda_vec_al(self->vec);
    tda_vec_drop(self->vec);
    tda_dealloc(al_copy, self, sizeof(tda_PQueue));
}

tda_Vec *tda_pqueue_into_vec(tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    tda_Vec *vec = self->vec;
    tda_dealloc(tda_vec_al(vec), self, sizeof(tda_PQueue));

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

    // the buffer is copied as it stands, heap order and all, so no reheapifying
    return wrap(vec, self->cmp, out);
}

tda_Status tda_pqueue_copy_assign(const tda_PQueue *self, tda_PQueue *other) {
    ASSERT_PQUEUE(self);
    ASSERT_PQUEUE(other);
    assert(tda_vec_elem_size(self->vec) == tda_vec_elem_size(other->vec));

    if (self == other) {
        return TDA_STATUS_OK;
    }

    const tda_Status st = tda_vec_copy_assign(self->vec, other->vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // keeping other's own comparator would leave it holding a buffer that is a heap
    // under nobody's order
    other->cmp = self->cmp;

    ASSERT_PQUEUE(other);

    return TDA_STATUS_OK;
}

tda_Status tda_pqueue_move_assign(tda_PQueue *self, tda_PQueue *other) {
    ASSERT_PQUEUE(self);
    ASSERT_PQUEUE(other);
    assert(tda_vec_elem_size(self->vec) == tda_vec_elem_size(other->vec));

    if (self == other) {
        return TDA_STATUS_OK;
    }

    const tda_Status st = tda_vec_move_assign(self->vec, other->vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // the elems arrive arranged under the comparator of 'self', so it travels with them —
    // the same reason copy_assign hands it over
    other->cmp = self->cmp;

    ASSERT_PQUEUE(other);

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

tda_Al *tda_pqueue_al(const tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    return tda_vec_al(self->vec);
}

tda_Cmp tda_pqueue_cmp(const tda_PQueue *self) {
    ASSERT_PQUEUE(self);

    return self->cmp;
}

/* ========== access ========== */

const void *tda_pqueue_top(const tda_PQueue *self) {
    ASSERT_PQUEUE(self);
    assert(tda_vec_len(self->vec) > 0);

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

    // the new elem sits last, which is exactly where push_heap expects it
    tda_span_push_heap(tda_vec_to_span_mut(self->vec), self->cmp);

    return TDA_STATUS_OK;
}

void tda_pqueue_pop(tda_PQueue *self) {
    ASSERT_PQUEUE(self);
    assert(tda_vec_len(self->vec) > 0);

    // pop_heap parks the greatest elem last and leaves a heap in front of it; dropping
    // the tail is then the vec's business
    tda_span_pop_heap(tda_vec_to_span_mut(self->vec), self->cmp);
    tda_vec_pop(self->vec);
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
    assert(tda_vec_elem_size(self->vec) == tda_vec_elem_size(other->vec));

    if (self == other) {
        return;
    }

    tda_vec_swap(self->vec, other->vec);
    TDA_SWAP(self->cmp, other->cmp);
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

static tda_Status wrap(tda_Vec *vec, tda_Cmp cmp, tda_PQueue **out) {
    assert(vec);
    assert(cmp);
    assert(out);

    tda_PQueue *obj = tda_alloc(tda_vec_al(vec), sizeof(tda_PQueue));
    if (!obj) {
        tda_vec_drop(vec);
        return TDA_STATUS_ERR_NO_MEM;
    }

    obj->vec = vec;
    obj->cmp = cmp;

    *out = obj;

    return TDA_STATUS_OK;
}
