#include "tda/ds/vec.h"

#include "tda/algo/compare.h"
#include "tda/core/check.h"

#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <string.h>

/* ========== internals ========== */

#define ASSERT_VEC(v)                        \
    (assert(v),                              \
     assert(tda_layout_is_type((v)->elem_)), \
     assert((v)->len_ <= (v)->cap_),         \
     assert((v)->cap_ == 0 || (v)->data_),   \
     assert(!(v)->data_ || (v)->cap_ > 0),   \
     assert((v)->al_.vt))

#define EXPECT_SAME_VEC(a, b) \
    (TDA_EXPECT(tda_layout_eq((a)->elem_, (b)->elem_)))

static constexpr size_t VEC_GROWTH_BASE = 1;
static constexpr size_t VEC_GROWTH_FACTOR = 2;

[[nodiscard]]
static tda_Status init_impl(bool zeroed, size_t len, size_t cap, tda_Layout elem, tda_Al al, tda_Vec *out);

/// frees the block, leaving an empty vec on the same allocator
static void release_data(tda_Vec *self);

/// puts 'count' elems read from 'src' at 'idx', moving the tail up once; the shared
/// body of insert and insert_span
[[nodiscard]]
static tda_Status insert_raw(tda_Vec *self, size_t idx, const void *src, size_t count);

[[nodiscard]]
static size_t next_cap(const tda_Vec *self);

/// room for one more elem
[[nodiscard]]
static tda_Status reserve_one(tda_Vec *self);

/// room for 'new_len' elems: grows by the growth factor when that is more, so repeated
/// extends stay amortized, and falls back to the exact size if that is refused
[[nodiscard]]
static tda_Status reserve_for(tda_Vec *self, size_t new_len);

/// moves the elems into a block of exactly 'new_cap', which holds them all
[[nodiscard]]
static tda_Status set_cap(tda_Vec *self, size_t new_cap);

[[nodiscard]]
static size_t len_bytes(const tda_Vec *self);

/// the block as it was allocated: cap elems at the elem's alignment
[[nodiscard]]
static tda_Layout block_layout(const tda_Vec *self);

/// the whole block as a view, the slots past the length included: what growth may free
[[nodiscard]] [[maybe_unused]]
static tda_Span block_span(const tda_Vec *self);

[[nodiscard]]
static const unsigned char *elem_at(const tda_Vec *self, size_t idx);

[[nodiscard]]
static unsigned char *elem_at_mut(tda_Vec *self, size_t idx);

/* ========== lifetime ========== */

tda_Vec tda_vec_init(tda_Layout elem, tda_Al al) {
    assert(tda_layout_is_type(elem));
    assert(al.vt);

    return (tda_Vec){.elem_ = elem, .al_ = al};
}

tda_Status tda_vec_init_len(size_t len, tda_Layout elem, tda_Al al, tda_Vec *out) {
    assert(tda_layout_is_type(elem));
    assert(al.vt);
    assert(out);

    return init_impl(true, len, len, elem, al, out);
}

tda_Status tda_vec_init_cap(size_t cap, tda_Layout elem, tda_Al al, tda_Vec *out) {
    assert(tda_layout_is_type(elem));
    assert(al.vt);
    assert(out);

    return init_impl(false, 0, cap, elem, al, out);
}

