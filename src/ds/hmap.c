#include "tda/ds/hmap.h"

#include "tda/core/check.h"
#include "tda/core/util.h"

#include "internal/bits.h"
#include "internal/hmap_impl.h"
#include "internal/order.h"
#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <string.h>

/* ========== internals ========== */

#define ASSERT_HMAP(m)                                                   \
    (assert(m),                                                          \
     assert(tda_layout_is_type((m)->key_)),                              \
     assert((m)->hasher_),                                               \
     assert((m)->eq_),                                                   \
     assert((m)->al_.vt),                                                \
     assert(((m)->bucket_count_ == 0) == ((m)->buckets_ == nullptr)),    \
     assert((m)->bucket_count_ == 0 || tda_is_pow2((m)->bucket_count_)), \
     assert((m)->len_ == 0 || (m)->bucket_count_ > 0))

// two maps whose entries can pass between them or be compared: one key layout, one
// value layout
#define EXPECT_SAME_HMAP(a, b)                        \
    (TDA_EXPECT(tda_layout_eq((a)->key_, (b)->key_)), \
     TDA_EXPECT(tda_layout_eq((a)->val_, (b)->val_)))

static constexpr size_t HMAP_BUCKETS_BASE = 8;
static constexpr size_t HMAP_GROWTH_FACTOR = 2;

// bucket_in masks, so every bucket count must be a power of two
static_assert((HMAP_BUCKETS_BASE & (HMAP_BUCKETS_BASE - 1)) == 0);
static_assert((HMAP_GROWTH_FACTOR & (HMAP_GROWTH_FACTOR - 1)) == 0);

// An entry is one allocation: this struct, then the key and the value, each at the first
// offset its alignment allows, as members of a struct would be. Only the map knows those
// offsets. The hash is cached so growing relinks without rehashing, and a lookup rejects
// most mismatches before calling 'eq'.
struct tda_HMapNode {
    tda_HMapNode *next;
    tda_Hash hash;
};

[[nodiscard]]
static tda_Layout node_layout(const tda_HMap *self);

[[nodiscard]]
static const void *node_key(const tda_HMap *self, const tda_HMapNode *node);

[[nodiscard]]
static void *node_val_mut(const tda_HMap *self, tda_HMapNode *node);

[[nodiscard]]
static const void *node_val(const tda_HMap *self, const tda_HMapNode *node);

[[nodiscard]]
static tda_Status alloc_node(const tda_HMap *self, const void *key, const void *val, tda_Hash hash, tda_HMapNode **out);

static void dealloc_node(const tda_HMap *self, tda_HMapNode *node);

/// which of 'count' buckets a hash belongs to. The count is a power of two, so this is a
/// mask, which is safe only because the mixer in core/hash avalanches every bit
[[nodiscard]]
static size_t bucket_in(tda_Hash hash, size_t count);

/// bucket_in over the map's own buckets
[[nodiscard]]
static size_t bucket_of(const tda_HMap *self, tda_Hash hash);

/// whether 'node' is the entry for 'key'; the one place that decides it
[[nodiscard]]
static bool holds_key(const tda_HMap *self, const tda_HMapNode *node, const void *key, tda_Hash hash);

/// the link that points at the entry for 'key', or the null link ending its chain
[[nodiscard]]
static tda_HMapNode **find_link(const tda_HMap *self, const void *key, tda_Hash hash);

[[nodiscard]]
static tda_HMapNode *find_node(const tda_HMap *self, const void *key, tda_Hash hash);

/// puts 'node' at the head of its bucket among 'count'
static void link_node(tda_HMapNode **buckets, size_t count, tda_HMapNode *node);

/// takes the node at '*link' out of its chain and frees it
static void remove_at(tda_HMap *self, tda_HMapNode **link);

/// the node after 'node' in walk order: down its chain, then on to the next bucket
[[nodiscard]]
static tda_HMapNode *next_node(const tda_HMap *self, const tda_HMapNode *node);

/// builds the entry for a key known to be absent and links it into its bucket. Shared by
/// insert and get_or_insert, which differ only when the key is present
[[nodiscard]]
static tda_Status add_node(tda_HMap *self, const void *key, const void *val, tda_Hash hash, tda_HMapNode **out);

