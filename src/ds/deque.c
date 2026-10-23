#include "tda/ds/deque.h"

#include "tda/core/check.h"

#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <string.h>

/* ========== internals ========== */

#define ASSERT_DEQUE(d)                             \
    (assert(d),                                     \
     assert(tda_layout_is_type((d)->elem)),         \
     assert((d)->len <= (d)->cap),                  \
     assert((d)->cap == 0 || (d)->data),            \
     assert(!(d)->data || (d)->cap > 0),            \
     assert((d)->cap == 0 || (d)->head < (d)->cap), \
     assert((d)->cap > 0 || (d)->head == 0),        \
     assert((d)->al))

static constexpr size_t DEQUE_GROWTH_BASE = 1;
static constexpr size_t DEQUE_GROWTH_FACTOR = 2;

struct tda_Deque {
    void *data;
    size_t len;
    size_t cap;
    // slot holding the front elem. Kept alongside 'len' rather than a tail index:
    // with two indices a full ring and an empty one look exactly alike
    size_t head;
    tda_Layout elem;
    tda_Al *al;
};

[[nodiscard]]
static tda_Status new_impl(bool zeroed, size_t len, size_t cap, tda_Layout elem, tda_Al *al, tda_Deque **out);

static void set_fields(tda_Deque *obj, void *data, size_t len, size_t cap, tda_Layout elem, tda_Al *al);

/// frees the block, leaving an empty deque on the same allocator
static void release_data(tda_Deque *self);

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
static tda_Layout cap_layout(const tda_Deque *self, size_t cap);

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

/* ========== lifetime ========== */

tda_Status tda_deque_new(tda_Layout elem, tda_Al *al, tda_Deque **out) {
    assert(tda_layout_is_type(elem));
    assert(al);
    assert(out);

    return new_impl(false, 0, 0, elem, al, out);
}

tda_Status tda_deque_new_len(size_t len, tda_Layout elem, tda_Al *al, tda_Deque **out) {
    assert(tda_layout_is_type(elem));
    assert(al);
    assert(out);

    return new_impl(true, len, len, elem, al, out);
}

tda_Status tda_deque_new_cap(size_t cap, tda_Layout elem, tda_Al *al, tda_Deque **out) {
    assert(tda_layout_is_type(elem));
    assert(al);
    assert(out);

    return new_impl(false, 0, cap, elem, al, out);
}

tda_Status tda_deque_from_data(const void *data, size_t len, tda_Layout elem, tda_Al *al, tda_Deque **out) {
    assert(data || len == 0);
    assert(tda_layout_is_type(elem));
    assert(al);
    assert(out);

    tda_Deque *deque;
    const tda_Status st = new_impl(false, len, len, elem, al, &deque);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // head is 0 here, so the ring starts out unwrapped and this is one memcpy
    if (len > 0) {
        memcpy(deque->data, data, len_bytes(deque));
    }

    *out = deque;

    return TDA_STATUS_OK;
}

tda_Status tda_deque_from_span(tda_Span s, tda_Al *al, tda_Deque **out) {
    TDA_SPAN_ASSERT(s);
    assert(al);
    assert(out);

    return tda_deque_from_data(s.data, s.len, s.elem, al, out);
}

void tda_deque_drop(tda_Deque *self) {
    if (!self) {
        return;
    }

    ASSERT_DEQUE(self);

    tda_Al *al_copy = self->al;
    tda_dealloc(al_copy, self->data, cap_layout(self, self->cap));
    TDA_DEALLOC(tda_Deque, al_copy, self, 1);
}

/* ========== copy ========== */

tda_Status tda_deque_copy(const tda_Deque *self, tda_Deque **out) {
    ASSERT_DEQUE(self);

    return tda_deque_copy_with(self, self->al, out);
}

tda_Status tda_deque_copy_with(const tda_Deque *self, tda_Al *al, tda_Deque **out) {
    ASSERT_DEQUE(self);
    assert(al);
    assert(out);

    tda_Deque *copy;
    const tda_Status st = new_impl(false, self->len, self->len, self->elem, al, &copy);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // the copy is sized to the content, so it comes out unwrapped whatever
    // self looks like
    copy_out(self, copy->data);

    *out = copy;

    return TDA_STATUS_OK;
}

