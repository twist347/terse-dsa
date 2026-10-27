#include "tda/ds/queue.h"

#include "tda/core/check.h"
#include "tda/ds/deque.h"

#include <assert.h>

/* ========== internals ========== */

// the deque asserts the rest of its invariants on every call
#define ASSERT_QUEUE(q) \
    (assert(q),         \
     assert((q)->deque_.al_.vt))

// two queues whose elems can pass between them or be compared: one elem layout
#define EXPECT_SAME_QUEUE(a, b) \
    (TDA_EXPECT(tda_layout_eq(tda_deque_elem_layout(&(a)->deque_), tda_deque_elem_layout(&(b)->deque_))))

// The deque does all the work; this type only hides the ops a queue should not have.

/// whether 'src' views any elem of the queue, in either of the runs the ring may form
[[nodiscard]] [[maybe_unused]]
static bool views_elems(const tda_Queue *self, tda_Span src);

/* ========== lifetime ========== */

tda_Queue tda_queue_init(tda_Layout elem, tda_Al al) {
    assert(tda_layout_is_type(elem));
    assert(al.vt);

    return (tda_Queue){.deque_ = tda_deque_init(elem, al)};
}

tda_Status tda_queue_init_cap(size_t cap, tda_Layout elem, tda_Al al, tda_Queue *out) {
    assert(tda_layout_is_type(elem));
    assert(al.vt);
    assert(out);

    return tda_deque_init_cap(cap, elem, al, &out->deque_);
}

tda_Status tda_queue_from_data(const void *data, size_t len, tda_Layout elem, tda_Al al, tda_Queue *out) {
    assert(data || len == 0);
    assert(tda_layout_is_type(elem));
    assert(al.vt);
    assert(out);

    // the deque keeps the order, so data[0] is the front
    return tda_deque_from_data(data, len, elem, al, &out->deque_);
}

tda_Status tda_queue_from_span(tda_Span s, tda_Al al, tda_Queue *out) {
    TDA_SPAN_ASSERT(s);
    assert(al.vt);
    assert(out);

    return tda_queue_from_data(s.data, s.len, s.elem, al, out);
}

void tda_queue_deinit(tda_Queue *self) {
    assert(self);

    tda_deque_deinit(&self->deque_);
}

tda_Deque tda_queue_into_deque(tda_Queue *self) {
    ASSERT_QUEUE(self);

    const tda_Deque inner = self->deque_;
    self->deque_ = (tda_Deque){};

    return inner;
}

/* ========== copy ========== */

tda_Status tda_queue_copy(const tda_Queue *self, tda_Queue *out) {
    ASSERT_QUEUE(self);
    assert(out);

    return tda_queue_copy_with(self, tda_deque_al(&self->deque_), out);
}

tda_Status tda_queue_copy_with(const tda_Queue *self, tda_Al al, tda_Queue *out) {
    ASSERT_QUEUE(self);
    assert(al.vt);
    assert(out);

    return tda_deque_copy_with(&self->deque_, al, &out->deque_);
}

tda_Status tda_queue_copy_assign(tda_Queue *self, const tda_Queue *other) {
    ASSERT_QUEUE(self);
    ASSERT_QUEUE(other);
    EXPECT_SAME_QUEUE(self, other);

    // no self-assignment guard: the deque already returns early on it
    const tda_Status st = tda_deque_copy_assign(&self->deque_, &other->deque_);

    ASSERT_QUEUE(self);

    return st;
}

void tda_queue_move_assign(tda_Queue *self, tda_Queue *other) {
    assert(self);
    ASSERT_QUEUE(other);
    // an inert 'self', `= {}`, is a target too: it takes on everything 'other' has
    if (self->deque_.al_.vt) {
        ASSERT_QUEUE(self);
        EXPECT_SAME_QUEUE(self, other);
    }

    // self-move is handled by the deque, as in copy_assign
    tda_deque_move_assign(&self->deque_, &other->deque_);

    ASSERT_QUEUE(self);
    ASSERT_QUEUE(other);
}