tda_Status tda_vec_from_data(const void *data, size_t len, tda_Layout elem, tda_Al al, tda_Vec *out) {
    assert(data || len == 0);
    assert(tda_layout_is_type(elem));
    assert(al.vt);
    assert(out);

    tda_Vec obj;
    const tda_Status st = init_impl(false, len, len, elem, al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    if (len > 0) {
        memcpy(obj.data_, data, len_bytes(&obj));
    }

    *out = obj;

    return TDA_STATUS_OK;
}

tda_Status tda_vec_from_span(tda_Span s, tda_Al al, tda_Vec *out) {
    TDA_SPAN_ASSERT(s);
    assert(al.vt);
    assert(out);

    return tda_vec_from_data(s.data, s.len, s.elem, al, out);
}

void tda_vec_deinit(tda_Vec *self) {
    assert(self);

    // the inert state, never initialized or already released
    if (!self->al_.vt) {
        assert(!self->data_);
        return;
    }

    ASSERT_VEC(self);

    tda_dealloc(self->al_, self->data_, block_layout(self));
    *self = (tda_Vec){};
}

/* ========== copy ========== */

tda_Status tda_vec_copy(const tda_Vec *self, tda_Vec *out) {
    ASSERT_VEC(self);
    assert(out);

    return tda_vec_copy_with(self, self->al_, out);
}

tda_Status tda_vec_copy_with(const tda_Vec *self, tda_Al al, tda_Vec *out) {
    ASSERT_VEC(self);
    assert(al.vt);
    assert(out);

    return tda_vec_from_span(tda_vec_to_span(self), al, out);
}

tda_Status tda_vec_copy_assign(tda_Vec *self, const tda_Vec *other) {
    ASSERT_VEC(self);
    ASSERT_VEC(other);
    EXPECT_SAME_VEC(self, other);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    const tda_Status st = tda_vec_reserve(self, other->len_);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    if (other->len_ > 0) {
        memcpy(self->data_, other->data_, len_bytes(other));
    }

    self->len_ = other->len_;

    ASSERT_VEC(self);

    return TDA_STATUS_OK;
}

void tda_vec_move_assign(tda_Vec *self, tda_Vec *other) {
    ASSERT_VEC(self);
    ASSERT_VEC(other);
    EXPECT_SAME_VEC(self, other);

    if (self == other) {
        return;
    }

    // the allocator goes along with the block, and 'other' stays usable on it
    tda_vec_deinit(self);
    *self = *other;
    *other = tda_vec_init(self->elem_, self->al_);

    ASSERT_VEC(self);
    ASSERT_VEC(other);
}

/* ========== compare ========== */

bool tda_vec_eq(const tda_Vec *a, const tda_Vec *b) {
    ASSERT_VEC(a);
    ASSERT_VEC(b);
    EXPECT_SAME_VEC(a, b);

    return tda_span_eq(tda_vec_to_span(a), tda_vec_to_span(b));
}

bool tda_vec_eq_by(const tda_Vec *a, const tda_Vec *b, tda_Eq eq, void *ctx) {
    ASSERT_VEC(a);
    ASSERT_VEC(b);
    assert(eq);
    EXPECT_SAME_VEC(a, b);

    return tda_span_eq_by(tda_vec_to_span(a), tda_vec_to_span(b), eq, ctx);
}

/* ========== access ========== */

void tda_vec_set(tda_Vec *self, size_t idx, const void *val) {
    ASSERT_VEC(self);
    assert(val);
    TDA_EXPECT(idx < self->len_);

    // memmove: 'val' may alias the elem being set
    memmove(elem_at_mut(self, idx), val, self->elem_.size);
}

/* ========== mods ========== */

tda_Status tda_vec_push(tda_Vec *self, const void *val) {
    ASSERT_VEC(self);
    assert(val);
    // growing frees the block 'val' would be read from
    TDA_EXPECT(!tda_addr_in(self->data_, block_layout(self).size, val));

    const tda_Status st = reserve_one(self);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    memcpy(elem_at_mut(self, self->len_), val, self->elem_.size);
    ++self->len_;

    ASSERT_VEC(self);

    return TDA_STATUS_OK;
}

tda_Status tda_vec_add_one(tda_Vec *self, void **out) {
    ASSERT_VEC(self);
    assert(out);

    const tda_Status st = reserve_one(self);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    *out = elem_at_mut(self, self->len_);
    ++self->len_;

    ASSERT_VEC(self);

    return TDA_STATUS_OK;
}

void tda_vec_pop_to(tda_Vec *self, void *out) {
    ASSERT_VEC(self);
    assert(out);
    TDA_EXPECT(self->len_ > 0);

    --self->len_;
    // memmove: 'out' may alias the dropped slot
    memmove(out, elem_at(self, self->len_), self->elem_.size);

    ASSERT_VEC(self);
}

tda_Status tda_vec_insert(tda_Vec *self, size_t idx, const void *val) {
    ASSERT_VEC(self);
    assert(val);
    TDA_EXPECT(idx <= self->len_);
    // growing frees the block 'val' would be read from
    TDA_EXPECT(!tda_addr_in(self->data_, block_layout(self).size, val));

    // straight from 'val', not through a span, which would ask it to be aligned
    return insert_raw(self, idx, val, 1);
}

void tda_vec_remove(tda_Vec *self, size_t idx) {
    ASSERT_VEC(self);
    TDA_EXPECT(idx < self->len_);

    tda_vec_remove_range(self, idx, 1);
}

void tda_vec_swap_remove(tda_Vec *self, size_t idx) {
    ASSERT_VEC(self);
    TDA_EXPECT(idx < self->len_);

    --self->len_;
    // removing the back elem leaves no gap; memcpy onto itself is undefined
    if (idx != self->len_) {
        memcpy(elem_at_mut(self, idx), elem_at(self, self->len_), self->elem_.size);
    }

    ASSERT_VEC(self);
}

tda_Status tda_vec_reserve(tda_Vec *self, size_t new_cap) {
    ASSERT_VEC(self);

    if (new_cap <= self->cap_) {
        return TDA_STATUS_OK;
    }

    const tda_Status st = set_cap(self, new_cap);

    ASSERT_VEC(self);

    return st;
}

tda_Status tda_vec_shrink_to_fit(tda_Vec *self) {
    ASSERT_VEC(self);

    if (self->len_ == self->cap_) {
        return TDA_STATUS_OK;
    }

    if (self->len_ == 0) {
        release_data(self);
        ASSERT_VEC(self);
        return TDA_STATUS_OK;
    }

    const tda_Status st = set_cap(self, self->len_);

    ASSERT_VEC(self);

    return st;
}

tda_Status tda_vec_resize(tda_Vec *self, size_t new_len) {
    ASSERT_VEC(self);

    if (new_len <= self->len_) {
        self->len_ = new_len;
        ASSERT_VEC(self);
        return TDA_STATUS_OK;
    }

    const tda_Status st = reserve_for(self, new_len);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // resize promises zeroed elems; reserve_for leaves the new room unspecified
    const size_t add_bytes = (new_len - self->len_) * self->elem_.size;
    memset(elem_at_mut(self, self->len_), 0, add_bytes);
    self->len_ = new_len;

    ASSERT_VEC(self);

    return TDA_STATUS_OK;
}

void tda_vec_truncate(tda_Vec *self, size_t new_len) {
    ASSERT_VEC(self);

    if (new_len < self->len_) {
        self->len_ = new_len;
    }

    ASSERT_VEC(self);
}

void tda_vec_swap(tda_Vec *self, tda_Vec *other) {
    ASSERT_VEC(self);
    ASSERT_VEC(other);
    EXPECT_SAME_VEC(self, other);

    if (self == other) {
        return;
    }

    TDA_SWAP(*self, *other);

    ASSERT_VEC(self);
    ASSERT_VEC(other);
}

void tda_vec_swap_elems(tda_Vec *self, size_t i, size_t j) {
    ASSERT_VEC(self);
    TDA_EXPECT(i < self->len_);
    TDA_EXPECT(j < self->len_);

    tda_span_swap_elems(tda_vec_to_span_mut(self), i, j);
}

/* ========== bulk mods ========== */

tda_Status tda_vec_extend(tda_Vec *self, tda_Span src) {
    ASSERT_VEC(self);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(tda_layout_eq(self->elem_, src.elem));
    // growing may free what 'src' views
    TDA_EXPECT(!tda_span_overlap(block_span(self), src));

    return tda_vec_insert_span(self, self->len_, src);
}

tda_Status tda_vec_insert_span(tda_Vec *self, size_t idx, tda_Span src) {
    ASSERT_VEC(self);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(tda_layout_eq(self->elem_, src.elem));
    TDA_EXPECT(idx <= self->len_);
    // growing may free what 'src' views
    TDA_EXPECT(!tda_span_overlap(block_span(self), src));

    return insert_raw(self, idx, src.data, src.len);
}

void tda_vec_remove_range(tda_Vec *self, size_t idx, size_t count) {
    ASSERT_VEC(self);
    TDA_EXPECT(idx <= self->len_);
    TDA_EXPECT(count <= self->len_ - idx);

    if (count == 0) {
        return;
    }

    const size_t tail = self->len_ - idx - count;
    if (tail > 0) {
        memmove(elem_at_mut(self, idx), elem_at_mut(self, idx + count), tail * self->elem_.size);
    }

    self->len_ -= count;

    ASSERT_VEC(self);
}

void tda_vec_retain(tda_Vec *self, tda_Pred pred, void *ctx) {
    ASSERT_VEC(self);
    assert(pred);

    size_t kept = 0;
    for (size_t i = 0; i < self->len_; ++i) {
        if (!pred(elem_at(self, i), ctx)) {
            continue;
        }
        // until the first drop every kept elem is already in place
        if (kept != i) {
            memcpy(elem_at_mut(self, kept), elem_at(self, i), self->elem_.size);
        }
        ++kept;
    }
    self->len_ = kept;

    ASSERT_VEC(self);
}

/* ========== print ========== */

int tda_vec_fprint(const tda_Vec *self, FILE *stream, tda_FPrint fprint, void *ctx) {
    ASSERT_VEC(self);
    assert(stream);
    assert(fprint);

    return tda_span_fprint(tda_vec_to_span(self), stream, fprint, ctx);
}

int tda_vec_print(const tda_Vec *self, tda_FPrint fprint, void *ctx) {
    ASSERT_VEC(self);
    assert(fprint);

    return tda_fprint_join_(tda_vec_fprint(self, stdout, fprint, ctx), fputc('\n', stdout));
}

/* ========== internals ========== */

static tda_Status insert_raw(tda_Vec *self, size_t idx, const void *src, size_t count) {
    if (count == 0) {
        return TDA_STATUS_OK;
    }

    size_t new_len;
    if (ckd_add(&new_len, self->len_, count)) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    const tda_Status st = reserve_for(self, new_len);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // one move for the whole run, not one per elem
    const size_t tail = self->len_ - idx;
    if (tail > 0) {
        memmove(elem_at_mut(self, idx + count), elem_at_mut(self, idx), tail * self->elem_.size);
    }

    memcpy(elem_at_mut(self, idx), src, count * self->elem_.size);
    self->len_ = new_len;

    ASSERT_VEC(self);

    return TDA_STATUS_OK;
}

[[nodiscard]]
static tda_Status init_impl(bool zeroed, size_t len, size_t cap, tda_Layout elem, tda_Al al, tda_Vec *out) {
    assert(len <= cap);

    tda_Vec obj = tda_vec_init(elem, al);
    if (cap > 0) {
        tda_Layout block;
        if (tda_layout_ckd_array(&block, elem, cap)) {
            return TDA_STATUS_ERR_NO_MEM;
        }
        obj.data_ = tda_alloc(al, block);
        if (!obj.data_) {
            return TDA_STATUS_ERR_NO_MEM;
        }
        if (zeroed) {
            memset(obj.data_, 0, len * elem.size);
        }
    }
    obj.len_ = len;
    obj.cap_ = cap;

    *out = obj;

    ASSERT_VEC(out);

    return TDA_STATUS_OK;
}

static size_t next_cap(const tda_Vec *self) {
    if (self->cap_ == 0) {
        return VEC_GROWTH_BASE;
    }

    size_t grown;
    if (ckd_mul(&grown, self->cap_, VEC_GROWTH_FACTOR)) {
        return SIZE_MAX;
    }

    return grown;
}

static tda_Status reserve_one(tda_Vec *self) {
    if (self->len_ < self->cap_) {
        return TDA_STATUS_OK;
    }

    if (self->cap_ == SIZE_MAX) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    return reserve_for(self, self->cap_ + 1);
}

static tda_Status set_cap(tda_Vec *self, size_t new_cap) {
    assert(new_cap >= self->len_);
    assert(new_cap > 0);

    tda_Layout block;
    if (tda_layout_ckd_array(&block, self->elem_, new_cap)) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    void *ptr = tda_realloc(self->al_, self->data_, block_layout(self), block.size);
    if (!ptr) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    self->data_ = ptr;
    self->cap_ = new_cap;

    return TDA_STATUS_OK;
}

static tda_Status reserve_for(tda_Vec *self, size_t new_len) {
    if (new_len <= self->cap_) {
        return TDA_STATUS_OK;
    }

    const size_t eager = next_cap(self);
    if (eager > new_len) {
        const tda_Status st = set_cap(self, eager);
        if (TDA_STATUS_IS_OK(st)) {
            return st;
        }
    }

    return set_cap(self, new_len);
}

static void release_data(tda_Vec *self) {
    tda_dealloc(self->al_, self->data_, block_layout(self));
    self->data_ = nullptr;
    self->len_ = 0;
    self->cap_ = 0;
}

static size_t len_bytes(const tda_Vec *self) {
    return self->len_ * self->elem_.size;
}

static tda_Layout block_layout(const tda_Vec *self) {
    return tda_elems_layout(self->elem_, self->cap_);
}

static tda_Span block_span(const tda_Vec *self) {
    return (tda_Span){.data = self->data_, .len = self->cap_, .elem = self->elem_};
}

static const unsigned char *elem_at(const tda_Vec *self, size_t idx) {
    return tda_byte_offset(self->data_, self->elem_.size, idx);
}

static unsigned char *elem_at_mut(tda_Vec *self, size_t idx) {
    return tda_byte_offset_mut(self->data_, self->elem_.size, idx);
}