tda_Status tda_deque_copy_assign(tda_Deque *self, const tda_Deque *other) {
    ASSERT_DEQUE(self);
    ASSERT_DEQUE(other);
    TDA_EXPECT(tda_layout_eq(self->elem, other->elem));

    if (self == other) {
        return TDA_STATUS_OK;
    }

    // a new block rather than tda_deque_reserve, which would carry over the elems about
    // to be overwritten; the old one goes only once the new one is in hand, so a failure
    // leaves 'self' as it was. 'other' holds this many, so the product fits
    if (other->len > self->cap) {
        void *data = tda_alloc(self->al, cap_layout(self, other->len));
        if (!data) {
            return TDA_STATUS_ERR_NO_MEM;
        }

        tda_dealloc(self->al, self->data, cap_layout(self, self->cap));
        self->data = data;
        self->cap = other->len;
    }

    // whatever 'self' held is gone, so its ring is laid out afresh from slot 0
    self->head = 0;
    self->len = other->len;
    copy_out(other, self->data);

    ASSERT_DEQUE(self);

    return TDA_STATUS_OK;
}

tda_Status tda_deque_move_assign(tda_Deque *self, tda_Deque *other) {
    ASSERT_DEQUE(self);
    ASSERT_DEQUE(other);
    TDA_EXPECT(tda_layout_eq(self->elem, other->elem));

    if (self == other) {
        return TDA_STATUS_OK;
    }

    // one allocator: swap the blocks, then release what 'self' held from 'other'
    if (self->al == other->al) {
        TDA_SWAP(*self, *other);
        release_data(other);

        ASSERT_DEQUE(self);
        ASSERT_DEQUE(other);

        return TDA_STATUS_OK;
    }

    // two allocators: build the copy first, so a refusal changes nothing
    tda_Deque *obj;
    const tda_Status st = tda_deque_copy_with(other, self->al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    TDA_SWAP(*self, *obj);
    tda_deque_drop(obj);
    release_data(other);

    ASSERT_DEQUE(self);
    ASSERT_DEQUE(other);

    return TDA_STATUS_OK;
}

void tda_deque_copy_to_span(const tda_Deque *self, tda_SpanMut dst) {
    ASSERT_DEQUE(self);
    TDA_SPAN_ASSERT(dst);
    TDA_EXPECT(tda_layout_eq(dst.elem, self->elem));
    TDA_EXPECT(dst.len == self->len);

    copy_out(self, dst.data);
}

void tda_deque_copy_from_span(tda_Deque *self, tda_Span src) {
    ASSERT_DEQUE(self);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(tda_layout_eq(src.elem, self->elem));
    TDA_EXPECT(src.len == self->len);

    copy_range_in(self, 0, src.data, self->len);
}

/* ========== to spans ========== */

void tda_deque_to_spans(const tda_Deque *self, tda_Span *out_front, tda_Span *out_back) {
    ASSERT_DEQUE(self);
    assert(out_front);
    assert(out_back);

    const size_t front_len = first_run(self);
    *out_front = tda_span_from_data(front_len ? elem_at(self, 0) : nullptr, front_len, self->elem);
    *out_back = tda_span_from_data(self->data, self->len - front_len, self->elem);
}

void tda_deque_to_spans_mut(tda_Deque *self, tda_SpanMut *out_front, tda_SpanMut *out_back) {
    ASSERT_DEQUE(self);
    assert(out_front);
    assert(out_back);

    const size_t front_len = first_run(self);
    *out_front = tda_span_from_data_mut(front_len ? elem_at_mut(self, 0) : nullptr, front_len, self->elem);
    *out_back = tda_span_from_data_mut(self->data, self->len - front_len, self->elem);
}

/* ========== compare ========== */

bool tda_deque_eq(const tda_Deque *a, const tda_Deque *b) {
    ASSERT_DEQUE(a);
    ASSERT_DEQUE(b);
    TDA_EXPECT(tda_layout_eq(a->elem, b->elem));

    return eq_impl(a, b, nullptr, nullptr);
}

bool tda_deque_eq_by(const tda_Deque *a, const tda_Deque *b, tda_Eq eq, void *ctx) {
    ASSERT_DEQUE(a);
    ASSERT_DEQUE(b);
    TDA_EXPECT(tda_layout_eq(a->elem, b->elem));
    assert(eq);

    return eq_impl(a, b, eq, ctx);
}

/* ========== info ========== */

size_t tda_deque_len(const tda_Deque *self) {
    ASSERT_DEQUE(self);

    return self->len;
}

size_t tda_deque_cap(const tda_Deque *self) {
    ASSERT_DEQUE(self);

    return self->cap;
}

size_t tda_deque_elem_size(const tda_Deque *self) {
    ASSERT_DEQUE(self);

    return self->elem.size;
}

tda_Layout tda_deque_elem_layout(const tda_Deque *self) {
    ASSERT_DEQUE(self);

    return self->elem;
}

size_t tda_deque_bytes(const tda_Deque *self) {
    ASSERT_DEQUE(self);

    return len_bytes(self);
}

tda_Al *tda_deque_al(const tda_Deque *self) {
    ASSERT_DEQUE(self);

    return self->al;
}

/* ========== access ========== */

const void *tda_deque_front(const tda_Deque *self) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(self->len > 0);

    return elem_at(self, 0);
}

