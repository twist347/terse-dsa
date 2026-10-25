#include "tda/ds/deque.h"

#include "tda/core/check.h"

#include "internal/order.h"
#include "internal/overlap.h"
#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <string.h>

/* ========== internals ========== */

#define ASSERT_DEQUE(d)                                \
    (assert(d),                                        \
     assert(tda_layout_is_type((d)->elem_)),           \
     assert((d)->len_ <= (d)->cap_),                   \
     assert((d)->cap_ == 0 || (d)->data_),             \
     assert(!(d)->data_ || (d)->cap_ > 0),             \
     assert((d)->cap_ == 0 || (d)->head_ < (d)->cap_), \
     assert((d)->cap_ > 0 || (d)->head_ == 0),         \
     assert((d)->al_))

#define EXPECT_SAME_DEQUE(a, b) \
    (TDA_EXPECT(tda_layout_eq((a)->elem_, (b)->elem_)))

static constexpr size_t DEQUE_GROWTH_BASE = 1;
static constexpr size_t DEQUE_GROWTH_FACTOR = 2;

[[nodiscard]]
static tda_Status init_impl(bool zeroed, size_t len, size_t cap, tda_Layout elem, tda_Al *al, tda_Deque *out);

/// frees the block, leaving an empty deque on the same allocator
static void release_data(tda_Deque *self);

/// puts 'count' elems read from 'src' at 'idx', shifting the shorter side; the shared
/// body of insert and insert_span
[[nodiscard]]
static tda_Status insert_raw(tda_Deque *self, size_t idx, const void *src, size_t count);

[[nodiscard]]
static size_t next_cap(const tda_Deque *self);

/// room for one more elem
[[nodiscard]]
static tda_Status reserve_one(tda_Deque *self);

/// room for 'new_len' elems: grows by the growth factor when that is more, so repeated
/// resizes stay amortized, and falls back to the exact size if that is refused
[[nodiscard]]
static tda_Status reserve_for(tda_Deque *self, size_t new_len);

[[nodiscard]]
static size_t len_bytes(const tda_Deque *self);

/// a block of 'cap' elems at the elem's alignment; the caller has checked that it fits
[[nodiscard]]
static tda_Layout block_layout(const tda_Deque *self, size_t cap);

/// the whole ring buffer as a view, the free slots included: what growth may free
[[nodiscard]] [[maybe_unused]]
static tda_Span block_span(const tda_Deque *self);

/// hands the block back to the allocator; the fields still describe it
static void free_block(tda_Deque *self);

/// frees the old block and takes 'ptr', of 'cap' elems, laid out from slot 0
static void adopt_block(tda_Deque *self, void *ptr, size_t cap);

/// moves the contents into a new block of 'new_cap' elems, unwrapped from slot 0. The
/// caller has checked that the byte count fits
[[nodiscard]]
static tda_Status relocate(tda_Deque *self, size_t new_cap);

/// the walk behind eq and eq_by; a null 'eq' compares bytes
[[nodiscard]]
static bool eq_impl(const tda_Deque *a, const tda_Deque *b, tda_Eq eq, void *ctx);

/// the slot 'count' places after 'slot', one lap on when that passes the end. One
/// subtraction and no division: 'count' is at most cap, so it never laps twice
[[nodiscard]]
static size_t slot_fwd(const tda_Deque *self, size_t slot, size_t count);

/// the slot 'count' places before 'slot', one lap back when that passes slot 0
[[nodiscard]]
static size_t slot_back(const tda_Deque *self, size_t slot, size_t count);

/// the slot holding the elem at 'idx' counted from the front
[[nodiscard]]
static size_t slot_of(const tda_Deque *self, size_t idx);

[[nodiscard]]
static const unsigned char *slot_at(const tda_Deque *self, size_t slot);

[[nodiscard]]
static unsigned char *slot_at_mut(tda_Deque *self, size_t slot);

[[nodiscard]]
static const unsigned char *elem_at(const tda_Deque *self, size_t idx);

