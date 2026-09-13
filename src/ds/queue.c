#include "tda/ds/queue.h"

#include "tda/ds/deque.h"

#include <assert.h>

/* ========== internals ========== */

#define ASSERT_QUEUE(q) \
    (assert(q),         \
     assert((q)->deque))

// A queue is a deque seen through a smaller keyhole: the growth, the allocator and the copy
// semantics stay the deque's, and what this type adds is the operations it does NOT forward.
struct tda_Queue {
    tda_Deque *deque;
};

/// takes ownership of 'deque' either way: on failure it is dropped, not handed back
[[nodiscard]]
static tda_Status wrap(tda_Deque *deque, tda_Queue **out);

/* ========== lifetime ========== */

tda_Status tda_queue_new(size_t elem_size, tda_Al *al, tda_Queue **out) {
    assert(elem_size > 0);
    assert(al);
    assert(out);

    tda_Deque *deque;
    const tda_Status st = tda_deque_new(elem_size, al, &deque);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    return wrap(deque, out);
}

tda_Status tda_queue_new_cap(size_t cap, size_t elem_size, tda_Al *al, tda_Queue **out) {
    assert(elem_size > 0);
    assert(al);
    assert(out);

    tda_Deque *deque;
    const tda_Status st = tda_deque_new_cap(cap, elem_size, al, &deque);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    return wrap(deque, out);
}

tda_Status tda_queue_from_data(const void *data, size_t len, size_t elem_size, tda_Al *al, tda_Queue **out) {
    assert(elem_size > 0);
    assert(al);
    assert(out);

    tda_Deque *deque;
    const tda_Status st = tda_deque_from_data(data, len, elem_size, al, &deque);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // the deque lays them out front to back, which is arrival order already
    return wrap(deque, out);
}

tda_Status tda_queue_from_span(tda_Span s, tda_Al *al, tda_Queue **out) {
    TDA_SPAN_ASSERT(s);
    assert(al);
    assert(out);

    return tda_queue_from_data(s.data, s.len, s.elem_size, al, out);
}

void tda_queue_drop(tda_Queue *self) {
    if (!self) {
        return;
    }

    ASSERT_QUEUE(self);

    tda_Al *al_copy = tda_deque_al(self->deque);
    tda_deque_drop(self->deque);
    tda_dealloc(al_copy, self, sizeof(tda_Queue));
}

tda_Deque *tda_queue_into_deque(tda_Queue *self) {
    ASSERT_QUEUE(self);

    tda_Deque *deque = self->deque;
    tda_dealloc(tda_deque_al(deque), self, sizeof(tda_Queue));

    return deque;
}

/* ========== copy ========== */

tda_Status tda_queue_copy(const tda_Queue *self, tda_Queue **out) {
    ASSERT_QUEUE(self);

    return tda_queue_copy_with(self, tda_deque_al(self->deque), out);
}

tda_Status tda_queue_copy_with(const tda_Queue *self, tda_Al *al, tda_Queue **out) {
    ASSERT_QUEUE(self);
    assert(al);
    assert(out);

    tda_Deque *deque;
    const tda_Status st = tda_deque_copy_with(self->deque, al, &deque);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    return wrap(deque, out);
}

tda_Status tda_queue_copy_assign(const tda_Queue *self, tda_Queue *other) {
    ASSERT_QUEUE(self);
    ASSERT_QUEUE(other);
    assert(tda_deque_elem_size(self->deque) == tda_deque_elem_size(other->deque));

    // self assignment is left to the deque, which already returns early on it: a guard
    // repeated here would be a branch no test could tell from its absence
    return tda_deque_copy_assign(self->deque, other->deque);
}

tda_Status tda_queue_move_assign(tda_Queue *self, tda_Queue *other) {
    ASSERT_QUEUE(self);
    ASSERT_QUEUE(other);
    assert(tda_deque_elem_size(self->deque) == tda_deque_elem_size(other->deque));

    // as in copy_assign, moving a queue onto itself is the deque's early return
    return tda_deque_move_assign(self->deque, other->deque);
}

void tda_queue_copy_to_span(const tda_Queue *self, tda_SpanMut dst) {
    ASSERT_QUEUE(self);
    TDA_SPAN_ASSERT(dst);

    tda_deque_copy_to_span(self->deque, dst);
}