void *tda_deque_front_mut(tda_Deque *self) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(self->len > 0);

    return elem_at_mut(self, 0);
}

const void *tda_deque_back(const tda_Deque *self) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(self->len > 0);

    return elem_at(self, self->len - 1);
}

void *tda_deque_back_mut(tda_Deque *self) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(self->len > 0);

    return elem_at_mut(self, self->len - 1);
}

const void *tda_deque_get(const tda_Deque *self, size_t idx) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(idx < self->len);

    return elem_at(self, idx);
}

void *tda_deque_get_mut(tda_Deque *self, size_t idx) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(idx < self->len);

    return elem_at_mut(self, idx);
}

void tda_deque_set(tda_Deque *self, size_t idx, const void *val) {
    ASSERT_DEQUE(self);
    assert(val);
    TDA_EXPECT(idx < self->len);

    // memmove: 'val' may alias the elem being set
    memmove(elem_at_mut(self, idx), val, self->elem.size);
}

/* ========== mods ========== */

tda_Status tda_deque_push_front(tda_Deque *self, const void *val) {
    ASSERT_DEQUE(self);
    assert(val);

    const tda_Status st = reserve_one(self);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    self->head = slot_back(self, self->head, 1);
    memcpy(slot_at_mut(self, self->head), val, self->elem.size);
    ++self->len;

    return TDA_STATUS_OK;
}

tda_Status tda_deque_push_back(tda_Deque *self, const void *val) {
    ASSERT_DEQUE(self);
    assert(val);

    const tda_Status st = reserve_one(self);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    memcpy(elem_at_mut(self, self->len), val, self->elem.size);
    ++self->len;

    return TDA_STATUS_OK;
}

void tda_deque_pop_front(tda_Deque *self) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(self->len > 0);

    self->head = slot_fwd(self, self->head, 1);
    --self->len;
}

void tda_deque_pop_back(tda_Deque *self) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(self->len > 0);

    --self->len;
}

void tda_deque_pop_front_to(tda_Deque *self, void *out) {
    ASSERT_DEQUE(self);
    assert(out);
    TDA_EXPECT(self->len > 0);

    // memmove: 'out' may alias the slot being dropped
    memmove(out, elem_at(self, 0), self->elem.size);
    tda_deque_pop_front(self);
}

void tda_deque_pop_back_to(tda_Deque *self, void *out) {
    ASSERT_DEQUE(self);
    assert(out);
    TDA_EXPECT(self->len > 0);

    // memmove, as in tda_deque_pop_front_to
    memmove(out, elem_at(self, self->len - 1), self->elem.size);
    tda_deque_pop_back(self);
}