[[nodiscard]]
static unsigned char *elem_at_mut(tda_Deque *self, size_t idx);

/// how many of 'count' elems from 'slot' on fit before the end of the buffer: the
/// length of the first of the at most two runs they form
[[nodiscard]]
static size_t run_from(const tda_Deque *self, size_t slot, size_t count);

/// run_from for the whole contents
[[nodiscard]]
static size_t first_run(const tda_Deque *self);

/// every elem, in ring order, into 'dst' bytes. The caller guarantees the room
static void copy_out(const tda_Deque *self, void *dst);

/// 'count' elems of 'src' bytes over the elems from 'idx' on, which may straddle the seam
static void copy_range_in(tda_Deque *self, size_t idx, const void *src, size_t count);

static void move_elem(tda_Deque *self, size_t to, size_t from);

/// memmove over indices: 'count' elems from 'from' to 'to', the ranges may overlap
static void move_elems(tda_Deque *self, size_t to, size_t from, size_t count);

/* ========== lifetime ========== */

void tda_deque_init(tda_Layout elem, tda_Al *al, tda_Deque *out) {
    assert(tda_layout_is_type(elem));
    assert(al);
    assert(out);

    *out = (tda_Deque){.elem_ = elem, .al_ = al};
}

tda_Status tda_deque_init_len(size_t len, tda_Layout elem, tda_Al *al, tda_Deque *out) {
    assert(tda_layout_is_type(elem));
    assert(al);
    assert(out);

    return init_impl(true, len, len, elem, al, out);
}

tda_Status tda_deque_init_cap(size_t cap, tda_Layout elem, tda_Al *al, tda_Deque *out) {
    assert(tda_layout_is_type(elem));
    assert(al);
    assert(out);

    return init_impl(false, 0, cap, elem, al, out);
}

