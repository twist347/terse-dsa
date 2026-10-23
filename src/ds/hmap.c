#include "tda/ds/hmap.h"

#include "tda/core/check.h"
#include "tda/core/util.h"

#include "internal/hmap_impl.h"
#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <string.h>

/* ========== internals ========== */

#define ASSERT_HMAP(m)                                              \
    (assert(m),                                                     \
     assert(tda_layout_is_type((m)->key)),                          \
     assert((m)->hasher),                                           \
     assert((m)->eq),                                               \
     assert((m)->al),                                               \
     assert(((m)->bucket_count == 0) == ((m)->buckets == nullptr)), \
     assert((m)->bucket_count == 0                                  \
            || ((m)->bucket_count & ((m)->bucket_count - 1)) == 0), \
     assert((m)->len == 0 || (m)->bucket_count > 0))

static constexpr size_t HMAP_BUCKETS_BASE = 8;
static constexpr size_t HMAP_GROWTH_FACTOR = 2;

// bucket_in masks, so every bucket count must be a power of two
static_assert((HMAP_BUCKETS_BASE & (HMAP_BUCKETS_BASE - 1)) == 0);
static_assert((HMAP_GROWTH_FACTOR & (HMAP_GROWTH_FACTOR - 1)) == 0);

// An entry is one allocation: this header, then the key and the value, each at the first
// offset its alignment allows, as members of a struct would be. Only the map knows those
// offsets. The hash is cached so growing relinks without rehashing, and a lookup rejects
// most mismatches before calling 'eq'.
struct tda_HMapNode {
    tda_HMapNode *next;
    tda_Hash hash;
};

