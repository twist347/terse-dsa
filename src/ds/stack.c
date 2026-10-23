#include "tda/ds/stack.h"

#include "tda/core/check.h"
#include "tda/ds/vec.h"

#include <assert.h>

/* ========== internals ========== */

// the vec asserts the rest of its invariants on every call
#define ASSERT_STACK(s) \
    (assert(s),         \
     assert((s)->vec_.al_))

// A stack is a vec with fewer ops. Growth, allocator and copy semantics are the vec's.

/* ========== lifetime ========== */

void tda_stack_init(tda_Layout elem, tda_Al *al, tda_Stack *out) {
    assert(tda_layout_is_type(elem));
    assert(al);
    assert(out);

    tda_vec_init(elem, al, &out->vec_);
}

tda_Status tda_stack_init_cap(size_t cap, tda_Layout elem, tda_Al *al, tda_Stack *out) {
    assert(tda_layout_is_type(elem));
    assert(al);
    assert(out);

    return tda_vec_init_cap(cap, elem, al, &out->vec_);
}

tda_Status tda_stack_from_data(const void *data, size_t len, tda_Layout elem, tda_Al *al, tda_Stack *out) {
    assert(tda_layout_is_type(elem));
    assert(al);
    assert(out);

    // the vec keeps the order, so the last elem is the top
    return tda_vec_from_data(data, len, elem, al, &out->vec_);
}

tda_Status tda_stack_from_span(tda_Span s, tda_Al *al, tda_Stack *out) {
    TDA_SPAN_ASSERT(s);
    assert(al);
    assert(out);

    return tda_stack_from_data(s.data, s.len, s.elem, al, out);
}

void tda_stack_deinit(tda_Stack *self) {
    assert(self);

    tda_vec_deinit(&self->vec_);
}

void tda_stack_into_vec(tda_Stack *self, tda_Vec *out) {
    ASSERT_STACK(self);
    assert(out);

    *out = self->vec_;
    self->vec_ = (tda_Vec){};
}

/* ========== copy ========== */

tda_Status tda_stack_copy(const tda_Stack *self, tda_Stack *out) {
    ASSERT_STACK(self);

    return tda_stack_copy_with(self, tda_vec_al(&self->vec_), out);
}

tda_Status tda_stack_copy_with(const tda_Stack *self, tda_Al *al, tda_Stack *out) {
    ASSERT_STACK(self);
    assert(al);
    assert(out);

    return tda_vec_copy_with(&self->vec_, al, &out->vec_);
}

tda_Status tda_stack_copy_assign(tda_Stack *self, const tda_Stack *other) {
    ASSERT_STACK(self);
    ASSERT_STACK(other);
    TDA_EXPECT(tda_layout_eq(tda_vec_elem_layout(&self->vec_), tda_vec_elem_layout(&other->vec_)));

    // no self-assignment guard: the vec already returns early on it
    return tda_vec_copy_assign(&self->vec_, &other->vec_);
}

void tda_stack_move_assign(tda_Stack *self, tda_Stack *other) {
    ASSERT_STACK(self);
    ASSERT_STACK(other);
    TDA_EXPECT(tda_layout_eq(tda_vec_elem_layout(&self->vec_), tda_vec_elem_layout(&other->vec_)));

    // self-move is handled by the vec, as in copy_assign
    tda_vec_move_assign(&self->vec_, &other->vec_);
}

/* ========== compare ========== */

bool tda_stack_eq(const tda_Stack *a, const tda_Stack *b) {
    ASSERT_STACK(a);
    ASSERT_STACK(b);

    return tda_vec_eq(&a->vec_, &b->vec_);
}

bool tda_stack_eq_by(const tda_Stack *a, const tda_Stack *b, tda_Eq eq, void *ctx) {
    ASSERT_STACK(a);
    ASSERT_STACK(b);

    return tda_vec_eq_by(&a->vec_, &b->vec_, eq, ctx);
}

/* ========== mods ========== */

tda_Status tda_stack_push(tda_Stack *self, const void *val) {
    ASSERT_STACK(self);
    assert(val);

    return tda_vec_push(&self->vec_, val);
}

void tda_stack_pop_to(tda_Stack *self, void *out) {
    ASSERT_STACK(self);
    assert(out);
    TDA_EXPECT(tda_vec_len(&self->vec_) > 0);

    tda_vec_pop_to(&self->vec_, out);
}

tda_Status tda_stack_reserve(tda_Stack *self, size_t new_cap) {
    ASSERT_STACK(self);

    return tda_vec_reserve(&self->vec_, new_cap);
}

tda_Status tda_stack_shrink_to_fit(tda_Stack *self) {
    ASSERT_STACK(self);

    return tda_vec_shrink_to_fit(&self->vec_);
}

void tda_stack_swap(tda_Stack *self, tda_Stack *other) {
    ASSERT_STACK(self);
    ASSERT_STACK(other);
    TDA_EXPECT(tda_layout_eq(tda_vec_elem_layout(&self->vec_), tda_vec_elem_layout(&other->vec_)));

    // self-swap is handled by the vec, as in copy_assign
    tda_vec_swap(&self->vec_, &other->vec_);
}

/* ========== print ========== */

void tda_stack_fprint(const tda_Stack *self, FILE *stream, tda_FPrint fprint) {
    ASSERT_STACK(self);

    tda_vec_fprint(&self->vec_, stream, fprint);
}

void tda_stack_print(const tda_Stack *self, tda_FPrint fprint) {
    ASSERT_STACK(self);

    tda_vec_print(&self->vec_, fprint);
}
