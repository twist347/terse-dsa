#include "tda/ds/vec.h"

#include "tda/algo/compare.h"

#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <string.h>

/* ========== internals ========== */

#define ASSERT_VEC(v)                    \
    (assert(v),                          \
     assert((v)->elem_size > 0),         \
     assert((v)->len <= (v)->cap),       \
     assert((v)->cap == 0 || (v)->data), \
     assert(!(v)->data || (v)->cap > 0), \
     assert((v)->al))

static constexpr size_t VEC_GROWTH_BASE = 1;
static constexpr size_t VEC_GROWTH_FACTOR = 2;

struct tda_Vec {
    void *data;
    size_t len;
    size_t cap;
    size_t elem_size;
    tda_Al *al;
};

[[nodiscard]]
static tda_Status new_impl(bool zeroed, size_t len, size_t cap, size_t elem_size, tda_Al *al, tda_Vec **out);

static void set_fields(tda_Vec *obj, void *data, size_t len, size_t cap, size_t elem_size, tda_Al *al);

/// hands the block back and leaves an empty vec on the same allocator
static void release_data(tda_Vec *self);

[[nodiscard]]
static size_t next_cap(const tda_Vec *self);

[[nodiscard]]
static tda_Status grow(tda_Vec *self);

/// room for one more elem, growing the block when it is full
[[nodiscard]]
static tda_Status reserve_one(tda_Vec *self);

/// room for 'new_len' elems, asked for with the growth factor when that is the bigger of
/// the two so a run of extends keeps the amortized cost a run of pushes has. Falls back
/// to the exact length when the eager request is refused
[[nodiscard]]
static tda_Status reserve_for(tda_Vec *self, size_t new_len);

[[nodiscard]]
static size_t len_bytes(const tda_Vec *self);

[[nodiscard]]
static size_t cap_bytes(const tda_Vec *self);

[[nodiscard]]
static const unsigned char *vec_offset(const tda_Vec *self, size_t idx);

[[nodiscard]]
static unsigned char *vec_offset_mut(tda_Vec *self, size_t idx);

/* ========== lifetime ========== */

tda_Status tda_vec_new(size_t elem_size, tda_Al *al, tda_Vec **out) {
    assert(elem_size > 0);
    assert(al);
    assert(out);

    return new_impl(false, 0, 0, elem_size, al, out);
}

tda_Status tda_vec_new_len(size_t len, size_t elem_size, tda_Al *al, tda_Vec **out) {
    assert(elem_size > 0);
    assert(al);
    assert(out);

    return new_impl(true, len, len, elem_size, al, out);
}

tda_Status tda_vec_new_cap(size_t cap, size_t elem_size, tda_Al *al, tda_Vec **out) {
    assert(elem_size > 0);
    assert(al);
    assert(out);

    return new_impl(false, 0, cap, elem_size, al, out);
}

tda_Status tda_vec_from_data(const void *data, size_t len, size_t elem_size, tda_Al *al, tda_Vec **out) {
    assert(data || len == 0);
    assert(elem_size > 0);
    assert(al);
    assert(out);

    tda_Vec *vec;
    const tda_Status st = new_impl(false, len, len, elem_size, al, &vec);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    if (len > 0) {
        memcpy(vec->data, data, len_bytes(vec));
    }

    *out = vec;

    return TDA_STATUS_OK;
}

tda_Status tda_vec_from_span(tda_Span s, tda_Al *al, tda_Vec **out) {
    TDA_SPAN_ASSERT(s);
    assert(al);
    assert(out);

    return tda_vec_from_data(s.data, s.len, s.elem_size, al, out);
}

void tda_vec_drop(tda_Vec *self) {
    if (!self) {
        return;
    }

    ASSERT_VEC(self);

    tda_Al *al_copy = self->al;
    tda_dealloc(al_copy, self->data, cap_bytes(self));
    tda_dealloc(al_copy, self, sizeof(tda_Vec));
}

/* ========== copy ========== */

tda_Status tda_vec_copy(const tda_Vec *self, tda_Vec **out) {
    ASSERT_VEC(self);

    return tda_vec_copy_with(self, self->al, out);
}

tda_Status tda_vec_copy_with(const tda_Vec *self, tda_Al *al, tda_Vec **out) {
    ASSERT_VEC(self);
    assert(al);

    return tda_vec_from_span(tda_vec_to_span(self), al, out);
}

tda_Status tda_vec_copy_assign(tda_Vec *self, const tda_Vec *other) {
    ASSERT_VEC(self);
    ASSERT_VEC(other);
    assert(self->elem_size == other->elem_size);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    const tda_Status st = tda_vec_reserve(self, other->len);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    if (other->len > 0) {
        memcpy(self->data, other->data, len_bytes(other));
    }

    self->len = other->len;

    ASSERT_VEC(self);

    return TDA_STATUS_OK;
}