/* ========== compare ========== */

bool tda_queue_eq(const tda_Queue *a, const tda_Queue *b) {
    ASSERT_QUEUE(a);
    ASSERT_QUEUE(b);

    return tda_deque_eq(a->deque, b->deque);
}

bool tda_queue_eq_by(const tda_Queue *a, const tda_Queue *b, tda_Eq eq) {
    ASSERT_QUEUE(a);
    ASSERT_QUEUE(b);

    return tda_deque_eq_by(a->deque, b->deque, eq);
}

/* ========== info ========== */

size_t tda_queue_len(const tda_Queue *self) {
    ASSERT_QUEUE(self);

    return tda_deque_len(self->deque);
}

size_t tda_queue_cap(const tda_Queue *self) {
    ASSERT_QUEUE(self);

    return tda_deque_cap(self->deque);
}

size_t tda_queue_elem_size(const tda_Queue *self) {
    ASSERT_QUEUE(self);

    return tda_deque_elem_size(self->deque);
}

tda_Al *tda_queue_al(const tda_Queue *self) {
    ASSERT_QUEUE(self);

    return tda_deque_al(self->deque);
}

/* ========== access ========== */

const void *tda_queue_front(const tda_Queue *self) {
    ASSERT_QUEUE(self);
    assert(tda_deque_len(self->deque) > 0);

    return tda_deque_front(self->deque);
}

void *tda_queue_front_mut(tda_Queue *self) {
    ASSERT_QUEUE(self);
    assert(tda_deque_len(self->deque) > 0);

    return tda_deque_front_mut(self->deque);
}

const void *tda_queue_back(const tda_Queue *self) {
    ASSERT_QUEUE(self);
    assert(tda_deque_len(self->deque) > 0);

    return tda_deque_back(self->deque);
}

void *tda_queue_back_mut(tda_Queue *self) {
    ASSERT_QUEUE(self);
    assert(tda_deque_len(self->deque) > 0);

    return tda_deque_back_mut(self->deque);
}

/* ========== mods ========== */

tda_Status tda_queue_push(tda_Queue *self, const void *val) {
    ASSERT_QUEUE(self);
    assert(val);

    return tda_deque_push_back(self->deque, val);
}

void tda_queue_pop(tda_Queue *self) {
    ASSERT_QUEUE(self);
    assert(tda_deque_len(self->deque) > 0);

    tda_deque_pop_front(self->deque);
}

void tda_queue_clear(tda_Queue *self) {
    ASSERT_QUEUE(self);

    tda_deque_clear(self->deque);
}

tda_Status tda_queue_reserve(tda_Queue *self, size_t new_cap) {
    ASSERT_QUEUE(self);

    return tda_deque_reserve(self->deque, new_cap);
}

tda_Status tda_queue_shrink_to_fit(tda_Queue *self) {
    ASSERT_QUEUE(self);

    return tda_deque_shrink_to_fit(self->deque);
}

void tda_queue_swap(tda_Queue *self, tda_Queue *other) {
    ASSERT_QUEUE(self);
    ASSERT_QUEUE(other);
    assert(tda_deque_elem_size(self->deque) == tda_deque_elem_size(other->deque));

    // as in copy_assign, swapping a queue with itself is the deque's early return
    tda_deque_swap(self->deque, other->deque);
}

/* ========== print ========== */

void tda_queue_fprint(const tda_Queue *self, FILE *stream, tda_FPrint fprint) {
    ASSERT_QUEUE(self);

    tda_deque_fprint(self->deque, stream, fprint);
}

void tda_queue_print(const tda_Queue *self, tda_FPrint fprint) {
    ASSERT_QUEUE(self);

    tda_deque_print(self->deque, fprint);
}

/* ========== internals ========== */

static tda_Status wrap(tda_Deque *deque, tda_Queue **out) {
    assert(deque);
    assert(out);

    tda_Queue *obj = tda_alloc(tda_deque_al(deque), sizeof(tda_Queue));
    if (!obj) {
        tda_deque_drop(deque);
        return TDA_STATUS_ERR_NO_MEM;
    }

    obj->deque = deque;

    *out = obj;

    return TDA_STATUS_OK;
}