/// the bucket count that holds 'len' entries at the load of one entry per bucket: the
/// smallest power of two at least 'len' and the base, or 0 on overflow
[[nodiscard]]
static size_t buckets_for(size_t len);

/// moves every node into a fresh bucket array of 'new_count'. Only the array is
/// allocated; the nodes are relinked, so borrowed nodes stay valid
[[nodiscard]]
static tda_Status rehash(tda_HMap *self, size_t new_count);

/// buckets for 'cap' entries at the load of one per bucket; the body of tda_hmap_reserve,
/// kept static so the insert path calls it directly
[[nodiscard]]
static tda_Status reserve_buckets(tda_HMap *self, size_t cap);

/// room for one more entry
[[nodiscard]]
static tda_Status reserve_one(tda_HMap *self);

/// the first node from bucket 'idx' onward, or null when the rest are empty
[[nodiscard]]
static tda_HMapNode *first_from(const tda_HMap *self, size_t idx);

/// frees every node; the bucket array still points at them and len_ still counts them,
/// so the caller clears or frees the array next
static void free_nodes(tda_HMap *self);

/// frees the nodes and the bucket array, leaving a map that owns nothing, like any
/// container after a move
static void release_buckets(tda_HMap *self);

/// the state release_buckets and a move leave: no buckets, no entries; frees nothing
static void forget_buckets(tda_HMap *self);

/// shared by eq and eq_by; a null 'val_eq' compares bytes
[[nodiscard]]
static bool eq_impl(const tda_HMap *a, const tda_HMap *b, tda_Eq val_eq, void *ctx);

/* ========== lifetime ========== */

tda_HMap tda_hmap_init(
    tda_Layout key, tda_Layout val,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al al
) {
    assert(tda_layout_is_type(key));
    assert(tda_layout_is_type(val)); // the zero belongs to internal/hmap_impl.h and to ds/hset alone
    assert(hasher);
    assert(eq);
    assert(al.vt);

    // with no room asked for nothing is allocated, so this cannot fail
    tda_HMap obj;
    [[maybe_unused]] const tda_Status st = tda_hmap_init_raw_(0, key, val, hasher, eq, ctx, al, &obj);
    assert(TDA_STATUS_IS_OK(st));

    return obj;
}

tda_Status tda_hmap_init_cap(
    size_t cap,
    tda_Layout key, tda_Layout val,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al al,
    tda_HMap *out
) {
    assert(tda_layout_is_type(key));
    assert(tda_layout_is_type(val)); // the zero belongs to internal/hmap_impl.h and to ds/hset alone
    assert(hasher);
    assert(eq);
    assert(al.vt);
    assert(out);

    return tda_hmap_init_raw_(cap, key, val, hasher, eq, ctx, al, out);
}

