#include "tda/ds/arr.h"

#include "tda/algo/compare.h"
#include "tda/core/check.h"
#include "tda/core/util.h"

#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <string.h>

/* ========== internals ========== */

#define ASSERT_ARR(a)                                                      \
    (assert(a),                                                            \
     assert(tda_layout_is_type((a)->elem_)),                               \
     assert((a)->len_ == 0 || (a)->data_),                                 \
     assert(!(a)->data_ || (a)->len_ > 0),                                 \
     assert(!(a)->data_ || tda_align_meets((a)->data_, (a)->elem_.align)), \
     assert((a)->al_.vt))

#define EXPECT_SAME_ARR(a, b) \
    (TDA_EXPECT(tda_layout_eq((a)->elem_, (b)->elem_)))

[[nodiscard]]
static tda_Status init_impl(bool zeroed, size_t len, tda_Layout elem, tda_Al al, tda_Arr *out);

/// the block as it was allocated: len elems at the elem's alignment
[[nodiscard]]
static tda_Layout block_layout(const tda_Arr *self);

[[nodiscard]]
static size_t len_bytes(const tda_Arr *self);

[[nodiscard]]
static unsigned char *elem_at_mut(tda_Arr *self, size_t idx);

/* ========== lifetime ========== */

tda_Status tda_arr_init_len(size_t len, tda_Layout elem, tda_Al al, tda_Arr *out) {
    assert(tda_layout_is_type(elem));
    assert(al.vt);
    assert(out);

    return init_impl(true, len, elem, al, out);
}