tda_Status tda_deque_from_data(const void *data, size_t len, tda_Layout elem, tda_Al *al, tda_Deque *out) {
    assert(data || len == 0);
    assert(tda_layout_is_type(elem));
    assert(al);
    assert(out);

    tda_Deque obj;
    const tda_Status st = init_impl(false, len, len, elem, al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // head is 0 here, so the ring starts out unwrapped and this is one memcpy
    if (len > 0) {
        memcpy(obj.data_, data, len_bytes(&obj));
    }

    *out = obj;

    return TDA_STATUS_OK;
}

tda_Status tda_deque_from_span(tda_Span s, tda_Al *al, tda_Deque *out) {
    TDA_SPAN_ASSERT(s);
    assert(al);
    assert(out);

    return tda_deque_from_data(s.data, s.len, s.elem, al, out);
}

void tda_deque_deinit(tda_Deque *self) {
    assert(self);

    // the inert state, never initialized or already released
    if (!self->al_) {
        assert(!self->data_);
        return;
    }

    ASSERT_DEQUE(self);

    free_block(self);
    *self = (tda_Deque){};
}

/* ========== copy ========== */

tda_Status tda_deque_copy(const tda_Deque *self, tda_Deque *out) {
    ASSERT_DEQUE(self);
    assert(out);

    return tda_deque_copy_with(self, self->al_, out);
}

tda_Status tda_deque_copy_with(const tda_Deque *self, tda_Al *al, tda_Deque *out) {
    ASSERT_DEQUE(self);
    assert(al);
    assert(out);

    tda_Deque copy;
    const tda_Status st = init_impl(false, self->len_, self->len_, self->elem_, al, &copy);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // the copy is sized to the content, so it comes out unwrapped whatever
    // self looks like
    copy_out(self, copy.data_);

    *out = copy;

    return TDA_STATUS_OK;
}

tda_Status tda_deque_copy_assign(tda_Deque *self, const tda_Deque *other) {
    ASSERT_DEQUE(self);
    ASSERT_DEQUE(other);
    EXPECT_SAME_DEQUE(self, other);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    // a new block rather than tda_deque_reserve, which would carry over the elems about
    // to be overwritten; the old one goes only once the new one is in hand, so a failure
    // leaves 'self' as it was. 'other' holds this many, so the product fits
    if (other->len_ > self->cap_) {
        void *ptr = tda_alloc(self->al_, block_layout(self, other->len_));
        if (!ptr) {
            return TDA_STATUS_ERR_NO_MEM;
        }

        adopt_block(self, ptr, other->len_);
    }

    // whatever 'self' held is gone, so its ring is laid out afresh from slot 0
    self->head_ = 0;
    self->len_ = other->len_;
    copy_out(other, self->data_);

    ASSERT_DEQUE(self);

    return TDA_STATUS_OK;
}

void tda_deque_move_assign(tda_Deque *self, tda_Deque *other) {
    ASSERT_DEQUE(self);
    ASSERT_DEQUE(other);
    EXPECT_SAME_DEQUE(self, other);

    if (self == other) {
        return;
    }

    // the allocator goes along with the block, and 'other' stays usable on it
    tda_deque_deinit(self);
    *self = *other;
    tda_deque_init(self->elem_, self->al_, other);

    ASSERT_DEQUE(self);
    ASSERT_DEQUE(other);
}

void tda_deque_copy_to_span(const tda_Deque *self, tda_SpanMut dst) {
    ASSERT_DEQUE(self);
    TDA_SPAN_ASSERT(dst);
    TDA_EXPECT(tda_layout_eq(self->elem_, dst.elem));
    TDA_EXPECT(dst.len == self->len_);

    copy_out(self, dst.data);
}

void tda_deque_copy_from_span(tda_Deque *self, tda_Span src) {
    ASSERT_DEQUE(self);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(tda_layout_eq(self->elem_, src.elem));
    TDA_EXPECT(src.len == self->len_);

    copy_range_in(self, 0, src.data, self->len_);
}

/* ========== to spans ========== */

void tda_deque_to_spans(const tda_Deque *self, tda_Span *out_front, tda_Span *out_back) {
    ASSERT_DEQUE(self);
    assert(out_front);
    assert(out_back);

    const size_t front_len = first_run(self);
    *out_front = tda_span_from_data(front_len ? elem_at(self, 0) : nullptr, front_len, self->elem_);
    *out_back = tda_span_from_data(self->data_, self->len_ - front_len, self->elem_);
}

void tda_deque_to_spans_mut(tda_Deque *self, tda_SpanMut *out_front, tda_SpanMut *out_back) {
    ASSERT_DEQUE(self);
    assert(out_front);
    assert(out_back);

    const size_t front_len = first_run(self);
    *out_front = tda_span_from_data_mut(front_len ? elem_at_mut(self, 0) : nullptr, front_len, self->elem_);
    *out_back = tda_span_from_data_mut(self->data_, self->len_ - front_len, self->elem_);
}

/* ========== compare ========== */

bool tda_deque_eq(const tda_Deque *a, const tda_Deque *b) {
    ASSERT_DEQUE(a);
    ASSERT_DEQUE(b);
    EXPECT_SAME_DEQUE(a, b);

    return eq_impl(a, b, nullptr, nullptr);
}

bool tda_deque_eq_by(const tda_Deque *a, const tda_Deque *b, tda_Eq eq, void *ctx) {
    ASSERT_DEQUE(a);
    ASSERT_DEQUE(b);
    assert(eq);
    EXPECT_SAME_DEQUE(a, b);

    return eq_impl(a, b, eq, ctx);
}

/* ========== access ========== */

void tda_deque_set(tda_Deque *self, size_t idx, const void *val) {
    ASSERT_DEQUE(self);
    assert(val);
    TDA_EXPECT(idx < self->len_);

    // memmove: 'val' may alias the elem being set
    memmove(elem_at_mut(self, idx), val, self->elem_.size);
}

/* ========== mods ========== */

tda_Status tda_deque_push_front(tda_Deque *self, const void *val) {
    ASSERT_DEQUE(self);
    assert(val);
    // growing frees the block 'val' would be read from
    TDA_EXPECT(!tda_addr_in(self->data_, block_layout(self, self->cap_).size, val));

    const tda_Status st = reserve_one(self);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    self->head_ = slot_back(self, self->head_, 1);
    memcpy(slot_at_mut(self, self->head_), val, self->elem_.size);
    ++self->len_;

    ASSERT_DEQUE(self);

    return TDA_STATUS_OK;
}

tda_Status tda_deque_push_back(tda_Deque *self, const void *val) {
    ASSERT_DEQUE(self);
    assert(val);
    // growing frees the block 'val' would be read from
    TDA_EXPECT(!tda_addr_in(self->data_, block_layout(self, self->cap_).size, val));

    const tda_Status st = reserve_one(self);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    memcpy(elem_at_mut(self, self->len_), val, self->elem_.size);
    ++self->len_;

    ASSERT_DEQUE(self);

    return TDA_STATUS_OK;
}

void tda_deque_pop_front(tda_Deque *self) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(self->len_ > 0);

    self->head_ = slot_fwd(self, self->head_, 1);
    --self->len_;

    ASSERT_DEQUE(self);
}