tda_Status tda_hmap_from_spans(
    tda_Span keys, tda_Span vals,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al al,
    tda_HMap *out
) {
    TDA_SPAN_ASSERT(keys);
    TDA_SPAN_ASSERT(vals);
    assert(hasher);
    assert(eq);
    assert(al.vt);
    assert(out);
    TDA_EXPECT(keys.len == vals.len);

    tda_HMap obj;
    tda_Status st = tda_hmap_init_cap(keys.len, keys.elem, vals.elem, hasher, eq, ctx, al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    st = tda_hmap_extend(&obj, keys, vals);
    if (TDA_STATUS_IS_ERR(st)) {
        tda_hmap_deinit(&obj);
        return st;
    }

    *out = obj;

    return TDA_STATUS_OK;
}

tda_Status tda_hmap_init_raw_(
    size_t cap,
    tda_Layout key, tda_Layout val,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al al,
    tda_HMap *out
) {
    assert(tda_layout_is_type(key));
    assert(tda_layout_is_type(val) || (val.size == 0 && tda_layout_is_align(val.align)));
    assert(hasher);
    assert(eq);
    assert(al.vt);
    assert(out);

    // the node struct, the key, the value, laid out as a struct's members; with no value there
    // is nothing to place after the key, so a set's node is just the struct and the key.
    // No real type comes near overflowing it; a layout built by hand might, and its nodes
    // would be too short for what is copied into them
    tda_Layout node = TDA_LAYOUT_OF(tda_HMapNode);
    size_t key_offset = 0, val_offset = 0;
    [[maybe_unused]] const bool overflow = tda_layout_ckd_extend(&node, key, &key_offset)
        || (val.size > 0 && tda_layout_ckd_extend(&node, val, &val_offset))
        || tda_layout_ckd_pad(&node);
    TDA_EXPECT(!overflow);
    if (val.size == 0) {
        val_offset = key_offset + key.size; // an empty value right past the key, not the struct
    }

    tda_HMap obj = {
        .key_ = key,
        .val_ = val,
        .key_offset_ = key_offset,
        .val_offset_ = val_offset,
        .node_ = node,
        .hasher_ = hasher,
        .eq_ = eq,
        .ctx_ = ctx,
        .al_ = al,
    };

    if (cap > 0) {
        const tda_Status st = tda_hmap_reserve(&obj, cap);
        if (TDA_STATUS_IS_ERR(st)) {
            return st;
        }
    }

    *out = obj;

    ASSERT_HMAP(out);

    return TDA_STATUS_OK;
}

void tda_hmap_deinit(tda_HMap *self) {
    assert(self);

    // the inert state, never initialized or already released
    if (!self->al_.vt) {
        assert(!self->buckets_);
        return;
    }

    ASSERT_HMAP(self);

    release_buckets(self);
    *self = (tda_HMap){};
}

/* ========== copy ========== */

tda_Status tda_hmap_copy(const tda_HMap *self, tda_HMap *out) {
    ASSERT_HMAP(self);
    assert(out);

    return tda_hmap_copy_with(self, self->al_, out);
}

tda_Status tda_hmap_copy_with(const tda_HMap *self, tda_Al al, tda_HMap *out) {
    ASSERT_HMAP(self);
    assert(al.vt);
    assert(out);

    tda_HMap obj;
    tda_Status st = tda_hmap_init_raw_(
        self->len_,
        self->key_,
        self->val_,
        self->hasher_,
        self->eq_,
        self->ctx_,
        al,
        &obj
    );
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // the keys are distinct and the copy hashes as the source does, so each node goes
    // straight in under its cached hash, with no search and no hasher call
    for (const tda_HMapNode *node = first_from(self, 0); node; node = next_node(self, node)) {
        const void *val = self->val_.size > 0 ? node_val(self, node) : nullptr;
        tda_HMapNode *added;
        st = add_node(&obj, node_key(self, node), val, node->hash, &added);
        if (TDA_STATUS_IS_ERR(st)) {
            tda_hmap_deinit(&obj);
            return st;
        }
    }

    *out = obj;

    return TDA_STATUS_OK;
}

tda_Status tda_hmap_copy_assign(tda_HMap *self, const tda_HMap *other) {
    ASSERT_HMAP(self);
    ASSERT_HMAP(other);
    EXPECT_SAME_HMAP(self, other);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    // build the clone first, so a refusal leaves 'self' unchanged
    tda_HMap clone;
    const tda_Status st = tda_hmap_copy_with(other, self->al_, &clone);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    tda_hmap_deinit(self);
    *self = clone;

    ASSERT_HMAP(self);

    return TDA_STATUS_OK;
}

void tda_hmap_move_assign(tda_HMap *self, tda_HMap *other) {
    ASSERT_HMAP(self);
    ASSERT_HMAP(other);
    EXPECT_SAME_HMAP(self, other);

    if (self == other) {
        return;
    }

    // the allocator, the hasher, the equality and the ctx go along with the entries;
    // 'other' keeps all of them and is left with no buckets
    tda_hmap_deinit(self);
    *self = *other;
    forget_buckets(other);

    ASSERT_HMAP(self);
    ASSERT_HMAP(other);
}

/* ========== compare ========== */

bool tda_hmap_eq(const tda_HMap *a, const tda_HMap *b) {
    ASSERT_HMAP(a);
    ASSERT_HMAP(b);
    EXPECT_SAME_HMAP(a, b);
    assert(a->eq_ == b->eq_);

    return eq_impl(a, b, nullptr, nullptr);
}

bool tda_hmap_eq_by(const tda_HMap *a, const tda_HMap *b, tda_Eq val_eq, void *ctx) {
    ASSERT_HMAP(a);
    ASSERT_HMAP(b);
    assert(val_eq);
    EXPECT_SAME_HMAP(a, b);
    assert(a->eq_ == b->eq_);

    return eq_impl(a, b, val_eq, ctx);
}

/* ========== lookup ========== */

const void *tda_hmap_get(const tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    const tda_HMapNode *node = find_node(self, key, self->hasher_(key, self->ctx_));

    return node ? node_val(self, node) : nullptr;
}

void *tda_hmap_get_mut(tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    tda_HMapNode *node = find_node(self, key, self->hasher_(key, self->ctx_));

    return node ? node_val_mut(self, node) : nullptr;
}

bool tda_hmap_contains(const tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    return find_node(self, key, self->hasher_(key, self->ctx_)) != nullptr;
}

const tda_HMapNode *tda_hmap_find(const tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    return find_node(self, key, self->hasher_(key, self->ctx_));
}

tda_HMapNode *tda_hmap_find_mut(tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    return find_node(self, key, self->hasher_(key, self->ctx_));
}

/* ========== nodes ========== */

const tda_HMapNode *tda_hmap_first_node(const tda_HMap *self) {
    ASSERT_HMAP(self);

    return first_from(self, 0);
}

tda_HMapNode *tda_hmap_first_node_mut(tda_HMap *self) {
    ASSERT_HMAP(self);

    return first_from(self, 0);
}

const tda_HMapNode *tda_hmap_node_next(const tda_HMap *self, const tda_HMapNode *node) {
    ASSERT_HMAP(self);
    assert(node);

    return next_node(self, node);
}

tda_HMapNode *tda_hmap_node_next_mut(tda_HMap *self, tda_HMapNode *node) {
    ASSERT_HMAP(self);
    assert(node);

    return next_node(self, node);
}

const void *tda_hmap_node_key(const tda_HMap *self, const tda_HMapNode *node) {
    ASSERT_HMAP(self);
    assert(node);

    return node_key(self, node);
}

const void *tda_hmap_node_val(const tda_HMap *self, const tda_HMapNode *node) {
    ASSERT_HMAP(self);
    assert(node);

    return node_val(self, node);
}

void *tda_hmap_node_val_mut(tda_HMap *self, tda_HMapNode *node) {
    ASSERT_HMAP(self);
    assert(node);

    return node_val_mut(self, node);
}

/* ========== mods ========== */

tda_Status tda_hmap_insert(tda_HMap *self, const void *key, const void *val, bool *out_is_new) {
    ASSERT_HMAP(self);
    assert(key);
    assert(val || self->val_.size == 0); // a value pointer is needed only when there is a value

    const tda_Hash hash = self->hasher_(key, self->ctx_);

    tda_HMapNode *found = find_node(self, key, hash);
    if (found) {
        if (self->val_.size > 0) {
            // memmove: 'val' may alias the value being overwritten
            memmove(node_val_mut(self, found), val, self->val_.size);
        }

        if (out_is_new) {
            *out_is_new = false;
        }

        return TDA_STATUS_OK;
    }

    // a refused node may still have grown the bucket array
    tda_HMapNode *node;
    const tda_Status st = add_node(self, key, val, hash, &node);

    ASSERT_HMAP(self);

    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    if (out_is_new) {
        *out_is_new = true;
    }

    return TDA_STATUS_OK;
}

tda_Status tda_hmap_extend(tda_HMap *self, tda_Span keys, tda_Span vals) {
    ASSERT_HMAP(self);
    TDA_SPAN_ASSERT(keys);
    TDA_SPAN_ASSERT(vals);
    TDA_EXPECT(tda_layout_eq(keys.elem, self->key_));
    TDA_EXPECT(tda_layout_eq(vals.elem, self->val_));
    TDA_EXPECT(keys.len == vals.len);

    for (size_t i = 0; i < keys.len; ++i) {
        const tda_Status st = tda_hmap_insert(self, tda_span_get(keys, i), tda_span_get(vals, i), nullptr);
        if (TDA_STATUS_IS_ERR(st)) {
            ASSERT_HMAP(self);
            return st;
        }
    }

    ASSERT_HMAP(self);

    return TDA_STATUS_OK;
}

tda_Status tda_hmap_get_or_insert(
    tda_HMap *self,
    const void *key,
    const void *val_if_absent,
    tda_HMapNode **out_node
) {
    ASSERT_HMAP(self);
    assert(key);
    assert(val_if_absent || self->val_.size == 0);
    assert(out_node);

    // one hash and one bucket walk serve both the lookup and the insert
    const tda_Hash hash = self->hasher_(key, self->ctx_);

    tda_HMapNode *found = find_node(self, key, hash);
    if (found) {
        *out_node = found;
        return TDA_STATUS_OK;
    }

    // a refused node may still have grown the bucket array
    const tda_Status st = add_node(self, key, val_if_absent, hash, out_node);

    ASSERT_HMAP(self);

    return st;
}

bool tda_hmap_remove(tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    const bool removed = tda_hmap_take(self, key, nullptr, nullptr);

    ASSERT_HMAP(self);

    return removed;
}

bool tda_hmap_take(tda_HMap *self, const void *key, void *out_key, void *out_val) {
    ASSERT_HMAP(self);
    assert(key);

    if (self->bucket_count_ == 0) {
        return false;
    }

    tda_HMapNode **link = find_link(self, key, self->hasher_(key, self->ctx_));
    if (!*link) {
        return false;
    }

    // memmove: 'out_key' may alias 'key'
    if (out_key) {
        memmove(out_key, node_key(self, *link), self->key_.size);
    }
    if (out_val) {
        memcpy(out_val, node_val(self, *link), self->val_.size);
    }
    remove_at(self, link);

    ASSERT_HMAP(self);

    return true;
}

void tda_hmap_remove_node(tda_HMap *self, tda_HMapNode *node) {
    ASSERT_HMAP(self);
    assert(node);
    TDA_EXPECT(self->len_ > 0);

    tda_HMapNode **link = &self->buckets_[bucket_of(self, node->hash)];
    while (*link && *link != node) {
        link = &(*link)->next;
    }

    // a node of another map would splice its chain into this one; the walk is already
    // done, so the check is free
    TDA_EXPECT(*link == node);

    remove_at(self, link);

    ASSERT_HMAP(self);
}

void tda_hmap_retain(tda_HMap *self, tda_EntryPred pred, void *ctx) {
    ASSERT_HMAP(self);
    assert(pred);

    for (size_t i = 0; i < self->bucket_count_; ++i) {
        tda_HMapNode **link = &self->buckets_[i];
        while (*link) {
            tda_HMapNode *node = *link;
            if (pred(node_key(self, node), node_val(self, node), ctx)) {
                link = &node->next;
                continue;
            }
            remove_at(self, link);
        }
    }

    ASSERT_HMAP(self);
}

void tda_hmap_clear(tda_HMap *self) {
    ASSERT_HMAP(self);

    free_nodes(self);

    for (size_t i = 0; i < self->bucket_count_; ++i) {
        self->buckets_[i] = nullptr;
    }

    self->len_ = 0;

    ASSERT_HMAP(self);
}

tda_Status tda_hmap_reserve(tda_HMap *self, size_t new_cap) {
    ASSERT_HMAP(self);

    const tda_Status st = reserve_buckets(self, new_cap);

    ASSERT_HMAP(self);

    return st;
}

tda_Status tda_hmap_shrink_to_fit(tda_HMap *self) {
    ASSERT_HMAP(self);

    if (self->len_ == 0) {
        // empty: the map goes back to owning no buckets
        release_buckets(self);

        ASSERT_HMAP(self);

        return TDA_STATUS_OK;
    }

    const size_t want = buckets_for(self->len_);
    if (want == 0 || want >= self->bucket_count_) {
        return TDA_STATUS_OK;
    }

    const tda_Status st = rehash(self, want);

    ASSERT_HMAP(self);

    return st;
}

void tda_hmap_swap(tda_HMap *self, tda_HMap *other) {
    ASSERT_HMAP(self);
    ASSERT_HMAP(other);
    EXPECT_SAME_HMAP(self, other);

    if (self == other) {
        return;
    }

    TDA_SWAP(*self, *other);

    ASSERT_HMAP(self);
    ASSERT_HMAP(other);
}

/* ========== print ========== */

int tda_hmap_fprint(
    const tda_HMap *self, FILE *stream,
    tda_FPrint key_fprint, void *key_ctx,
    tda_FPrint val_fprint, void *val_ctx
) {
    ASSERT_HMAP(self);
    assert(stream);
    assert(key_fprint);
    assert(val_fprint);

    int rc = fputc('{', stream);
    bool first = true;
    for (const tda_HMapNode *node = first_from(self, 0); node; node = next_node(self, node)) {
        if (!first) {
            rc = tda_fprint_join_(rc, fputs(", ", stream));
        }
        first = false;
        rc = tda_fprint_join_(rc, key_fprint(stream, node_key(self, node), key_ctx));
        rc = tda_fprint_join_(rc, fputs(": ", stream));
        rc = tda_fprint_join_(rc, val_fprint(stream, node_val(self, node), val_ctx));
    }

    return tda_fprint_join_(rc, fputc('}', stream));
}

int tda_hmap_print(
    const tda_HMap *self,
    tda_FPrint key_fprint, void *key_ctx,
    tda_FPrint val_fprint, void *val_ctx
) {
    ASSERT_HMAP(self);
    assert(key_fprint);
    assert(val_fprint);

    const int rc = tda_hmap_fprint(self, stdout, key_fprint, key_ctx, val_fprint, val_ctx);

    return tda_fprint_join_(rc, fputc('\n', stdout));
}

/* ========== internals ========== */

static tda_Layout node_layout(const tda_HMap *self) {
    return self->node_;
}

static const void *node_key(const tda_HMap *self, const tda_HMapNode *node) {
    return tda_byte_offset(node, 1, self->key_offset_);
}

static void *node_val_mut(const tda_HMap *self, tda_HMapNode *node) {
    return tda_byte_offset_mut(node, 1, self->val_offset_);
}

static const void *node_val(const tda_HMap *self, const tda_HMapNode *node) {
    return tda_byte_offset(node, 1, self->val_offset_);
}

static tda_Status alloc_node(const tda_HMap *self, const void *key, const void *val, tda_Hash hash, tda_HMapNode **out) {
    tda_HMapNode *node = tda_alloc(self->al_, node_layout(self));
    if (!node) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    node->next = nullptr;
    node->hash = hash;
    memcpy(tda_byte_offset_mut(node, 1, self->key_offset_), key, self->key_.size);
    if (self->val_.size > 0) {
        memcpy(node_val_mut(self, node), val, self->val_.size);
    }

    *out = node;

    return TDA_STATUS_OK;
}

static void dealloc_node(const tda_HMap *self, tda_HMapNode *node) {
    tda_dealloc(self->al_, node, node_layout(self));
}

static size_t bucket_in(tda_Hash hash, size_t count) {
    assert(tda_is_pow2(count));

    return (size_t) hash & (count - 1);
}

static size_t bucket_of(const tda_HMap *self, tda_Hash hash) {
    assert(self->bucket_count_ > 0);

    return bucket_in(hash, self->bucket_count_);
}

static bool holds_key(const tda_HMap *self, const tda_HMapNode *node, const void *key, tda_Hash hash) {
    // the cheap hash first, so 'eq' runs only on keys whose hashes match
    return node->hash == hash && self->eq_(node_key(self, node), key, self->ctx_);
}

static tda_HMapNode **find_link(const tda_HMap *self, const void *key, tda_Hash hash) {
    // walk the links, not the nodes: the chain is singly linked, so the link stands in
    // for the previous node when the entry is taken out
    tda_HMapNode **link = &self->buckets_[bucket_of(self, hash)];
    while (*link && !holds_key(self, *link, key, hash)) {
        link = &(*link)->next;
    }

    return link;
}

static tda_HMapNode *find_node(const tda_HMap *self, const void *key, tda_Hash hash) {
    if (self->bucket_count_ == 0) {
        return nullptr;
    }

    // its own walk over nodes rather than *find_link: a lookup is the hot path, and the
    // walk over links measured slower here
    for (tda_HMapNode *node = self->buckets_[bucket_of(self, hash)]; node; node = node->next) {
        if (holds_key(self, node, key, hash)) {
            return node;
        }
    }

    return nullptr;
}

static void link_node(tda_HMapNode **buckets, size_t count, tda_HMapNode *node) {
    const size_t bucket = bucket_in(node->hash, count);
    node->next = buckets[bucket];
    buckets[bucket] = node;
}

static void remove_at(tda_HMap *self, tda_HMapNode **link) {
    tda_HMapNode *node = *link;
    assert(node);

    *link = node->next;
    dealloc_node(self, node);
    --self->len_;
}

static tda_HMapNode *next_node(const tda_HMap *self, const tda_HMapNode *node) {
    if (node->next) {
        return node->next;
    }

    return first_from(self, bucket_of(self, node->hash) + 1);
}

static tda_Status add_node(tda_HMap *self, const void *key, const void *val, tda_Hash hash, tda_HMapNode **out) {
    // reserve first: growing may change which bucket the node belongs to
    tda_Status st = reserve_one(self);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    tda_HMapNode *node;
    st = alloc_node(self, key, val, hash, &node);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    link_node(self->buckets_, self->bucket_count_, node);
    ++self->len_;

    ASSERT_HMAP(self);

    *out = node;

    return TDA_STATUS_OK;
}

static size_t buckets_for(size_t len) {
    size_t count = HMAP_BUCKETS_BASE;
    while (count < len) {
        size_t grown;
        if (ckd_mul(&grown, count, HMAP_GROWTH_FACTOR)) {
            return 0;
        }
        count = grown;
    }

    return count;
}

static tda_Status rehash(tda_HMap *self, size_t new_count) {
    assert(tda_is_pow2(new_count));

    tda_HMapNode **buckets = TDA_ALLOC_ZEROED(tda_HMapNode *, self->al_, new_count);
    if (!buckets) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    for (size_t i = 0; i < self->bucket_count_; ++i) {
        tda_HMapNode *node = self->buckets_[i];
        while (node) {
            tda_HMapNode *next = node->next;
            link_node(buckets, new_count, node);
            node = next;
        }
    }

    TDA_DEALLOC(tda_HMapNode *, self->al_, self->buckets_, self->bucket_count_);
    self->buckets_ = buckets;
    self->bucket_count_ = new_count;

    ASSERT_HMAP(self);

    return TDA_STATUS_OK;
}

static tda_Status reserve_buckets(tda_HMap *self, size_t cap) {
    if (cap <= self->bucket_count_) {
        return TDA_STATUS_OK;
    }

    const size_t want = buckets_for(cap);
    if (want == 0) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    return rehash(self, want);
}

static tda_Status reserve_one(tda_HMap *self) {
    // len is at most the bucket count, which is far below SIZE_MAX, so len + 1 fits
    return reserve_buckets(self, self->len_ + 1);
}

static tda_HMapNode *first_from(const tda_HMap *self, size_t idx) {
    for (size_t i = idx; i < self->bucket_count_; ++i) {
        if (self->buckets_[i]) {
            return self->buckets_[i];
        }
    }

    return nullptr;
}

static void free_nodes(tda_HMap *self) {
    for (size_t i = 0; i < self->bucket_count_; ++i) {
        tda_HMapNode *node = self->buckets_[i];
        while (node) {
            tda_HMapNode *next = node->next;
            dealloc_node(self, node);
            node = next;
        }
    }
}

static void release_buckets(tda_HMap *self) {
    free_nodes(self);
    TDA_DEALLOC(tda_HMapNode *, self->al_, self->buckets_, self->bucket_count_);
    forget_buckets(self);
}

static void forget_buckets(tda_HMap *self) {
    self->buckets_ = nullptr;
    self->bucket_count_ = 0;
    self->len_ = 0;
}

static bool eq_impl(const tda_HMap *a, const tda_HMap *b, tda_Eq val_eq, void *ctx) {
    if (a == b) {
        return true;
    }

    if (a->len_ != b->len_) {
        return false;
    }

    // equal lengths plus every key of 'a' in 'b' means containment both ways, since
    // the two share one equality, so one pass is enough. 'b' uses its own hasher
    for (size_t i = 0; i < a->bucket_count_; ++i) {
        for (const tda_HMapNode *node = a->buckets_[i]; node; node = node->next) {
            const void *key = node_key(a, node);
            const tda_HMapNode *found = find_node(b, key, b->hasher_(key, b->ctx_));
            if (!found) {
                return false;
            }

            if (!tda_elem_eq(val_eq, ctx, node_val(a, node), node_val(b, found), a->val_.size)) {
                return false;
            }
        }
    }

    return true;
}