void tda_queue_copy_to_span(const tda_Queue *self, tda_SpanMut dst) {
    ASSERT_QUEUE(self);
    TDA_SPAN_ASSERT(dst);
    TDA_EXPECT(tda_layout_eq(tda_deque_elem_layout(&self->deque_), dst.elem));
    TDA_EXPECT(dst.len == tda_deque_len(&self->deque_));

    tda_deque_copy_to_span(&self->deque_, dst);
}

/* ========== compare ========== */

bool tda_queue_eq(const tda_Queue *a, const tda_Queue *b) {
    ASSERT_QUEUE(a);
    ASSERT_QUEUE(b);
    EXPECT_SAME_QUEUE(a, b);

    return tda_deque_eq(&a->deque_, &b->deque_);
}

bool tda_queue_eq_by(const tda_Queue *a, const tda_Queue *b, tda_Eq eq, void *ctx) {
    ASSERT_QUEUE(a);
    ASSERT_QUEUE(b);
    assert(eq);
    EXPECT_SAME_QUEUE(a, b);

    return tda_deque_eq_by(&a->deque_, &b->deque_, eq, ctx);
}

/* ========== mods ========== */

tda_Status tda_queue_push(tda_Queue *self, const void *val) {
    ASSERT_QUEUE(self);
    assert(val);

    const tda_Status st = tda_deque_push_back(&self->deque_, val);

    ASSERT_QUEUE(self);

    return st;
}

tda_Status tda_queue_extend(tda_Queue *self, tda_Span src) {
    ASSERT_QUEUE(self);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(tda_layout_eq(tda_deque_elem_layout(&self->deque_), src.elem));
    // growing may free what 'src' views; the elems lie in up to two runs
    TDA_EXPECT(!views_elems(self, src));

    const tda_Status st = tda_deque_extend(&self->deque_, src);

    ASSERT_QUEUE(self);

    return st;
}

void tda_queue_pop(tda_Queue *self) {
    ASSERT_QUEUE(self);
    TDA_EXPECT(tda_deque_len(&self->deque_) > 0);

    tda_deque_pop_front(&self->deque_);

    ASSERT_QUEUE(self);
}

void tda_queue_pop_to(tda_Queue *self, void *out) {
    ASSERT_QUEUE(self);
    assert(out);
    TDA_EXPECT(tda_deque_len(&self->deque_) > 0);

    tda_deque_pop_front_to(&self->deque_, out);

    ASSERT_QUEUE(self);
}

void tda_queue_clear(tda_Queue *self) {
    ASSERT_QUEUE(self);

    tda_deque_clear(&self->deque_);

    ASSERT_QUEUE(self);
}

tda_Status tda_queue_reserve(tda_Queue *self, size_t new_cap) {
    ASSERT_QUEUE(self);

    const tda_Status st = tda_deque_reserve(&self->deque_, new_cap);

    ASSERT_QUEUE(self);

    return st;
}

tda_Status tda_queue_shrink_to_fit(tda_Queue *self) {
    ASSERT_QUEUE(self);

    const tda_Status st = tda_deque_shrink_to_fit(&self->deque_);

    ASSERT_QUEUE(self);

    return st;
}

void tda_queue_swap(tda_Queue *self, tda_Queue *other) {
    ASSERT_QUEUE(self);
    ASSERT_QUEUE(other);
    EXPECT_SAME_QUEUE(self, other);

    // self-swap is handled by the deque, as in copy_assign
    tda_deque_swap(&self->deque_, &other->deque_);

    ASSERT_QUEUE(self);
    ASSERT_QUEUE(other);
}

/* ========== print ========== */

int tda_queue_fprint(const tda_Queue *self, FILE *stream, tda_FPrint fprint, void *ctx) {
    ASSERT_QUEUE(self);
    assert(stream);
    assert(fprint);

    return tda_deque_fprint(&self->deque_, stream, fprint, ctx);
}

int tda_queue_print(const tda_Queue *self, tda_FPrint fprint, void *ctx) {
    ASSERT_QUEUE(self);
    assert(fprint);

    return tda_deque_print(&self->deque_, fprint, ctx);
}

/* ========== internals ========== */

static bool views_elems(const tda_Queue *self, tda_Span src) {
    tda_Span front, back;
    tda_deque_to_spans(&self->deque_, &front, &back);

    return tda_span_overlap(front, src) || tda_span_overlap(back, src);
}