void tda_deque_pop_back(tda_Deque *self) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(self->len_ > 0);

    --self->len_;

    ASSERT_DEQUE(self);
}

void tda_deque_pop_front_to(tda_Deque *self, void *out) {
    ASSERT_DEQUE(self);
    assert(out);
    TDA_EXPECT(self->len_ > 0);

    // memmove: 'out' may alias the slot being dropped
    memmove(out, elem_at(self, 0), self->elem_.size);
    tda_deque_pop_front(self);
}

void tda_deque_pop_back_to(tda_Deque *self, void *out) {
    ASSERT_DEQUE(self);
    assert(out);
    TDA_EXPECT(self->len_ > 0);

    // memmove, as in tda_deque_pop_front_to
    memmove(out, elem_at(self, self->len_ - 1), self->elem_.size);
    tda_deque_pop_back(self);
}

tda_Status tda_deque_insert(tda_Deque *self, size_t idx, const void *val) {
    ASSERT_DEQUE(self);
    assert(val);
    TDA_EXPECT(idx <= self->len_);
    // growing frees the block 'val' would be read from
    TDA_EXPECT(!tda_addr_in(self->data_, block_layout(self, self->cap_).size, val));

    // straight from 'val', not through a span, which would ask it to be aligned
    return insert_raw(self, idx, val, 1);
}

void tda_deque_remove(tda_Deque *self, size_t idx) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(idx < self->len_);

    tda_deque_remove_range(self, idx, 1);
}

void tda_deque_clear(tda_Deque *self) {
    ASSERT_DEQUE(self);

    self->len_ = 0;
    self->head_ = 0;

    ASSERT_DEQUE(self);
}

tda_Status tda_deque_reserve(tda_Deque *self, size_t new_cap) {
    ASSERT_DEQUE(self);

    if (new_cap <= self->cap_) {
        return TDA_STATUS_OK;
    }

    tda_Layout block;
    if (tda_layout_ckd_array(&block, self->elem_, new_cap)) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    return relocate(self, new_cap);
}

tda_Status tda_deque_shrink_to_fit(tda_Deque *self) {
    ASSERT_DEQUE(self);

    if (self->len_ == self->cap_) {
        return TDA_STATUS_OK;
    }

    if (self->len_ == 0) {
        release_data(self);
        ASSERT_DEQUE(self);
        return TDA_STATUS_OK;
    }

    // 'len' elems are held now, so their byte count fits
    return relocate(self, self->len_);
}