tda_Status tda_deque_insert(tda_Deque *self, size_t idx, const void *val) {
    ASSERT_DEQUE(self);
    assert(val);
    TDA_EXPECT(idx <= self->len);

    return tda_deque_insert_span(self, idx, tda_span_from_data(val, 1, self->elem));
}

void tda_deque_remove(tda_Deque *self, size_t idx) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(idx < self->len);

    tda_deque_remove_range(self, idx, 1);
}

void tda_deque_clear(tda_Deque *self) {
    ASSERT_DEQUE(self);

    self->len = 0;
    self->head = 0;
}

tda_Status tda_deque_reserve(tda_Deque *self, size_t new_cap) {
    ASSERT_DEQUE(self);

    if (new_cap <= self->cap) {
        return TDA_STATUS_OK;
    }

    size_t new_bytes;
    if (ckd_mul(&new_bytes, new_cap, self->elem.size)) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    return relocate(self, new_cap);
}

tda_Status tda_deque_shrink_to_fit(tda_Deque *self) {
    ASSERT_DEQUE(self);

    if (self->len == self->cap) {
        return TDA_STATUS_OK;
    }

    if (self->len == 0) {
        release_data(self);
        return TDA_STATUS_OK;
    }

    // 'len' elems are held now, so their byte count fits
    return relocate(self, self->len);
}

tda_Status tda_deque_resize(tda_Deque *self, size_t new_len) {
    ASSERT_DEQUE(self);

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

    // the new tail may straddle the seam, so it is zeroed a slot at a time rather
    // than with one memset over a range that need not be contiguous
    for (size_t i = self->len; i < new_len; ++i) {
        memset(elem_at_mut(self, i), 0, self->elem.size);
    }
    self->len = new_len;

    ASSERT_DEQUE(self);

    return TDA_STATUS_OK;
}

void tda_deque_truncate(tda_Deque *self, size_t len) {
    ASSERT_DEQUE(self);

    if (len < self->len) {
        self->len = len;
    }
}

void tda_deque_swap(tda_Deque *self, tda_Deque *other) {
    ASSERT_DEQUE(self);
    ASSERT_DEQUE(other);
    TDA_EXPECT(tda_layout_eq(self->elem, other->elem));
    TDA_EXPECT(self->al == other->al);

    if (self == other) {
        return;
    }

    TDA_SWAP(*self, *other);

    ASSERT_DEQUE(self);
    ASSERT_DEQUE(other);
}

void tda_deque_swap_elems(tda_Deque *self, size_t i, size_t j) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(i < self->len);
    TDA_EXPECT(j < self->len);

    if (i == j) {
        return;
    }

    tda_bytes_swap(elem_at_mut(self, i), elem_at_mut(self, j), self->elem.size);
}

/* ========== bulk mods ========== */

tda_Status tda_deque_extend(tda_Deque *self, tda_Span src) {
    ASSERT_DEQUE(self);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(tda_layout_eq(src.elem, self->elem));

    return tda_deque_insert_span(self, self->len, src);
}

tda_Status tda_deque_insert_span(tda_Deque *self, size_t idx, tda_Span src) {
    ASSERT_DEQUE(self);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(tda_layout_eq(src.elem, self->elem));
    TDA_EXPECT(idx <= self->len);

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

    const size_t count = src.len;

    // whichever side is shorter is shifted to open a gap 'count' wide. The two sides move
    // in opposite directions, so each loop runs the way that reads an elem before
    // anything is written over it
    if (idx < self->len - idx) {
        self->head = slot_back(self, self->head, count);
        self->len = new_len;

        for (size_t i = 0; i < idx; ++i) {
            move_elem(self, i, i + count);
        }
    } else {
        self->len = new_len;

        for (size_t i = new_len - 1; i >= idx + count; --i) {
            move_elem(self, i, i - count);
        }
    }

    copy_range_in(self, idx, src.data, count);

    ASSERT_DEQUE(self);

    return TDA_STATUS_OK;
}

