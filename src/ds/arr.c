#include "tda/ds/arr.h"

#include "tda/algo/compare.h"
#include "tda/core/check.h"
#include "tda/core/util.h"

#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <string.h>

/* ========== internals ========== */

#define ASSERT_ARR(a)                    \
    (assert(a),                          \
     assert((a)->elem_size > 0),         \
     assert((a)->len == 0 || (a)->data), \
     assert(!(a)->data || (a)->len > 0), \
     assert((a)->al))

struct tda_Arr {
    void *data;
    size_t len;
    size_t elem_size;
    tda_Al *al;
};

[[nodiscard]]
static tda_Status new_impl(bool zeroed, size_t len, size_t elem_size, tda_Al *al, tda_Arr **out);

static void set_fields(tda_Arr *obj, void *data, size_t len, size_t elem_size, tda_Al *al);

/// hands the block back and leaves an empty arr on the same allocator
static void release_data(tda_Arr *self);

[[nodiscard]]
static size_t len_bytes(const tda_Arr *self);

[[nodiscard]]
static const unsigned char *arr_offset(const tda_Arr *self, size_t idx);

[[nodiscard]]
static unsigned char *arr_offset_mut(tda_Arr *self, size_t idx);

/* ========== lifetime ========== */

tda_Status tda_arr_new_len(size_t len, size_t elem_size, tda_Al *al, tda_Arr **out) {
    assert(elem_size > 0);
    assert(al);
    assert(out);

    return new_impl(true, len, elem_size, al, out);
}

tda_Status tda_arr_from_data(const void *data, size_t len, size_t elem_size, tda_Al *al, tda_Arr **out) {
    assert(data || len == 0);
    assert(elem_size > 0);
    assert(al);
    assert(out);

    tda_Arr *arr;
    const tda_Status st = new_impl(false, len, elem_size, al, &arr);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    if (len > 0) {
        memcpy(arr->data, data, len_bytes(arr));
    }

    *out = arr;

    return TDA_STATUS_OK;
}

tda_Status tda_arr_from_span(tda_Span s, tda_Al *al, tda_Arr **out) {
    TDA_SPAN_ASSERT(s);
    assert(al);
    assert(out);

    return tda_arr_from_data(s.data, s.len, s.elem_size, al, out);
}

void tda_arr_drop(tda_Arr *self) {
    if (!self) {
        return;
    }

    ASSERT_ARR(self);

    tda_Al *al_copy = self->al;
    tda_dealloc(al_copy, self->data, len_bytes(self));
    tda_dealloc(al_copy, self, sizeof(tda_Arr));
}

/* ========== copy ========== */

tda_Status tda_arr_copy(const tda_Arr *self, tda_Arr **out) {
    ASSERT_ARR(self);

    return tda_arr_copy_with(self, self->al, out);
}

tda_Status tda_arr_copy_with(const tda_Arr *self, tda_Al *al, tda_Arr **out) {
    ASSERT_ARR(self);
    assert(al);

    return tda_arr_from_span(tda_arr_to_span(self), al, out);
}

tda_Status tda_arr_copy_assign(tda_Arr *self, const tda_Arr *other) {
    ASSERT_ARR(self);
    ASSERT_ARR(other);
    TDA_EXPECT(self->elem_size == other->elem_size);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    const size_t self_bytes = len_bytes(self);
    const size_t other_bytes = len_bytes(other);

    if (self_bytes != other_bytes) {
        void *new_data = tda_realloc(self->al, self->data, self_bytes, other_bytes);
        if (other_bytes > 0 && !new_data) {
            return TDA_STATUS_ERR_NO_MEM;
        }

        self->data = new_data;
        self->len = other->len;
    }

    if (other_bytes > 0) {
        memcpy(self->data, other->data, other_bytes);
    }

    ASSERT_ARR(self);

    return TDA_STATUS_OK;
}