tda_Status tda_deque_resize(tda_Deque *self, size_t new_len) {
    ASSERT_DEQUE(self);

    if (new_len <= self->len_) {
        self->len_ = new_len;
        ASSERT_DEQUE(self);
        return TDA_STATUS_OK;
    }

    if (new_len > self->cap_) {
        const tda_Status st = reserve_for(self, new_len);
        if (TDA_STATUS_IS_ERR(st)) {
            return st;
        }
    }

    // the new tail may straddle the seam, so it is zeroed a slot at a time rather
    // than with one memset over a range that need not be contiguous
    for (size_t i = self->len_; i < new_len; ++i) {
        memset(elem_at_mut(self, i), 0, self->elem_.size);
    }
    self->len_ = new_len;

    ASSERT_DEQUE(self);

    return TDA_STATUS_OK;
}

void tda_deque_truncate(tda_Deque *self, size_t new_len) {
    ASSERT_DEQUE(self);

    if (new_len < self->len_) {
        self->len_ = new_len;
    }

    ASSERT_DEQUE(self);
}

void tda_deque_swap(tda_Deque *self, tda_Deque *other) {
    ASSERT_DEQUE(self);
    ASSERT_DEQUE(other);
    EXPECT_SAME_DEQUE(self, other);

    if (self == other) {
        return;
    }

    TDA_SWAP(*self, *other);

    ASSERT_DEQUE(self);
    ASSERT_DEQUE(other);
}

void tda_deque_swap_elems(tda_Deque *self, size_t i, size_t j) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(i < self->len_);
    TDA_EXPECT(j < self->len_);

    if (i == j) {
        return;
    }

    tda_bytes_swap(elem_at_mut(self, i), elem_at_mut(self, j), self->elem_.size);
}

/* ========== bulk mods ========== */

tda_Status tda_deque_extend(tda_Deque *self, tda_Span src) {
    ASSERT_DEQUE(self);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(tda_layout_eq(self->elem_, src.elem));
    // growing may free what 'src' views
    TDA_EXPECT(!tda_span_overlap(block_span(self), src));

    return tda_deque_insert_span(self, self->len_, src);
}

tda_Status tda_deque_insert_span(tda_Deque *self, size_t idx, tda_Span src) {
    ASSERT_DEQUE(self);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(tda_layout_eq(self->elem_, src.elem));
    TDA_EXPECT(idx <= self->len_);
    // growing may free what 'src' views
    TDA_EXPECT(!tda_span_overlap(block_span(self), src));

    return insert_raw(self, idx, src.data, src.len);
}

void tda_deque_remove_range(tda_Deque *self, size_t idx, size_t count) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(idx <= self->len_);
    TDA_EXPECT(count <= self->len_ - idx);

    if (count == 0) {
        return;
    }

    // whichever side is shorter slides over the gap
    if (idx < self->len_ - idx - count) {
        move_elems(self, count, 0, idx);
        self->head_ = slot_fwd(self, self->head_, count);
    } else {
        move_elems(self, idx, idx + count, self->len_ - idx - count);
    }

    self->len_ -= count;

    ASSERT_DEQUE(self);
}

void tda_deque_retain(tda_Deque *self, tda_Pred pred, void *ctx) {
    ASSERT_DEQUE(self);
    assert(pred);

    size_t kept = 0;
    for (size_t i = 0; i < self->len_; ++i) {
        if (!pred(elem_at(self, i), ctx)) {
            continue;
        }
        // until the first drop every kept elem is already in place
        if (kept != i) {
            move_elem(self, kept, i);
        }
        ++kept;
    }
    self->len_ = kept;

    ASSERT_DEQUE(self);
}

/* ========== print ========== */

void tda_deque_fprint(const tda_Deque *self, FILE *stream, tda_FPrint fprint) {
    ASSERT_DEQUE(self);
    assert(stream);
    assert(fprint);

    // not delegated to tda_span_fprint: the ring is up to two spans, and one bracket must
    // hold both. The format matches it on purpose
    fputc('[', stream);
    for (size_t i = 0; i < self->len_; ++i) {
        if (i > 0) {
            fputs(", ", stream);
        }
        fprint(stream, elem_at(self, i));
    }
    fputs("]\n", stream);
}