void tda_deque_remove_range(tda_Deque *self, size_t idx, size_t count) {
    ASSERT_DEQUE(self);
    TDA_EXPECT(idx <= self->len);
    TDA_EXPECT(count <= self->len - idx);

    if (count == 0) {
        return;
    }

    if (idx < self->len - idx - count) {
        // the front side is shorter: slide it forward over the gap, back elem first
        for (size_t i = idx; i > 0; --i) {
            move_elem(self, i - 1 + count, i - 1);
        }
        self->head = slot_fwd(self, self->head, count);
    } else {
        for (size_t i = idx; i + count < self->len; ++i) {
            move_elem(self, i, i + count);
        }
    }

    self->len -= count;

    ASSERT_DEQUE(self);
}

void tda_deque_retain(tda_Deque *self, tda_Pred pred, void *ctx) {
    ASSERT_DEQUE(self);
    assert(pred);

    size_t kept = 0;
    for (size_t i = 0; i < self->len; ++i) {
        if (!pred(elem_at(self, i), ctx)) {
            continue;
        }
        // until the first drop every kept elem is already in place
        if (kept != i) {
            move_elem(self, kept, i);
        }
        ++kept;
    }
    self->len = kept;
}

/* ========== print ========== */

void tda_deque_fprint(const tda_Deque *self, FILE *stream, tda_FPrint fprint) {
    ASSERT_DEQUE(self);
    assert(stream);
    assert(fprint);

    // not delegated to tda_span_fprint: the ring is up to two spans, and one bracket must
    // hold both. The format matches it on purpose
    fputc('[', stream);
    for (size_t i = 0; i < self->len; ++i) {
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

static tda_Status new_impl(bool zeroed, size_t len, size_t cap, tda_Layout elem, tda_Al *al, tda_Deque **out) {
    assert(len <= cap);
    assert(tda_layout_is_type(elem));
    assert(al);
    assert(out);

    tda_Deque *obj = TDA_ALLOC(tda_Deque, al, 1);
    if (!obj) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    void *data = nullptr;

    if (cap > 0) {
        tda_Layout block;
        if (tda_layout_ckd_array(&block, elem, cap)) {
            goto fail;
        }
        data = tda_alloc(al, block);
        if (!data) {
            goto fail;
        }
        if (zeroed) {
            memset(data, 0, len * elem.size);
        }
    }

    set_fields(obj, data, len, cap, elem, al);

    ASSERT_DEQUE(obj);

    *out = obj;
    return TDA_STATUS_OK;

fail:
    TDA_DEALLOC(tda_Deque, al, obj, 1);

    return TDA_STATUS_ERR_NO_MEM;
}

static void set_fields(tda_Deque *obj, void *data, size_t len, size_t cap, tda_Layout elem, tda_Al *al) {
    obj->data = data;
    obj->len = len;
    obj->cap = cap;
    obj->head = 0;
    obj->elem = elem;
    obj->al = al;
}

static size_t next_cap(const tda_Deque *self) {
    if (self->cap == 0) {
        return DEQUE_GROWTH_BASE;
    }

    size_t grown;
    if (ckd_mul(&grown, self->cap, DEQUE_GROWTH_FACTOR)) {
        return SIZE_MAX;
    }

    return grown;
}

static tda_Status reserve_for(tda_Deque *self, size_t new_len) {
    assert(new_len > self->cap);

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
    if (self->len < self->cap) {
        return TDA_STATUS_OK;
    }

    if (self->cap == SIZE_MAX) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    return reserve_for(self, self->cap + 1);
}

static tda_Status relocate(tda_Deque *self, size_t new_cap) {
    assert(new_cap >= self->len);
    assert(new_cap > 0);

    // a fresh block rather than a realloc: growing in place would leave the wrapped part
    // sitting before the seam, where the new capacity does not reach it. The contents are
    // unrolled into the new block instead, which is the same two memcpy a realloc would
    // have needed anyway
    void *data = tda_alloc(self->al, cap_layout(self, new_cap));
    if (!data) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    copy_out(self, data);
    tda_dealloc(self->al, self->data, cap_layout(self, self->cap));

    self->data = data;
    self->cap = new_cap;
    self->head = 0;

    ASSERT_DEQUE(self);

    return TDA_STATUS_OK;
}

static bool eq_impl(const tda_Deque *a, const tda_Deque *b, tda_Eq eq, void *ctx) {
    if (a == b) {
        return true;
    }

    if (a->len != b->len) {
        return false;
    }

    // by index and not by memcmp over the buffers: two rings holding the same elems start
    // at different slots, and the bytes outside the contents are not contents
    for (size_t i = 0; i < a->len; ++i) {
        const void *x = elem_at(a, i);
        const void *y = elem_at(b, i);
        if (eq ? !eq(x, y, ctx) : memcmp(x, y, a->elem.size) != 0) {
            return false;
        }
    }

    return true;
}

static void release_data(tda_Deque *self) {
    tda_dealloc(self->al, self->data, cap_layout(self, self->cap));
    self->data = nullptr;
    self->len = 0;
    self->cap = 0;
    self->head = 0;
}

static size_t len_bytes(const tda_Deque *self) {
    return self->len * self->elem.size;
}

static tda_Layout cap_layout(const tda_Deque *self, size_t cap) {
    return (tda_Layout){.size = cap * self->elem.size, .align = self->elem.align};
}

static size_t slot_fwd(const tda_Deque *self, size_t slot, size_t count) {
    assert(slot < self->cap);
    assert(count <= self->cap);

    // against the room left rather than through slot + count, which then cannot overflow
    return count < self->cap - slot ? slot + count : count - (self->cap - slot);
}

static size_t slot_back(const tda_Deque *self, size_t slot, size_t count) {
    assert(slot < self->cap);
    assert(count <= self->cap);

    return slot >= count ? slot - count : slot + self->cap - count;
}

static size_t slot_of(const tda_Deque *self, size_t idx) {
    assert(idx < self->cap);

    return slot_fwd(self, self->head, idx);
}

static const unsigned char *slot_at(const tda_Deque *self, size_t slot) {
    return tda_byte_offset(self->data, self->elem.size, slot);
}

static unsigned char *slot_at_mut(tda_Deque *self, size_t slot) {
    return tda_byte_offset_mut(self->data, self->elem.size, slot);
}

static const unsigned char *elem_at(const tda_Deque *self, size_t idx) {
    return slot_at(self, slot_of(self, idx));
}

static unsigned char *elem_at_mut(tda_Deque *self, size_t idx) {
    return slot_at_mut(self, slot_of(self, idx));
}

static size_t run_from(const tda_Deque *self, size_t slot, size_t count) {
    assert(slot <= self->cap);

    const size_t to_end = self->cap - slot;

    return to_end < count ? to_end : count;
}

static size_t first_run(const tda_Deque *self) {
    return run_from(self, self->head, self->len);
}

static void copy_out(const tda_Deque *self, void *dst) {
    if (self->len == 0) {
        return;
    }

    const size_t run = first_run(self);
    memcpy(dst, slot_at(self, self->head), run * self->elem.size);

    if (run < self->len) {
        memcpy(
            tda_byte_offset_mut(dst, self->elem.size, run),
            slot_at(self, 0),
            (self->len - run) * self->elem.size
        );
    }
}

static void copy_range_in(tda_Deque *self, size_t idx, const void *src, size_t count) {
    assert(idx + count <= self->len);

    if (count == 0) {
        return;
    }

    const size_t slot = slot_of(self, idx);
    const size_t run = run_from(self, slot, count);

    memcpy(slot_at_mut(self, slot), src, run * self->elem.size);

    if (run < count) {
        memcpy(
            slot_at_mut(self, 0),
            tda_byte_offset(src, self->elem.size, run),
            (count - run) * self->elem.size
        );
    }
}

static void move_elem(tda_Deque *self, size_t to, size_t from) {
    // two distinct slots of the same ring, so they never overlap
    memcpy(elem_at_mut(self, to), elem_at_mut(self, from), self->elem.size);
}