struct tda_HMap {
    tda_HMapNode **buckets;
    size_t bucket_count;
    size_t len;
    tda_Layout key;
    tda_Layout val; // size 0 for a set's map, which has no value
    size_t key_offset;
    size_t val_offset;
    tda_Layout node; // checked against overflow once, at construction
    tda_Hasher hasher;
    tda_Eq eq;
    void *ctx; // passed to both: they agree on what a key is
    tda_Al *al;
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
static tda_Status node_new(const tda_HMap *self, const void *key, const void *val, tda_Hash hash, tda_HMapNode **out);

static void node_drop(const tda_HMap *self, tda_HMapNode *node);

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
static void unlink_at(tda_HMap *self, tda_HMapNode **link);

/// the node after 'node' in walk order: down its chain, then on to the next bucket
[[nodiscard]]
static tda_HMapNode *next_node(const tda_HMap *self, const tda_HMapNode *node);

/// builds the entry for a key known to be absent and links it into its bucket. Shared by
/// insert and get_or_insert, which differ only when the key is present
[[nodiscard]]
static tda_Status add_node(tda_HMap *self, const void *key, const void *val, tda_Hash hash, tda_HMapNode **out);

/// the bucket count that holds 'n' entries at the load of one entry per bucket: the
/// smallest power of two at least 'n' and the base, or 0 on overflow
[[nodiscard]]
static size_t buckets_for(size_t n);

/// moves every node into a fresh bucket array of 'new_count'. Only the array is
/// allocated; the nodes are relinked, so borrowed nodes stay valid
[[nodiscard]]
static tda_Status rehash(tda_HMap *self, size_t new_count);

/// buckets for 'cap' entries at the load of one per bucket; the body of tda_hmap_reserve,
/// kept static so the insert path calls it directly
[[nodiscard]]
static tda_Status reserve_for(tda_HMap *self, size_t cap);

/// room for one more entry
[[nodiscard]]
static tda_Status reserve_one(tda_HMap *self);

/// the first node from bucket 'idx' onward, or null when the rest are empty
[[nodiscard]]
static tda_HMapNode *first_from(const tda_HMap *self, size_t idx);

static void clear_nodes(tda_HMap *self);

/// frees the nodes and the bucket array, leaving a map that owns nothing, like any
/// container after a move
static void release_buckets(tda_HMap *self);

/// shared by eq and eq_by; a null 'val_eq' compares bytes
[[nodiscard]]
static bool eq_impl(const tda_HMap *a, const tda_HMap *b, tda_Eq val_eq, void *ctx);

/* ========== lifetime ========== */

tda_Status tda_hmap_new(
    tda_Layout key, tda_Layout val,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al *al,
    tda_HMap **out
) {
    return tda_hmap_new_cap(0, key, val, hasher, eq, ctx, al, out);
}

tda_Status tda_hmap_new_cap(
    size_t cap,
    tda_Layout key, tda_Layout val,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al *al,
    tda_HMap **out
) {
    // the zero belongs to internal/hmap_impl.h and to ds/hset alone
    assert(tda_layout_is_type(val));

    return tda_hmap_new_raw_(cap, key, val, hasher, eq, ctx, al, out);
}

tda_Status tda_hmap_from_spans(
    tda_Span keys,
    tda_Span vals,
    tda_Hasher hasher,
    tda_Eq eq,
    void *ctx,
    tda_Al *al,
    tda_HMap **out
) {
    TDA_SPAN_ASSERT(keys);
    TDA_SPAN_ASSERT(vals);
    TDA_EXPECT(keys.len == vals.len);
    assert(out);

    tda_HMap *obj;
    tda_Status st = tda_hmap_new_cap(keys.len, keys.elem, vals.elem, hasher, eq, ctx, al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    st = tda_hmap_extend(obj, keys, vals);
    if (TDA_STATUS_IS_ERR(st)) {
        tda_hmap_drop(obj);
        return st;
    }

    *out = obj;
    return TDA_STATUS_OK;
}

tda_Status tda_hmap_new_raw_(
    size_t cap, tda_Layout key, tda_Layout val,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al *al,
    tda_HMap **out
) {
    assert(tda_layout_is_type(key));
    assert(tda_layout_is_type(val) || (val.size == 0 && tda_layout_is_align(val.align)));
    assert(hasher);
    assert(eq);
    assert(al);
    assert(out);

    // the header, the key, the value, laid out as a struct's members; with no value there
    // is nothing to place after the key, so a set's node is just the header and the key
    tda_Layout node = TDA_LAYOUT_OF(tda_HMapNode);
    size_t key_offset;
    size_t val_offset;
    if (tda_layout_ckd_extend(&node, key, &key_offset)
        || (val.size > 0 && tda_layout_ckd_extend(&node, val, &val_offset))
        || tda_layout_ckd_pad(&node)) {
        return TDA_STATUS_ERR_NO_MEM;
    }
    if (val.size == 0) {
        val_offset = key_offset + key.size; // an empty value right past the key, not the header
    }

    tda_HMap *obj = TDA_ALLOC(tda_HMap, al, 1);
    if (!obj) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    obj->buckets = nullptr;
    obj->bucket_count = 0;
    obj->len = 0;
    obj->key = key;
    obj->val = val;
    obj->key_offset = key_offset;
    obj->val_offset = val_offset;
    obj->node = node;
    obj->hasher = hasher;
    obj->eq = eq;
    obj->ctx = ctx;
    obj->al = al;

    if (cap > 0) {
        const tda_Status st = tda_hmap_reserve(obj, cap);
        if (TDA_STATUS_IS_ERR(st)) {
            TDA_DEALLOC(tda_HMap, al, obj, 1);
            return st;
        }
    }

    ASSERT_HMAP(obj);

    *out = obj;

    return TDA_STATUS_OK;
}

void tda_hmap_drop(tda_HMap *self) {
    if (!self) {
        return;
    }

    ASSERT_HMAP(self);

    release_buckets(self);
    TDA_DEALLOC(tda_HMap, self->al, self, 1);
}

/* ========== copy ========== */

tda_Status tda_hmap_copy(const tda_HMap *self, tda_HMap **out) {
    ASSERT_HMAP(self);

    return tda_hmap_copy_with(self, self->al, out);
}

tda_Status tda_hmap_copy_with(const tda_HMap *self, tda_Al *al, tda_HMap **out) {
    ASSERT_HMAP(self);
    assert(al);
    assert(out);

    tda_HMap *obj;
    tda_Status st = tda_hmap_new_raw_(self->len, self->key, self->val, self->hasher, self->eq, self->ctx, al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    for (const tda_HMapNode *node = first_from(self, 0); node; node = tda_hmap_node_next(self, node)) {
        const void *val = self->val.size > 0 ? node_val(self, node) : nullptr;
        st = tda_hmap_insert(obj, node_key(self, node), val, nullptr);
        if (TDA_STATUS_IS_ERR(st)) {
            tda_hmap_drop(obj);
            return st;
        }
    }

    *out = obj;

    return TDA_STATUS_OK;
}

tda_Status tda_hmap_copy_assign(tda_HMap *self, const tda_HMap *other) {
    ASSERT_HMAP(self);
    ASSERT_HMAP(other);
    TDA_EXPECT(tda_layout_eq(self->key, other->key));
    TDA_EXPECT(tda_layout_eq(self->val, other->val));

    if (self == other) {
        return TDA_STATUS_OK;
    }

    // build the clone first, so a refusal leaves 'self' unchanged
    tda_HMap *clone;
    const tda_Status st = tda_hmap_copy_with(other, self->al, &clone);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    TDA_SWAP(*self, *clone);
    tda_hmap_drop(clone);

    ASSERT_HMAP(self);

    return TDA_STATUS_OK;
}

tda_Status tda_hmap_move_assign(tda_HMap *self, tda_HMap *other) {
    ASSERT_HMAP(self);
    ASSERT_HMAP(other);
    TDA_EXPECT(tda_layout_eq(self->key, other->key));
    TDA_EXPECT(tda_layout_eq(self->val, other->val));

    if (self == other) {
        return TDA_STATUS_OK;
    }

    // one allocator: swap the buckets, then release what 'self' held from 'other'. The
    // swap also gives 'other' the hasher, equality and ctx of 'self', so restore its own,
    // as the two-allocator path leaves them
    if (self->al == other->al) {
        const tda_Hasher other_hasher = other->hasher;
        const tda_Eq other_eq = other->eq;
        void *other_ctx = other->ctx;

        TDA_SWAP(*self, *other);
        release_buckets(other);

        other->hasher = other_hasher;
        other->eq = other_eq;
        other->ctx = other_ctx;

        ASSERT_HMAP(self);
        ASSERT_HMAP(other);

        return TDA_STATUS_OK;
    }

    // two allocators: build the copy first, so a refusal changes nothing
    tda_HMap *obj;
    const tda_Status st = tda_hmap_copy_with(other, self->al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    TDA_SWAP(*self, *obj);
    tda_hmap_drop(obj);
    release_buckets(other);

    ASSERT_HMAP(self);
    ASSERT_HMAP(other);

    return TDA_STATUS_OK;
}

/* ========== info ========== */

size_t tda_hmap_len(const tda_HMap *self) {
    ASSERT_HMAP(self);

    return self->len;
}

size_t tda_hmap_bucket_count(const tda_HMap *self) {
    ASSERT_HMAP(self);

    return self->bucket_count;
}

size_t tda_hmap_key_size(const tda_HMap *self) {
    ASSERT_HMAP(self);

    return self->key.size;
}

tda_Layout tda_hmap_key_layout(const tda_HMap *self) {
    ASSERT_HMAP(self);

    return self->key;
}

tda_Layout tda_hmap_val_layout(const tda_HMap *self) {
    ASSERT_HMAP(self);

    return self->val;
}

size_t tda_hmap_val_size(const tda_HMap *self) {
    ASSERT_HMAP(self);

    return self->val.size;
}

tda_Al *tda_hmap_al(const tda_HMap *self) {
    ASSERT_HMAP(self);

    return self->al;
}

tda_Hasher tda_hmap_hasher(const tda_HMap *self) {
    ASSERT_HMAP(self);

    return self->hasher;
}

tda_Eq tda_hmap_key_eq(const tda_HMap *self) {
    ASSERT_HMAP(self);

    return self->eq;
}

void *tda_hmap_key_ctx(const tda_HMap *self) {
    ASSERT_HMAP(self);

    return self->ctx;
}

/* ========== compare ========== */

bool tda_hmap_eq(const tda_HMap *a, const tda_HMap *b) {
    ASSERT_HMAP(a);
    ASSERT_HMAP(b);
    TDA_EXPECT(tda_layout_eq(a->key, b->key));
    TDA_EXPECT(tda_layout_eq(a->val, b->val));
    TDA_EXPECT(a->eq == b->eq);

    return eq_impl(a, b, nullptr, nullptr);
}

bool tda_hmap_eq_by(const tda_HMap *a, const tda_HMap *b, tda_Eq val_eq, void *ctx) {
    ASSERT_HMAP(a);
    ASSERT_HMAP(b);
    TDA_EXPECT(tda_layout_eq(a->key, b->key));
    TDA_EXPECT(tda_layout_eq(a->val, b->val));
    TDA_EXPECT(a->eq == b->eq);
    assert(val_eq);

    return eq_impl(a, b, val_eq, ctx);
}

/* ========== lookup ========== */

const void *tda_hmap_get(const tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    const tda_HMapNode *node = find_node(self, key, self->hasher(key, self->ctx));

    return node ? node_val(self, node) : nullptr;
}

void *tda_hmap_get_mut(tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    tda_HMapNode *node = find_node(self, key, self->hasher(key, self->ctx));

    return node ? node_val_mut(self, node) : nullptr;
}

bool tda_hmap_contains(const tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    return find_node(self, key, self->hasher(key, self->ctx)) != nullptr;
}

const tda_HMapNode *tda_hmap_find(const tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    return find_node(self, key, self->hasher(key, self->ctx));
}

tda_HMapNode *tda_hmap_find_mut(tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    return find_node(self, key, self->hasher(key, self->ctx));
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
    assert(val || self->val.size == 0); // a value pointer is needed only when there is a value

    const tda_Hash hash = self->hasher(key, self->ctx);

    tda_HMapNode *found = find_node(self, key, hash);
    if (found) {
        if (self->val.size > 0) {
            // memmove: 'val' may alias the value being overwritten
            memmove(node_val_mut(self, found), val, self->val.size);
        }

        if (out_is_new) {
            *out_is_new = false;
        }

        return TDA_STATUS_OK;
    }

    tda_HMapNode *node;
    const tda_Status st = add_node(self, key, val, hash, &node);
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
    TDA_EXPECT(tda_layout_eq(keys.elem, self->key));
    TDA_EXPECT(tda_layout_eq(vals.elem, self->val));
    TDA_EXPECT(keys.len == vals.len);

    for (size_t i = 0; i < keys.len; ++i) {
        const tda_Status st = tda_hmap_insert(self, tda_span_get(keys, i), tda_span_get(vals, i), nullptr);
        if (TDA_STATUS_IS_ERR(st)) {
            return st;
        }
    }

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
    assert(val_if_absent || self->val.size == 0);
    assert(out_node);

    // one hash and one bucket walk serve both the lookup and the insert
    const tda_Hash hash = self->hasher(key, self->ctx);

    tda_HMapNode *found = find_node(self, key, hash);
    if (found) {
        *out_node = found;
        return TDA_STATUS_OK;
    }

    return add_node(self, key, val_if_absent, hash, out_node);
}

bool tda_hmap_remove(tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    return tda_hmap_take(self, key, nullptr, nullptr);
}

bool tda_hmap_take(tda_HMap *self, const void *key, void *out_key, void *out_val) {
    ASSERT_HMAP(self);
    assert(key);

    if (self->bucket_count == 0) {
        return false;
    }

    tda_HMapNode **link = find_link(self, key, self->hasher(key, self->ctx));
    if (!*link) {
        return false;
    }

    // memmove: 'out_key' may alias 'key'
    if (out_key) {
        memmove(out_key, node_key(self, *link), self->key.size);
    }
    if (out_val) {
        memcpy(out_val, node_val(self, *link), self->val.size);
    }
    unlink_at(self, link);

    ASSERT_HMAP(self);

    return true;
}

void tda_hmap_remove_node(tda_HMap *self, tda_HMapNode *node) {
    ASSERT_HMAP(self);
    assert(node);
    TDA_EXPECT(self->len > 0);

    tda_HMapNode **link = &self->buckets[bucket_of(self, node->hash)];
    while (*link && *link != node) {
        link = &(*link)->next;
    }

    // a node of another map would splice its chain into this one; the walk is already
    // done, so the check is free
    TDA_EXPECT(*link == node);

    unlink_at(self, link);

    ASSERT_HMAP(self);
}

void tda_hmap_retain(tda_HMap *self, tda_EntryPred pred, void *ctx) {
    ASSERT_HMAP(self);
    assert(pred);

    for (size_t b = 0; b < self->bucket_count; ++b) {
        tda_HMapNode **link = &self->buckets[b];
        while (*link) {
            tda_HMapNode *node = *link;
            if (pred(node_key(self, node), node_val(self, node), ctx)) {
                link = &node->next;
                continue;
            }
            unlink_at(self, link);
        }
    }
}

void tda_hmap_clear(tda_HMap *self) {
    ASSERT_HMAP(self);

    clear_nodes(self);

    for (size_t i = 0; i < self->bucket_count; ++i) {
        self->buckets[i] = nullptr;
    }

    self->len = 0;

    ASSERT_HMAP(self);
}

tda_Status tda_hmap_reserve(tda_HMap *self, size_t cap) {
    ASSERT_HMAP(self);

    return reserve_for(self, cap);
}

tda_Status tda_hmap_shrink_to_fit(tda_HMap *self) {
    ASSERT_HMAP(self);

    if (self->len == 0) {
        // empty: the map goes back to owning no buckets
        release_buckets(self);

        ASSERT_HMAP(self);

        return TDA_STATUS_OK;
    }

    const size_t want = buckets_for(self->len);
    if (want == 0 || want >= self->bucket_count) {
        return TDA_STATUS_OK;
    }

    return rehash(self, want);
}

void tda_hmap_swap(tda_HMap *self, tda_HMap *other) {
    ASSERT_HMAP(self);
    ASSERT_HMAP(other);
    TDA_EXPECT(tda_layout_eq(self->key, other->key));
    TDA_EXPECT(tda_layout_eq(self->val, other->val));
    TDA_EXPECT(self->al == other->al);

    if (self == other) {
        return;
    }

    TDA_SWAP(*self, *other);

    ASSERT_HMAP(self);
    ASSERT_HMAP(other);
}

/* ========== print ========== */

void tda_hmap_fprint(const tda_HMap *self, FILE *stream, tda_FPrint key_fprint, tda_FPrint val_fprint) {
    ASSERT_HMAP(self);
    assert(stream);
    assert(key_fprint);
    assert(val_fprint);

    fputc('{', stream);
    bool first = true;
    for (const tda_HMapNode *node = first_from(self, 0); node; node = tda_hmap_node_next(self, node)) {
        if (!first) {
            fputs(", ", stream);
        }
        first = false;
        key_fprint(stream, node_key(self, node));
        fputs(": ", stream);
        val_fprint(stream, node_val(self, node));
    }
    fputs("}\n", stream);
}

void tda_hmap_print(const tda_HMap *self, tda_FPrint key_fprint, tda_FPrint val_fprint) {
    tda_hmap_fprint(self, stdout, key_fprint, val_fprint);
}

/* ========== internals ========== */

static tda_Layout node_layout(const tda_HMap *self) {
    return self->node;
}

static const void *node_key(const tda_HMap *self, const tda_HMapNode *node) {
    return tda_byte_offset(node, 1, self->key_offset);
}

static void *node_val_mut(const tda_HMap *self, tda_HMapNode *node) {
    return tda_byte_offset_mut(node, 1, self->val_offset);
}

static const void *node_val(const tda_HMap *self, const tda_HMapNode *node) {
    return tda_byte_offset(node, 1, self->val_offset);
}

static tda_Status node_new(const tda_HMap *self, const void *key, const void *val, tda_Hash hash, tda_HMapNode **out) {
    tda_HMapNode *node = tda_alloc(self->al, node_layout(self));
    if (!node) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    node->next = nullptr;
    node->hash = hash;
    memcpy(tda_byte_offset_mut(node, 1, self->key_offset), key, self->key.size);
    if (self->val.size > 0) {
        memcpy(node_val_mut(self, node), val, self->val.size);
    }

    *out = node;

    return TDA_STATUS_OK;
}

static void node_drop(const tda_HMap *self, tda_HMapNode *node) {
    tda_dealloc(self->al, node, node_layout(self));
}

static size_t bucket_in(tda_Hash hash, size_t count) {
    assert(count > 0 && (count & (count - 1)) == 0);

    return (size_t) hash & (count - 1);
}

static size_t bucket_of(const tda_HMap *self, tda_Hash hash) {
    assert(self->bucket_count > 0);

    return bucket_in(hash, self->bucket_count);
}

static bool holds_key(const tda_HMap *self, const tda_HMapNode *node, const void *key, tda_Hash hash) {
    // the cheap hash first, so 'eq' runs only on keys whose hashes match
    return node->hash == hash && self->eq(node_key(self, node), key, self->ctx);
}

static tda_HMapNode **find_link(const tda_HMap *self, const void *key, tda_Hash hash) {
    // walk the links, not the nodes: the chain is singly linked, so the link stands in
    // for the previous node when the entry is taken out
    tda_HMapNode **link = &self->buckets[bucket_of(self, hash)];
    while (*link && !holds_key(self, *link, key, hash)) {
        link = &(*link)->next;
    }

    return link;
}

static tda_HMapNode *find_node(const tda_HMap *self, const void *key, tda_Hash hash) {
    if (self->bucket_count == 0) {
        return nullptr;
    }

    // its own walk over nodes rather than *find_link: a lookup is the hot path, and the
    // walk over links measured slower here
    for (tda_HMapNode *node = self->buckets[bucket_of(self, hash)]; node; node = node->next) {
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

static void unlink_at(tda_HMap *self, tda_HMapNode **link) {
    tda_HMapNode *node = *link;
    assert(node);

    *link = node->next;
    node_drop(self, node);
    --self->len;
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
    st = node_new(self, key, val, hash, &node);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    link_node(self->buckets, self->bucket_count, node);
    ++self->len;

    ASSERT_HMAP(self);

    *out = node;

    return TDA_STATUS_OK;
}

static size_t buckets_for(size_t want) {
    size_t n = HMAP_BUCKETS_BASE;
    while (n < want) {
        size_t grown;
        if (ckd_mul(&grown, n, HMAP_GROWTH_FACTOR)) {
            return 0;
        }
        n = grown;
    }

    return n;
}

static tda_Status rehash(tda_HMap *self, size_t new_count) {
    assert(new_count > 0);
    assert((new_count & (new_count - 1)) == 0);

    tda_HMapNode **buckets = TDA_ALLOC_ZEROED(tda_HMapNode *, self->al, new_count);
    if (!buckets) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    for (size_t i = 0; i < self->bucket_count; ++i) {
        tda_HMapNode *node = self->buckets[i];
        while (node) {
            tda_HMapNode *next = node->next;
            link_node(buckets, new_count, node);
            node = next;
        }
    }

    TDA_DEALLOC(tda_HMapNode *, self->al, self->buckets, self->bucket_count);
    self->buckets = buckets;
    self->bucket_count = new_count;

    ASSERT_HMAP(self);

    return TDA_STATUS_OK;
}

static tda_Status reserve_for(tda_HMap *self, size_t cap) {
    if (cap <= self->bucket_count) {
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
    return reserve_for(self, self->len + 1);
}

static tda_HMapNode *first_from(const tda_HMap *self, size_t idx) {
    for (size_t i = idx; i < self->bucket_count; ++i) {
        if (self->buckets[i]) {
            return self->buckets[i];
        }
    }

    return nullptr;
}

static void clear_nodes(tda_HMap *self) {
    for (size_t i = 0; i < self->bucket_count; ++i) {
        tda_HMapNode *node = self->buckets[i];
        while (node) {
            tda_HMapNode *next = node->next;
            node_drop(self, node);
            node = next;
        }
    }
}

static void release_buckets(tda_HMap *self) {
    clear_nodes(self);
    TDA_DEALLOC(tda_HMapNode *, self->al, self->buckets, self->bucket_count);
    self->buckets = nullptr;
    self->bucket_count = 0;
    self->len = 0;
}

static bool eq_impl(const tda_HMap *a, const tda_HMap *b, tda_Eq val_eq, void *ctx) {
    if (a == b) {
        return true;
    }

    if (a->len != b->len) {
        return false;
    }

    // equal lengths plus every key of 'a' in 'b' means containment both ways, since
    // the two share one equality, so one pass is enough. 'b' uses its own hasher
    for (size_t i = 0; i < a->bucket_count; ++i) {
        for (const tda_HMapNode *node = a->buckets[i]; node; node = node->next) {
            const void *key = node_key(a, node);
            const tda_HMapNode *found = find_node(b, key, b->hasher(key, b->ctx));
            if (!found) {
                return false;
            }

            const void *lhs = node_val(a, node);
            const void *rhs = node_val(b, found);
            const bool same = val_eq ? val_eq(lhs, rhs, ctx) : memcmp(lhs, rhs, a->val.size) == 0;
            if (!same) {
                return false;
            }
        }
    }

    return true;
}