tda_Status tda_arr_move_assign(tda_Arr *self, tda_Arr *other) {
    ASSERT_ARR(self);
    ASSERT_ARR(other);
    TDA_EXPECT(self->elem_size == other->elem_size);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    // one allocator: the block is handed over as it is. What 'self' held ends up in 'other' and is released
    // there, through the very allocator that made it
    if (self->al == other->al) {
        TDA_SWAP(*self, *other);
        release_data(other);

        ASSERT_ARR(self);
        ASSERT_ARR(other);

        return TDA_STATUS_OK;
    }

    // two allocators: the whole copy is built on the target's before anything of it is
    // touched, so a refusal leaves both as they were
    tda_Arr *obj;
    const tda_Status st = tda_arr_copy_with(other, self->al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    TDA_SWAP(*self, *obj);
    tda_arr_drop(obj);
    release_data(other);

    ASSERT_ARR(self);
    ASSERT_ARR(other);

    return TDA_STATUS_OK;
}

/* ========== compare ========== */

bool tda_arr_eq(const tda_Arr *a, const tda_Arr *b) {
    ASSERT_ARR(a);
    ASSERT_ARR(b);

    return tda_span_eq(tda_arr_to_span(a), tda_arr_to_span(b));
}

bool tda_arr_eq_by(const tda_Arr *a, const tda_Arr *b, tda_Eq eq) {
    ASSERT_ARR(a);
    ASSERT_ARR(b);
    assert(eq);

    return tda_span_eq_by(tda_arr_to_span(a), tda_arr_to_span(b), eq);
}

/* ========== info ========== */

size_t tda_arr_len(const tda_Arr *self) {
    ASSERT_ARR(self);

    return self->len;
}

size_t tda_arr_elem_size(const tda_Arr *self) {
    ASSERT_ARR(self);

    return self->elem_size;
}

size_t tda_arr_bytes(const tda_Arr *self) {
    ASSERT_ARR(self);

    return len_bytes(self);
}

tda_Al *tda_arr_al(const tda_Arr *self) {
    ASSERT_ARR(self);

    return self->al;
}

/* ========== access ========== */

const void *tda_arr_front(const tda_Arr *self) {
    ASSERT_ARR(self);
    TDA_EXPECT(self->len > 0);

    return arr_offset(self, 0);
}

void *tda_arr_front_mut(tda_Arr *self) {
    ASSERT_ARR(self);
    TDA_EXPECT(self->len > 0);

    return arr_offset_mut(self, 0);
}

const void *tda_arr_back(const tda_Arr *self) {
    ASSERT_ARR(self);
    TDA_EXPECT(self->len > 0);

    return arr_offset(self, self->len - 1);
}

void *tda_arr_back_mut(tda_Arr *self) {
    ASSERT_ARR(self);
    TDA_EXPECT(self->len > 0);

    return arr_offset_mut(self, self->len - 1);
}

const void *tda_arr_get(const tda_Arr *self, size_t idx) {
    ASSERT_ARR(self);
    TDA_EXPECT(idx < self->len);

    return arr_offset(self, idx);
}

void *tda_arr_get_mut(tda_Arr *self, size_t idx) {
    ASSERT_ARR(self);
    TDA_EXPECT(idx < self->len);

    return arr_offset_mut(self, idx);
}

void tda_arr_set(tda_Arr *self, size_t idx, const void *val) {
    ASSERT_ARR(self);
    assert(val);
    TDA_EXPECT(idx < self->len);

    memcpy(arr_offset_mut(self, idx), val, self->elem_size);
}

const void *tda_arr_data(const tda_Arr *self) {
    ASSERT_ARR(self);

    return self->data;
}

void *tda_arr_data_mut(tda_Arr *self) {
    ASSERT_ARR(self);

    return self->data;
}

/* ========== mods ========== */

void tda_arr_swap(tda_Arr *self, tda_Arr *other) {
    ASSERT_ARR(self);
    ASSERT_ARR(other);
    TDA_EXPECT(self->elem_size == other->elem_size);
    TDA_EXPECT(self->al == other->al);

    if (self == other) {
        return;
    }

    TDA_SWAP(*self, *other);

    ASSERT_ARR(self);
    ASSERT_ARR(other);
}

void tda_arr_swap_elems(tda_Arr *self, size_t i, size_t j) {
    ASSERT_ARR(self);

    tda_span_swap_elems(tda_arr_to_span_mut(self), i, j);
}

/* ========== to span ========== */

tda_SpanMut tda_arr_to_span_mut(tda_Arr *self) {
    ASSERT_ARR(self);

    return tda_span_from_data_mut(self->data, self->len, self->elem_size);
}

tda_Span tda_arr_to_span(const tda_Arr *self) {
    ASSERT_ARR(self);

    return tda_span_from_data(self->data, self->len, self->elem_size);
}

/* ========== print ========== */

void tda_arr_fprint(const tda_Arr *self, FILE *stream, tda_FPrint fprint) {
    ASSERT_ARR(self);
    assert(stream);
    assert(fprint);

    tda_span_fprint(tda_arr_to_span(self), stream, fprint);
}

void tda_arr_print(const tda_Arr *self, tda_FPrint fprint) {
    ASSERT_ARR(self);
    assert(fprint);

    tda_arr_fprint(self, stdout, fprint);
}

/* ========== internals ========== */

static tda_Status new_impl(bool zeroed, size_t len, size_t elem_size, tda_Al *al, tda_Arr **out) {
    assert(elem_size > 0);
    assert(al);
    assert(out);

    tda_Arr *obj = tda_alloc(al, sizeof(tda_Arr));
    if (!obj) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    void *data = nullptr;

    if (len > 0) {
        size_t bytes;
        if (ckd_mul(&bytes, len, elem_size)) {
            goto fail;
        }
        data = zeroed ? tda_calloc(al, len, elem_size) : tda_alloc(al, bytes);
        if (!data) {
            goto fail;
        }
    }

    set_fields(obj, data, len, elem_size, al);

    ASSERT_ARR(obj);

    *out = obj;
    return TDA_STATUS_OK;

fail:
    tda_dealloc(al, obj, sizeof(tda_Arr));
    return TDA_STATUS_ERR_NO_MEM;
}

static void set_fields(tda_Arr *obj, void *data, size_t len, size_t elem_size, tda_Al *al) {
    obj->data = data;
    obj->len = len;
    obj->elem_size = elem_size;
    obj->al = al;
}

static void release_data(tda_Arr *self) {
    tda_dealloc(self->al, self->data, len_bytes(self));
    self->data = nullptr;
    self->len = 0;
}

static size_t len_bytes(const tda_Arr *self) {
    return self->len * self->elem_size;
}

static const unsigned char *arr_offset(const tda_Arr *self, size_t idx) {
    return tda_byte_offset(self->data, self->elem_size, idx);
}

static unsigned char *arr_offset_mut(tda_Arr *self, size_t idx) {
    return tda_byte_offset_mut(self->data, self->elem_size, idx);
}
