#include "tda/ds/stack.h"

#include "tda/core/check.h"
#include "tda/ds/vec.h"

#include <assert.h>

/* ========== internals ========== */

#define ASSERT_STACK(s) \
    (assert(s),         \
     assert((s)->vec))

// A stack is a vec seen through a smaller keyhole: the growth, the allocator and the copy
// semantics stay the vec's, and what this type adds is the operations it does NOT forward.
struct tda_Stack {
    tda_Vec *vec;
};

/// takes ownership of 'vec' either way: on failure it is dropped, not handed back
[[nodiscard]]
static tda_Status wrap(tda_Vec *vec, tda_Stack **out);

/* ========== lifetime ========== */

tda_Status tda_stack_new(size_t elem_size, tda_Al *al, tda_Stack **out) {
    assert(elem_size > 0);
    assert(al);
    assert(out);

    tda_Vec *vec;
    const tda_Status st = tda_vec_new(elem_size, al, &vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    return wrap(vec, out);
}

tda_Status tda_stack_new_cap(size_t cap, size_t elem_size, tda_Al *al, tda_Stack **out) {
    assert(elem_size > 0);
    assert(al);
    assert(out);

    tda_Vec *vec;
    const tda_Status st = tda_vec_new_cap(cap, elem_size, al, &vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    return wrap(vec, out);
}

tda_Status tda_stack_from_data(const void *data, size_t len, size_t elem_size, tda_Al *al, tda_Stack **out) {
    assert(elem_size > 0);
    assert(al);
    assert(out);

    tda_Vec *vec;
    const tda_Status st = tda_vec_from_data(data, len, elem_size, al, &vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // the vec lays them out in order, so the last one is the one on top
    return wrap(vec, out);
}

tda_Status tda_stack_from_span(tda_Span s, tda_Al *al, tda_Stack **out) {
    TDA_SPAN_ASSERT(s);
    assert(al);
    assert(out);

    return tda_stack_from_data(s.data, s.len, s.elem_size, al, out);
}

void tda_stack_drop(tda_Stack *self) {
    if (!self) {
        return;
    }

    ASSERT_STACK(self);

    tda_Al *al_copy = tda_vec_al(self->vec);
    tda_vec_drop(self->vec);
    tda_dealloc(al_copy, self, sizeof(tda_Stack));
}

tda_Vec *tda_stack_into_vec(tda_Stack *self) {
    ASSERT_STACK(self);

    tda_Vec *vec = self->vec;
    tda_dealloc(tda_vec_al(vec), self, sizeof(tda_Stack));

    return vec;
}

/* ========== copy ========== */

tda_Status tda_stack_copy(const tda_Stack *self, tda_Stack **out) {
    ASSERT_STACK(self);

    return tda_stack_copy_with(self, tda_vec_al(self->vec), out);
}

tda_Status tda_stack_copy_with(const tda_Stack *self, tda_Al *al, tda_Stack **out) {
    ASSERT_STACK(self);
    assert(al);
    assert(out);

    tda_Vec *vec;
    const tda_Status st = tda_vec_copy_with(self->vec, al, &vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    return wrap(vec, out);
}

tda_Status tda_stack_copy_assign(tda_Stack *self, const tda_Stack *other) {
    ASSERT_STACK(self);
    ASSERT_STACK(other);
    TDA_EXPECT(tda_vec_elem_size(self->vec) == tda_vec_elem_size(other->vec));

    // self assignment is left to the vec, which already returns early on it: a guard
    // repeated here would be a branch no test could tell from its absence
    return tda_vec_copy_assign(self->vec, other->vec);
}

tda_Status tda_stack_move_assign(tda_Stack *self, tda_Stack *other) {
    ASSERT_STACK(self);
    ASSERT_STACK(other);
    TDA_EXPECT(tda_vec_elem_size(self->vec) == tda_vec_elem_size(other->vec));

    // as in copy_assign, moving a stack onto itself is the vec's early return
    return tda_vec_move_assign(self->vec, other->vec);
}

/* ========== compare ========== */

bool tda_stack_eq(const tda_Stack *a, const tda_Stack *b) {
    ASSERT_STACK(a);
    ASSERT_STACK(b);

    return tda_vec_eq(a->vec, b->vec);
}

bool tda_stack_eq_by(const tda_Stack *a, const tda_Stack *b, tda_Eq eq) {
    ASSERT_STACK(a);
    ASSERT_STACK(b);

    return tda_vec_eq_by(a->vec, b->vec, eq);
}

/* ========== info ========== */

size_t tda_stack_len(const tda_Stack *self) {
    ASSERT_STACK(self);

    return tda_vec_len(self->vec);
}

size_t tda_stack_cap(const tda_Stack *self) {
    ASSERT_STACK(self);

    return tda_vec_cap(self->vec);
}

size_t tda_stack_elem_size(const tda_Stack *self) {
    ASSERT_STACK(self);

    return tda_vec_elem_size(self->vec);
}

tda_Al *tda_stack_al(const tda_Stack *self) {
    ASSERT_STACK(self);

    return tda_vec_al(self->vec);
}

/* ========== access ========== */

const void *tda_stack_top(const tda_Stack *self) {
    ASSERT_STACK(self);
    TDA_EXPECT(tda_vec_len(self->vec) > 0);

    return tda_vec_back(self->vec);
}

void *tda_stack_top_mut(tda_Stack *self) {
    ASSERT_STACK(self);
    TDA_EXPECT(tda_vec_len(self->vec) > 0);

    return tda_vec_back_mut(self->vec);
}

/* ========== mods ========== */

tda_Status tda_stack_push(tda_Stack *self, const void *val) {
    ASSERT_STACK(self);
    assert(val);

    return tda_vec_push(self->vec, val);
}

void tda_stack_pop(tda_Stack *self) {
    ASSERT_STACK(self);
    TDA_EXPECT(tda_vec_len(self->vec) > 0);

    tda_vec_pop(self->vec);
}

void tda_stack_clear(tda_Stack *self) {
    ASSERT_STACK(self);

    tda_vec_clear(self->vec);
}

tda_Status tda_stack_reserve(tda_Stack *self, size_t new_cap) {
    ASSERT_STACK(self);

    return tda_vec_reserve(self->vec, new_cap);
}

tda_Status tda_stack_shrink_to_fit(tda_Stack *self) {
    ASSERT_STACK(self);

    return tda_vec_shrink_to_fit(self->vec);
}

void tda_stack_swap(tda_Stack *self, tda_Stack *other) {
    ASSERT_STACK(self);
    ASSERT_STACK(other);
    TDA_EXPECT(tda_vec_elem_size(self->vec) == tda_vec_elem_size(other->vec));

    // as in copy_assign, swapping a stack with itself is the vec's early return
    tda_vec_swap(self->vec, other->vec);
}

/* ========== to span ========== */

tda_Span tda_stack_to_span(const tda_Stack *self) {
    ASSERT_STACK(self);

    return tda_vec_to_span(self->vec);
}

/* ========== print ========== */

void tda_stack_fprint(const tda_Stack *self, FILE *stream, tda_FPrint fprint) {
    ASSERT_STACK(self);

    tda_vec_fprint(self->vec, stream, fprint);
}

void tda_stack_print(const tda_Stack *self, tda_FPrint fprint) {
    ASSERT_STACK(self);

    tda_vec_print(self->vec, fprint);
}

/* ========== internals ========== */

static tda_Status wrap(tda_Vec *vec, tda_Stack **out) {
    assert(vec);
    assert(out);

    tda_Stack *obj = tda_alloc(tda_vec_al(vec), sizeof(tda_Stack));
    if (!obj) {
        tda_vec_drop(vec);
        return TDA_STATUS_ERR_NO_MEM;
    }

    obj->vec = vec;

    *out = obj;

    return TDA_STATUS_OK;
}