void tda_deque_print(const tda_Deque *self, tda_FPrint fprint) {
    ASSERT_DEQUE(self);
    assert(fprint);

    tda_deque_fprint(self, stdout, fprint);
}

/* ========== internals ========== */

static tda_Status insert_raw(tda_Deque *self, size_t idx, const void *src, size_t count) {
    if (count == 0) {
        return TDA_STATUS_OK;
    }

    size_t new_len;
    if (ckd_add(&new_len, self->len_, count)) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    if (new_len > self->cap_) {
        const tda_Status st = reserve_for(self, new_len);
        if (TDA_STATUS_IS_ERR(st)) {
            return st;
        }
    }

    // whichever side is shorter is shifted to open a gap 'count' wide
    const size_t old_len = self->len_;
    if (idx < old_len - idx) {
        self->head_ = slot_back(self, self->head_, count);
        self->len_ = new_len;
        move_elems(self, 0, count, idx);
    } else {
        self->len_ = new_len;
        move_elems(self, idx + count, idx, old_len - idx);
    }

    copy_range_in(self, idx, src, count);

    ASSERT_DEQUE(self);

    return TDA_STATUS_OK;
}

static tda_Status init_impl(bool zeroed, size_t len, size_t cap, tda_Layout elem, tda_Al *al, tda_Deque *out) {
    assert(len <= cap);

    tda_Deque obj;
    tda_deque_init(elem, al, &obj);
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

    ASSERT_DEQUE(out);

    return TDA_STATUS_OK;
}

static size_t next_cap(const tda_Deque *self) {
    if (self->cap_ == 0) {
        return DEQUE_GROWTH_BASE;
    }

    size_t grown;
    if (ckd_mul(&grown, self->cap_, DEQUE_GROWTH_FACTOR)) {
        return SIZE_MAX;
    }

    return grown;
}

static tda_Status reserve_for(tda_Deque *self, size_t new_len) {
    assert(new_len > self->cap_);

    const size_t eager = next_cap(self);
    if (eager > new_len) {
        const tda_Status st = tda_deque_reserve(self, eager);
        if (TDA_STATUS_IS_OK(st)) {
            return st;
        }
    }

    return tda_deque_reserve(self, new_len);
}

static tda_Status reserve_one(tda_Deque *self) {
    if (self->len_ < self->cap_) {
        return TDA_STATUS_OK;
    }

    if (self->cap_ == SIZE_MAX) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    return reserve_for(self, self->cap_ + 1);
}

static tda_Status relocate(tda_Deque *self, size_t new_cap) {
    assert(new_cap >= self->len_);
    assert(new_cap > 0);

    // a fresh block rather than a realloc: growing in place would leave the wrapped part
    // sitting before the seam, where the new capacity does not reach it. The contents are
    // unrolled into the new block instead, which is the same two memcpy a realloc would
    // have needed anyway
    void *ptr = tda_alloc(self->al_, block_layout(self, new_cap));
    if (!ptr) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    copy_out(self, ptr);
    adopt_block(self, ptr, new_cap);

    ASSERT_DEQUE(self);

    return TDA_STATUS_OK;
}

static bool eq_impl(const tda_Deque *a, const tda_Deque *b, tda_Eq eq, void *ctx) {
    if (a == b) {
        return true;
    }

    if (a->len_ != b->len_) {
        return false;
    }

    // by index and not by memcmp over the buffers: two rings holding the same elems start
    // at different slots, and the bytes outside the contents are not contents
    for (size_t i = 0; i < a->len_; ++i) {
        const void *lhs = elem_at(a, i);
        const void *rhs = elem_at(b, i);
        if (!tda_elem_eq(eq, ctx, lhs, rhs, a->elem_.size)) {
            return false;
        }
    }

    return true;
}

static void release_data(tda_Deque *self) {
    free_block(self);
    self->data_ = nullptr;
    self->len_ = 0;
    self->cap_ = 0;
    self->head_ = 0;
}