tda_Status tda_arr_from_data(const void *data, size_t len, tda_Layout elem, tda_Al al, tda_Arr *out) {
    assert(data || len == 0);
    assert(tda_layout_is_type(elem));
    assert(al.vt);
    assert(out);

    tda_Arr obj;
    const tda_Status st = init_impl(false, len, elem, al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    if (len > 0) {
        memcpy(obj.data_, data, len_bytes(&obj));
    }

    *out = obj;

    return TDA_STATUS_OK;
}

tda_Status tda_arr_from_span(tda_Span s, tda_Al al, tda_Arr *out) {
    TDA_SPAN_ASSERT(s);
    assert(al.vt);
    assert(out);

    return tda_arr_from_data(s.data, s.len, s.elem, al, out);
}

void tda_arr_deinit(tda_Arr *self) {
    assert(self);

    // the inert state, never initialized or already released
    if (!self->al_.vt) {
        assert(!self->data_);
        return;
    }

    ASSERT_ARR(self);

    tda_dealloc(self->al_, self->data_, block_layout(self));
    *self = (tda_Arr){};
}

/* ========== copy ========== */

tda_Status tda_arr_copy(const tda_Arr *self, tda_Arr *out) {
    ASSERT_ARR(self);
    assert(out);

    return tda_arr_copy_with(self, self->al_, out);
}

tda_Status tda_arr_copy_with(const tda_Arr *self, tda_Al al, tda_Arr *out) {
    ASSERT_ARR(self);
    assert(al.vt);
    assert(out);

    return tda_arr_from_span(tda_arr_to_span(self), al, out);
}

tda_Status tda_arr_copy_assign(tda_Arr *self, const tda_Arr *other) {
    ASSERT_ARR(self);
    ASSERT_ARR(other);
    EXPECT_SAME_ARR(self, other);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    const size_t self_bytes = len_bytes(self), other_bytes = len_bytes(other);

    if (self_bytes != other_bytes) {
        void *ptr = tda_realloc(self->al_, self->data_, block_layout(self), other_bytes);
        if (other_bytes > 0 && !ptr) {
            return TDA_STATUS_ERR_NO_MEM;
        }

        self->data_ = ptr;
        self->len_ = other->len_;
    }

    if (other_bytes > 0) {
        memcpy(self->data_, other->data_, other_bytes);
    }

    ASSERT_ARR(self);

    return TDA_STATUS_OK;
}

void tda_arr_move_assign(tda_Arr *self, tda_Arr *other) {
    ASSERT_ARR(self);
    ASSERT_ARR(other);
    EXPECT_SAME_ARR(self, other);

    if (self == other) {
        return;
    }

    // the allocator goes along with the block, and 'other' is left empty on it
    tda_arr_deinit(self);
    *self = *other;
    *other = (tda_Arr){.elem_ = self->elem_, .al_ = self->al_};

    ASSERT_ARR(self);
    ASSERT_ARR(other);
}

/* ========== compare ========== */

bool tda_arr_eq(const tda_Arr *a, const tda_Arr *b) {
    ASSERT_ARR(a);
    ASSERT_ARR(b);
    EXPECT_SAME_ARR(a, b);

    return tda_span_eq(tda_arr_to_span(a), tda_arr_to_span(b));
}

bool tda_arr_eq_by(const tda_Arr *a, const tda_Arr *b, tda_Eq eq, void *ctx) {
    ASSERT_ARR(a);
    ASSERT_ARR(b);
    assert(eq);
    EXPECT_SAME_ARR(a, b);

    return tda_span_eq_by(tda_arr_to_span(a), tda_arr_to_span(b), eq, ctx);
}

/* ========== access ========== */

void tda_arr_set(tda_Arr *self, size_t idx, const void *val) {
    ASSERT_ARR(self);
    assert(val);
    TDA_EXPECT(idx < self->len_);

    memmove(elem_at_mut(self, idx), val, self->elem_.size);
}

/* ========== mods ========== */

void tda_arr_swap(tda_Arr *self, tda_Arr *other) {
    ASSERT_ARR(self);
    ASSERT_ARR(other);
    EXPECT_SAME_ARR(self, other);

    if (self == other) {
        return;
    }

    TDA_SWAP(*self, *other);

    ASSERT_ARR(self);
    ASSERT_ARR(other);
}

void tda_arr_swap_elems(tda_Arr *self, size_t i, size_t j) {
    ASSERT_ARR(self);
    TDA_EXPECT(i < self->len_);
    TDA_EXPECT(j < self->len_);

    tda_span_swap_elems(tda_arr_to_span_mut(self), i, j);
}

/* ========== print ========== */

int tda_arr_fprint(const tda_Arr *self, FILE *stream, tda_FPrint fprint, void *ctx) {
    ASSERT_ARR(self);
    assert(stream);
    assert(fprint);

    return tda_span_fprint(tda_arr_to_span(self), stream, fprint, ctx);
}

int tda_arr_print(const tda_Arr *self, tda_FPrint fprint, void *ctx) {
    ASSERT_ARR(self);
    assert(fprint);

    return tda_fprint_join_(tda_arr_fprint(self, stdout, fprint, ctx), fputc('\n', stdout));
}

/* ========== internals ========== */

static tda_Status init_impl(bool zeroed, size_t len, tda_Layout elem, tda_Al al, tda_Arr *out) {
    assert(tda_layout_is_type(elem));
    assert(al.vt);
    assert(out);

    tda_Arr obj = {.len_ = len, .elem_ = elem, .al_ = al};
    if (len > 0) {
        tda_Layout block;
        if (tda_layout_ckd_array(&block, elem, len)) {
            return TDA_STATUS_ERR_NO_MEM;
        }
        obj.data_ = zeroed ? tda_alloc_zeroed(al, block) : tda_alloc(al, block);
        if (!obj.data_) {
            return TDA_STATUS_ERR_NO_MEM;
        }
    }

    *out = obj;

    ASSERT_ARR(out);

    return TDA_STATUS_OK;
}

static tda_Layout block_layout(const tda_Arr *self) {
    return tda_elems_layout(self->elem_, self->len_);
}

static size_t len_bytes(const tda_Arr *self) {
    return self->len_ * self->elem_.size;
}

static unsigned char *elem_at_mut(tda_Arr *self, size_t idx) {
    return tda_byte_offset_mut(self->data_, self->elem_.size, idx);
}