tda_Status tda_vec_move_assign(tda_Vec *self, tda_Vec *other) {
    ASSERT_VEC(self);
    ASSERT_VEC(other);
    assert(self->elem_size == other->elem_size);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    // one allocator: the block is handed over, capacity and all. What 'self' held ends up in 'other' and is released
    // there, through the very allocator that made it
    if (self->al == other->al) {
        TDA_SWAP(*self, *other);
        release_data(other);

        ASSERT_VEC(self);
        ASSERT_VEC(other);

        return TDA_STATUS_OK;
    }

    // two allocators: the whole copy is built on the target's before anything of it is
    // touched, so a refusal leaves both as they were
    tda_Vec *obj;
    const tda_Status st = tda_vec_copy_with(other, self->al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    TDA_SWAP(*self, *obj);
    tda_vec_drop(obj);
    release_data(other);

    ASSERT_VEC(self);
    ASSERT_VEC(other);

    return TDA_STATUS_OK;
}

/* ========== compare ========== */

bool tda_vec_eq(const tda_Vec *a, const tda_Vec *b) {
    ASSERT_VEC(a);
    ASSERT_VEC(b);

    return tda_span_eq(tda_vec_to_span(a), tda_vec_to_span(b));
}

bool tda_vec_eq_by(const tda_Vec *a, const tda_Vec *b, tda_Eq eq) {
    ASSERT_VEC(a);
    ASSERT_VEC(b);
    assert(eq);

    return tda_span_eq_by(tda_vec_to_span(a), tda_vec_to_span(b), eq);
}

/* ========== info ========== */

size_t tda_vec_len(const tda_Vec *self) {
    ASSERT_VEC(self);

    return self->len;
}

size_t tda_vec_cap(const tda_Vec *self) {
    ASSERT_VEC(self);

    return self->cap;
}

size_t tda_vec_elem_size(const tda_Vec *self) {
    ASSERT_VEC(self);

    return self->elem_size;
}

size_t tda_vec_bytes(const tda_Vec *self) {
    ASSERT_VEC(self);

    return len_bytes(self);
}

tda_Al *tda_vec_al(const tda_Vec *self) {
    ASSERT_VEC(self);

    return self->al;
}

/* ========== access ========== */

const void *tda_vec_front(const tda_Vec *self) {
    ASSERT_VEC(self);
    assert(self->len > 0);

    return vec_offset(self, 0);
}

void *tda_vec_front_mut(tda_Vec *self) {
    ASSERT_VEC(self);
    assert(self->len > 0);

    return vec_offset_mut(self, 0);
}

const void *tda_vec_back(const tda_Vec *self) {
    ASSERT_VEC(self);
    assert(self->len > 0);

    return vec_offset(self, self->len - 1);
}

void *tda_vec_back_mut(tda_Vec *self) {
    ASSERT_VEC(self);
    assert(self->len > 0);

    return vec_offset_mut(self, self->len - 1);
}

const void *tda_vec_get(const tda_Vec *self, size_t idx) {
    ASSERT_VEC(self);
    assert(idx < self->len);

    return vec_offset(self, idx);
}

void *tda_vec_get_mut(tda_Vec *self, size_t idx) {
    ASSERT_VEC(self);
    assert(idx < self->len);

    return vec_offset_mut(self, idx);
}

void tda_vec_set(tda_Vec *self, size_t idx, const void *val) {
    ASSERT_VEC(self);
    assert(val);
    assert(idx < self->len);

    memcpy(vec_offset_mut(self, idx), val, self->elem_size);
}

const void *tda_vec_data(const tda_Vec *self) {
    ASSERT_VEC(self);

    return self->data;
}

void *tda_vec_data_mut(tda_Vec *self) {
    ASSERT_VEC(self);

    return self->data;
}

/* ========== mods ========== */

tda_Status tda_vec_push(tda_Vec *self, const void *val) {
    ASSERT_VEC(self);
    assert(val);

    const tda_Status st = reserve_one(self);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    memcpy(vec_offset_mut(self, self->len), val, self->elem_size);
    ++self->len;

    return TDA_STATUS_OK;
}

void tda_vec_pop(tda_Vec *self) {
    ASSERT_VEC(self);
    assert(self->len > 0);

    --self->len;
}

tda_Status tda_vec_insert(tda_Vec *self, size_t idx, const void *val) {
    ASSERT_VEC(self);
    assert(val);
    assert(idx <= self->len);

    const tda_Status st = reserve_one(self);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    const size_t tail = self->len - idx;
    if (tail > 0) {
        memmove(vec_offset_mut(self, idx + 1), vec_offset_mut(self, idx), tail * self->elem_size);
    }

    memcpy(vec_offset_mut(self, idx), val, self->elem_size);
    ++self->len;

    return TDA_STATUS_OK;
}

void tda_vec_remove(tda_Vec *self, size_t idx) {
    ASSERT_VEC(self);
    assert(idx < self->len);

    const size_t tail = self->len - idx - 1;
    if (tail > 0) {
        memmove(vec_offset_mut(self, idx), vec_offset_mut(self, idx + 1), tail * self->elem_size);
    }
    --self->len;
}

void tda_vec_clear(tda_Vec *self) {
    ASSERT_VEC(self);

    self->len = 0;
}

tda_Status tda_vec_reserve(tda_Vec *self, size_t new_cap) {
    ASSERT_VEC(self);

    if (new_cap <= self->cap) {
        return TDA_STATUS_OK;
    }

    size_t new_bytes;
    if (ckd_mul(&new_bytes, new_cap, self->elem_size)) {
        return TDA_STATUS_ERR_NO_MEM;
    }
    void *data = tda_realloc(self->al, self->data, cap_bytes(self), new_bytes);
    if (!data) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    self->data = data;
    self->cap = new_cap;

    return TDA_STATUS_OK;
}

tda_Status tda_vec_shrink_to_fit(tda_Vec *self) {
    ASSERT_VEC(self);

    if (self->len == self->cap) {
        return TDA_STATUS_OK;
    }

    if (self->len == 0) {
        tda_dealloc(self->al, self->data, cap_bytes(self));
        self->data = nullptr;
        self->cap = 0;

        ASSERT_VEC(self);

        return TDA_STATUS_OK;
    }

    void *data = tda_realloc(self->al, self->data, cap_bytes(self), len_bytes(self));
    if (!data) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    self->data = data;
    self->cap = self->len;

    ASSERT_VEC(self);

    return TDA_STATUS_OK;
}

tda_Status tda_vec_resize(tda_Vec *self, size_t new_len) {
    ASSERT_VEC(self);

    if (new_len <= self->len) {
        self->len = new_len;
        return TDA_STATUS_OK;
    }

    if (new_len > self->cap) {
        const tda_Status st = reserve_for(self, new_len);
        if (TDA_STATUS_IS_ERR(st)) {
            return st;
        }
    }

    // zero init tail
    const size_t add_bytes = (new_len - self->len) * self->elem_size;
    memset(vec_offset_mut(self, self->len), 0, add_bytes);
    self->len = new_len;

    return TDA_STATUS_OK;
}

void tda_vec_swap(tda_Vec *self, tda_Vec *other) {
    ASSERT_VEC(self);
    ASSERT_VEC(other);
    assert(self->elem_size == other->elem_size);
    assert(self->al == other->al);

    if (self == other) {
        return;
    }

    TDA_SWAP(*self, *other);

    ASSERT_VEC(self);
    ASSERT_VEC(other);
}

void tda_vec_swap_elems(tda_Vec *self, size_t i, size_t j) {
    ASSERT_VEC(self);

    tda_span_swap_elems(tda_vec_to_span_mut(self), i, j);
}

/* ========== bulk mods ========== */

tda_Status tda_vec_extend(tda_Vec *self, tda_Span src) {
    ASSERT_VEC(self);
    TDA_SPAN_ASSERT(src);
    assert(src.elem_size == self->elem_size);

    return tda_vec_insert_span(self, self->len, src);
}

tda_Status tda_vec_insert_span(tda_Vec *self, size_t idx, tda_Span src) {
    ASSERT_VEC(self);
    TDA_SPAN_ASSERT(src);
    assert(src.elem_size == self->elem_size);
    assert(idx <= self->len);

    if (src.len == 0) {
        return TDA_STATUS_OK;
    }

    size_t new_len;
    if (ckd_add(&new_len, self->len, src.len)) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    if (new_len > self->cap) {
        const tda_Status st = reserve_for(self, new_len);
        if (TDA_STATUS_IS_ERR(st)) {
            return st;
        }
    }

    // one move for the whole run: this is the difference from a loop of insert, which
    // walks the tail again for every elem
    const size_t tail = self->len - idx;
    if (tail > 0) {
        memmove(vec_offset_mut(self, idx + src.len), vec_offset_mut(self, idx), tail * self->elem_size);
    }

    memcpy(vec_offset_mut(self, idx), src.data, src.len * self->elem_size);
    self->len = new_len;

    return TDA_STATUS_OK;
}

void tda_vec_remove_range(tda_Vec *self, size_t idx, size_t count) {
    ASSERT_VEC(self);
    assert(idx <= self->len);
    assert(count <= self->len - idx);

    if (count == 0) {
        return;
    }

    const size_t tail = self->len - idx - count;
    if (tail > 0) {
        memmove(vec_offset_mut(self, idx), vec_offset_mut(self, idx + count), tail * self->elem_size);
    }

    self->len -= count;
}

/* ========== to span ========== */

tda_SpanMut tda_vec_to_span_mut(tda_Vec *self) {
    ASSERT_VEC(self);

    return tda_span_from_data_mut(self->data, self->len, self->elem_size);
}

tda_Span tda_vec_to_span(const tda_Vec *self) {
    ASSERT_VEC(self);

    return tda_span_from_data(self->data, self->len, self->elem_size);
}

/* ========== print ========== */

void tda_vec_fprint(const tda_Vec *self, FILE *stream, tda_FPrint fprint) {
    ASSERT_VEC(self);
    assert(stream);
    assert(fprint);

    tda_span_fprint(tda_vec_to_span(self), stream, fprint);
}

void tda_vec_print(const tda_Vec *self, tda_FPrint fprint) {
    ASSERT_VEC(self);
    assert(fprint);

    tda_vec_fprint(self, stdout, fprint);
}

/* ========== internals ========== */

[[nodiscard]]
static tda_Status new_impl(bool zeroed, size_t len, size_t cap, size_t elem_size, tda_Al *al, tda_Vec **out) {
    assert(len <= cap);
    assert(elem_size > 0);
    assert(al);
    assert(out);

    tda_Vec *obj = tda_alloc(al, sizeof(tda_Vec));
    if (!obj) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    void *data = nullptr;

    if (cap > 0) {
        size_t bytes;
        if (ckd_mul(&bytes, cap, elem_size)) {
            goto fail;
        }
        data = tda_alloc(al, bytes);
        if (!data) {
            goto fail;
        }
        if (zeroed) {
            memset(data, 0, len * elem_size);
        }
    }

    set_fields(obj, data, len, cap, elem_size, al);

    ASSERT_VEC(obj);

    *out = obj;
    return TDA_STATUS_OK;

fail:
    tda_dealloc(al, obj, sizeof(tda_Vec));
    return TDA_STATUS_ERR_NO_MEM;
}

static void set_fields(tda_Vec *obj, void *data, size_t len, size_t cap, size_t elem_size, tda_Al *al) {
    obj->data = data;
    obj->len = len;
    obj->cap = cap;
    obj->elem_size = elem_size;
    obj->al = al;
}

static size_t next_cap(const tda_Vec *self) {
    if (self->cap == 0) {
        return VEC_GROWTH_BASE;
    }

    size_t grown;
    if (ckd_mul(&grown, self->cap, VEC_GROWTH_FACTOR)) {
        return SIZE_MAX;
    }

    return grown;
}

static tda_Status grow(tda_Vec *self) {
    assert(self->len == self->cap);

    if (self->cap == SIZE_MAX) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    const size_t wanted = next_cap(self);

    const tda_Status st = tda_vec_reserve(self, wanted);
    if (TDA_STATUS_IS_OK(st) || wanted <= self->cap + 1) {
        return st;
    }

    return tda_vec_reserve(self, self->cap + 1);
}

static tda_Status reserve_one(tda_Vec *self) {
    return self->len == self->cap ? grow(self) : TDA_STATUS_OK;
}

static tda_Status reserve_for(tda_Vec *self, size_t new_len) {
    assert(new_len > self->cap);

    const size_t eager = next_cap(self);
    if (eager > new_len) {
        const tda_Status st = tda_vec_reserve(self, eager);
        if (TDA_STATUS_IS_OK(st)) {
            return st;
        }
    }

    return tda_vec_reserve(self, new_len);
}

static void release_data(tda_Vec *self) {
    tda_dealloc(self->al, self->data, cap_bytes(self));
    self->data = nullptr;
    self->len = 0;
    self->cap = 0;
}

static size_t len_bytes(const tda_Vec *self) {
    return self->len * self->elem_size;
}

static size_t cap_bytes(const tda_Vec *self) {
    return self->cap * self->elem_size;
}

static const unsigned char *vec_offset(const tda_Vec *self, size_t idx) {
    return tda_byte_offset(self->data, self->elem_size, idx);
}

static unsigned char *vec_offset_mut(tda_Vec *self, size_t idx) {
    return tda_byte_offset_mut(self->data, self->elem_size, idx);
}