static size_t len_bytes(const tda_Deque *self) {
    return self->len_ * self->elem_.size;
}

static tda_Layout block_layout(const tda_Deque *self, size_t cap) {
    return tda_elems_layout(self->elem_, cap);
}

static tda_Span block_span(const tda_Deque *self) {
    return (tda_Span){.data = self->data_, .len = self->cap_, .elem = self->elem_};
}

static void free_block(tda_Deque *self) {
    tda_dealloc(self->al_, self->data_, block_layout(self, self->cap_));
}

static void adopt_block(tda_Deque *self, void *ptr, size_t cap) {
    free_block(self);
    self->data_ = ptr;
    self->cap_ = cap;
    self->head_ = 0;
}

static size_t slot_fwd(const tda_Deque *self, size_t slot, size_t count) {
    assert(slot < self->cap_);
    assert(count <= self->cap_);

    // against the room left rather than through slot + count, which then cannot overflow
    return count < self->cap_ - slot ? slot + count : count - (self->cap_ - slot);
}

static size_t slot_back(const tda_Deque *self, size_t slot, size_t count) {
    assert(slot < self->cap_);
    assert(count <= self->cap_);

    return slot >= count ? slot - count : slot + self->cap_ - count;
}

static size_t slot_of(const tda_Deque *self, size_t idx) {
    assert(idx < self->cap_);

    return slot_fwd(self, self->head_, idx);
}

static const unsigned char *slot_at(const tda_Deque *self, size_t slot) {
    return tda_byte_offset(self->data_, self->elem_.size, slot);
}

static unsigned char *slot_at_mut(tda_Deque *self, size_t slot) {
    return tda_byte_offset_mut(self->data_, self->elem_.size, slot);
}

static const unsigned char *elem_at(const tda_Deque *self, size_t idx) {
    return slot_at(self, slot_of(self, idx));
}

static unsigned char *elem_at_mut(tda_Deque *self, size_t idx) {
    return slot_at_mut(self, slot_of(self, idx));
}

static size_t run_from(const tda_Deque *self, size_t slot, size_t count) {
    assert(slot <= self->cap_);

    const size_t to_end = self->cap_ - slot;

    return to_end < count ? to_end : count;
}

static size_t first_run(const tda_Deque *self) {
    return run_from(self, self->head_, self->len_);
}

static void copy_out(const tda_Deque *self, void *dst) {
    if (self->len_ == 0) {
        return;
    }

    const size_t run = first_run(self);
    memcpy(dst, slot_at(self, self->head_), run * self->elem_.size);

    if (run < self->len_) {
        memcpy(
            tda_byte_offset_mut(dst, self->elem_.size, run),
            slot_at(self, 0),
            (self->len_ - run) * self->elem_.size
        );
    }
}

static void copy_range_in(tda_Deque *self, size_t idx, const void *src, size_t count) {
    assert(idx + count <= self->len_);

    if (count == 0) {
        return;
    }

    const size_t slot = slot_of(self, idx);
    const size_t run = run_from(self, slot, count);

    memcpy(slot_at_mut(self, slot), src, run * self->elem_.size);

    if (run < count) {
        memcpy(
            slot_at_mut(self, 0),
            tda_byte_offset(src, self->elem_.size, run),
            (count - run) * self->elem_.size
        );
    }
}

static void move_elem(tda_Deque *self, size_t to, size_t from) {
    // two distinct slots of the same ring, so they never overlap
    memcpy(elem_at_mut(self, to), elem_at_mut(self, from), self->elem_.size);
}

static void move_elems(tda_Deque *self, size_t to, size_t from, size_t count) {
    // each elem is read before anything is written over it: moving down walks up, moving
    // up walks down
    if (to < from) {
        for (size_t i = 0; i < count; ++i) {
            move_elem(self, to + i, from + i);
        }
    } else {
        for (size_t i = count; i > 0; --i) {
            move_elem(self, to + i - 1, from + i - 1);
        }
    }
}
