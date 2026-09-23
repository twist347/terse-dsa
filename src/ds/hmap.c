#include "tda/ds/hmap.h"

#include "tda/core/check.h"
#include "tda/core/util.h"

#include "internal/hmap_impl.h"
#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <string.h>

/* ========== internals ========== */

#define ASSERT_HMAP(m)                                                          \
    (assert(m),                                                                 \
     assert((m)->key_size > 0),                                                 \
     assert((m)->hasher),                                                       \
     assert((m)->eq),                                                           \
     assert((m)->al),                                                           \
     assert(((m)->bucket_count == 0) == ((m)->buckets == nullptr)),             \
     assert((m)->bucket_count == 0                                              \
            || ((m)->bucket_count & ((m)->bucket_count - 1)) == 0),             \
     assert((m)->len == 0 || (m)->bucket_count > 0))

static constexpr size_t HMAP_BUCKETS_BASE = 8;
static constexpr size_t HMAP_GROWTH_FACTOR = 2;

// The key and the value share one flexible array so an entry is one allocation rather
// than two: the key sits at 0 and the value at 'val_offset', which is 'key_size' rounded
// up to the widest alignment the platform has. The hash is kept because it pays twice —
// growing relinks without asking the hasher again, and a lookup rejects on a number
// before it ever calls 'eq'.
struct tda_HMapNode {
    tda_HMapNode *next;
    tda_Hash hash;
    alignas(max_align_t) unsigned char kv[];
};

struct tda_HMap {
    tda_HMapNode **buckets;
    size_t bucket_count;
    size_t len;
    size_t key_size;
    size_t val_size;
    size_t val_offset;
    tda_Hasher hasher;
    tda_Eq eq;
    tda_Al *al;
};

[[nodiscard]]
static size_t node_bytes(const tda_HMap *self);

[[nodiscard]]
static const void *node_key(const tda_HMapNode *node);

[[nodiscard]]
static void *node_val_mut(const tda_HMap *self, tda_HMapNode *node);

[[nodiscard]]
static const void *node_val(const tda_HMap *self, const tda_HMapNode *node);

[[nodiscard]]
static tda_Status node_new(const tda_HMap *self, const void *key, const void *val, tda_Hash hash, tda_HMapNode **out);

static void node_drop(const tda_HMap *self, tda_HMapNode *node);

/// which bucket a hash belongs to. The count is a power of two, so this is a mask and not
/// a division — affordable only because the mixer in core/hash gives every bit avalanche
[[nodiscard]]
static size_t bucket_of(const tda_HMap *self, tda_Hash hash);

[[nodiscard]]
static tda_HMapNode *find_node(const tda_HMap *self, const void *key, tda_Hash hash);

/// builds the entry for a key the caller has already found to be absent and links it into
/// its bucket. Shared by insert and get_or_insert, which differ only in what they do when
/// the key IS there
[[nodiscard]]
static tda_Status add_node(tda_HMap *self, const void *key, const void *val, tda_Hash hash, tda_HMapNode **out);

/// the smallest power of two that is at least 'want', or 0 on overflow
[[nodiscard]]
static size_t round_up_pow2(size_t want);

/// moves every node into a fresh bucket array of 'new_count'. Only the array is
/// allocated: the nodes are relinked where they lie, which is what keeps a borrowed node
/// valid across a growth
[[nodiscard]]
static tda_Status rehash(tda_HMap *self, size_t new_count);

/// room for one more entry, growing the buckets when the load would pass one per bucket
[[nodiscard]]
static tda_Status reserve_one(tda_HMap *self);

/// the first node from bucket 'idx' onward, or null when the rest are empty
[[nodiscard]]
static tda_HMapNode *first_from(const tda_HMap *self, size_t idx);

static void clear_nodes(tda_HMap *self);

/// frees the nodes and the bucket array both, leaving a map that owns nothing: what
/// every other container is left as once its elems have been moved out
static void release_buckets(tda_HMap *self);

/// the walk both compare doors take, with 'val_eq' null standing for the bytes
[[nodiscard]]
static bool eq_impl(const tda_HMap *a, const tda_HMap *b, tda_Eq val_eq);

/* ========== lifetime ========== */

tda_Status tda_hmap_new(size_t key_size, size_t val_size, tda_Hasher hasher, tda_Eq eq, tda_Al *al, tda_HMap **out) {
    return tda_hmap_new_cap(0, key_size, val_size, hasher, eq, al, out);
}

tda_Status tda_hmap_new_cap(
    size_t cap,
    size_t key_size, size_t val_size,
    tda_Hasher hasher, tda_Eq eq,
    tda_Al *al,
    tda_HMap **out
) {
    assert(val_size > 0); // the zero belongs to internal/hmap_impl.h and to ds/hset alone

    return tda_hmap_new_raw_(cap, key_size, val_size, hasher, eq, al, out);
}

tda_Status tda_hmap_new_raw_(
    size_t cap, size_t key_size, size_t val_size,
    tda_Hasher hasher, tda_Eq eq,
    tda_Al *al,
    tda_HMap **out
) {
    assert(key_size > 0);
    assert(hasher);
    assert(eq);
    assert(al);
    assert(out);

    // with no value to follow the key there is nothing to align it to, so a set's node is
    // the header plus the key and not a byte more
    size_t val_offset = key_size;
    if (val_size > 0 && tda_ckd_align_up(&val_offset, key_size, alignof(max_align_t))) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    size_t node_size;
    if (ckd_add(&node_size, val_offset, val_size) || ckd_add(&node_size, node_size, sizeof(tda_HMapNode))) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    tda_HMap *obj = tda_alloc(al, sizeof(tda_HMap));
    if (!obj) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    obj->buckets = nullptr;
    obj->bucket_count = 0;
    obj->len = 0;
    obj->key_size = key_size;
    obj->val_size = val_size;
    obj->val_offset = val_offset;
    obj->hasher = hasher;
    obj->eq = eq;
    obj->al = al;

    if (cap > 0) {
        const tda_Status st = tda_hmap_reserve(obj, cap);
        if (TDA_STATUS_IS_ERR(st)) {
            tda_dealloc(al, obj, sizeof(tda_HMap));
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

    tda_Al *al_copy = self->al;
    clear_nodes(self);
    tda_dealloc(al_copy, self->buckets, self->bucket_count * sizeof(tda_HMapNode *));
    tda_dealloc(al_copy, self, sizeof(tda_HMap));
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
    tda_Status st = tda_hmap_new_raw_(self->len, self->key_size, self->val_size, self->hasher, self->eq, al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    for (const tda_HMapNode *node = first_from(self, 0); node; node = tda_hmap_node_next(self, node)) {
        const void *val = self->val_size > 0 ? node_val(self, node) : nullptr;
        st = tda_hmap_insert(obj, node_key(node), val, nullptr);
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
    TDA_EXPECT(self->key_size == other->key_size);
    TDA_EXPECT(self->val_size == other->val_size);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    // the whole clone is built before anything of 'self' is touched, so a refusal
    // halfway through leaves the target exactly as it was
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
    TDA_EXPECT(self->key_size == other->key_size);
    TDA_EXPECT(self->val_size == other->val_size);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    // one allocator: the buckets are handed over, nodes and all. What 'self' held ends up in 'other' and is released
    // there, through the very allocator that made it
    if (self->al == other->al) {
        TDA_SWAP(*self, *other);
        release_buckets(other);

        ASSERT_HMAP(self);
        ASSERT_HMAP(other);

        return TDA_STATUS_OK;
    }

    // two allocators: the whole copy is built on the target's before anything of it is
    // touched, so a refusal leaves both as they were
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

    return self->key_size;
}

size_t tda_hmap_val_size(const tda_HMap *self) {
    ASSERT_HMAP(self);

    return self->val_size;
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

/* ========== compare ========== */

bool tda_hmap_eq(const tda_HMap *a, const tda_HMap *b) {
    ASSERT_HMAP(a);
    ASSERT_HMAP(b);
    TDA_EXPECT(a->key_size == b->key_size);
    TDA_EXPECT(a->val_size == b->val_size);

    return eq_impl(a, b, nullptr);
}

bool tda_hmap_eq_by(const tda_HMap *a, const tda_HMap *b, tda_Eq val_eq) {
    ASSERT_HMAP(a);
    ASSERT_HMAP(b);
    TDA_EXPECT(a->key_size == b->key_size);
    TDA_EXPECT(a->val_size == b->val_size);
    assert(val_eq);

    return eq_impl(a, b, val_eq);
}

/* ========== lookup ========== */

const void *tda_hmap_get(const tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    const tda_HMapNode *node = find_node(self, key, self->hasher(key));

    return node ? node_val(self, node) : nullptr;
}

void *tda_hmap_get_mut(tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    tda_HMapNode *node = find_node(self, key, self->hasher(key));

    return node ? node_val_mut(self, node) : nullptr;
}

bool tda_hmap_contains(const tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    return find_node(self, key, self->hasher(key)) != nullptr;
}

const tda_HMapNode *tda_hmap_find(const tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    return find_node(self, key, self->hasher(key));
}

tda_HMapNode *tda_hmap_find_mut(tda_HMap *self, const void *key) {
    ASSERT_HMAP(self);
    assert(key);

    return find_node(self, key, self->hasher(key));
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

    if (node->next) {
        return node->next;
    }

    return first_from(self, bucket_of(self, node->hash) + 1);
}

tda_HMapNode *tda_hmap_node_next_mut(tda_HMap *self, tda_HMapNode *node) {
    ASSERT_HMAP(self);
    assert(node);

    if (node->next) {
        return node->next;
    }

    return first_from(self, bucket_of(self, node->hash) + 1);
}

const void *tda_hmap_node_key(const tda_HMapNode *node) {
    assert(node);

    return node_key(node);
}

const void *tda_hmap_node_val(const tda_HMap *self, const tda_HMapNode *node) {
    ASSERT_HMAP(self);
    assert(node);

    return node_val(self, node);
}

void *tda_hmap_node_val_mut(const tda_HMap *self, tda_HMapNode *node) {
    ASSERT_HMAP(self);
    assert(node);

    return node_val_mut(self, node);
}

/* ========== mods ========== */

tda_Status tda_hmap_insert(tda_HMap *self, const void *key, const void *val, bool *out_is_new) {
    ASSERT_HMAP(self);
    assert(key);
    assert(val || self->val_size == 0); // a value pointer is wanted exactly when there is a value

    const tda_Hash hash = self->hasher(key);

    tda_HMapNode *found = find_node(self, key, hash);
    if (found) {
        if (self->val_size > 0) {
            memcpy(node_val_mut(self, found), val, self->val_size);
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

tda_Status tda_hmap_get_or_insert(
    tda_HMap *self,
    const void *key,
    const void *val_if_absent,
    tda_HMapNode **out_node
) {
    ASSERT_HMAP(self);
    assert(key);
    assert(val_if_absent || self->val_size == 0);
    assert(out_node);

    // one hash for both halves of the question, and one walk of the bucket it names
    const tda_Hash hash = self->hasher(key);

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

    if (self->bucket_count == 0) {
        return false;
    }

    const tda_Hash hash = self->hasher(key);

    // walking the links themselves rather than the nodes: the chain is singly linked, and
    // this is what stands in for the previous node
    tda_HMapNode **link = &self->buckets[bucket_of(self, hash)];
    while (*link) {
        if ((*link)->hash == hash && self->eq(node_key(*link), key)) {
            tda_HMapNode *dead = *link;
            *link = dead->next;
            node_drop(self, dead);
            --self->len;

            ASSERT_HMAP(self);

            return true;
        }
        link = &(*link)->next;
    }

    return false;
}

void tda_hmap_remove_node(tda_HMap *self, tda_HMapNode *node) {
    ASSERT_HMAP(self);
    assert(node);
    TDA_EXPECT(self->len > 0);

    tda_HMapNode **link = &self->buckets[bucket_of(self, node->hash)];
    while (*link && *link != node) {
        link = &(*link)->next;
    }

    assert(*link == node); // the node must belong to this map

    *link = node->next;
    node_drop(self, node);
    --self->len;

    ASSERT_HMAP(self);
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

    if (cap <= self->bucket_count) {
        return TDA_STATUS_OK;
    }

    // round_up_pow2 never returns less than the base, so a small 'cap' still gets a
    // sensible bucket array rather than one or two buckets
    const size_t want = round_up_pow2(cap);
    if (want == 0) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    return rehash(self, want);
}

tda_Status tda_hmap_shrink_to_fit(tda_HMap *self) {
    ASSERT_HMAP(self);

    if (self->len == 0) {
        // nothing left to hold: the map goes back to owning no buckets at all
        release_buckets(self);

        ASSERT_HMAP(self);

        return TDA_STATUS_OK;
    }

    const size_t want = round_up_pow2(self->len);
    if (want == 0 || want >= self->bucket_count) {
        return TDA_STATUS_OK;
    }

    return rehash(self, want);
}

void tda_hmap_swap(tda_HMap *self, tda_HMap *other) {
    ASSERT_HMAP(self);
    ASSERT_HMAP(other);
    TDA_EXPECT(self->key_size == other->key_size);
    TDA_EXPECT(self->val_size == other->val_size);
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
        key_fprint(stream, node_key(node));
        fputs(": ", stream);
        val_fprint(stream, node_val(self, node));
    }
    fputs("}\n", stream);
}

void tda_hmap_print(const tda_HMap *self, tda_FPrint key_fprint, tda_FPrint val_fprint) {
    tda_hmap_fprint(self, stdout, key_fprint, val_fprint);
}

/* ========== internals ========== */

static size_t node_bytes(const tda_HMap *self) {
    return sizeof(tda_HMapNode) + self->val_offset + self->val_size;
}

static const void *node_key(const tda_HMapNode *node) {
    return node->kv;
}

static void *node_val_mut(const tda_HMap *self, tda_HMapNode *node) {
    return tda_byte_offset_mut(node->kv, 1, self->val_offset);
}

static const void *node_val(const tda_HMap *self, const tda_HMapNode *node) {
    return tda_byte_offset(node->kv, 1, self->val_offset);
}

static tda_Status node_new(const tda_HMap *self, const void *key, const void *val, tda_Hash hash, tda_HMapNode **out) {
    tda_HMapNode *node = tda_alloc(self->al, node_bytes(self));
    if (!node) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    assert(tda_ptr_is_aligned(node, alignof(max_align_t)));

    node->next = nullptr;
    node->hash = hash;
    memcpy(node->kv, key, self->key_size);
    if (self->val_size > 0) {
        memcpy(node_val_mut(self, node), val, self->val_size);
    }

    *out = node;

    return TDA_STATUS_OK;
}

static void node_drop(const tda_HMap *self, tda_HMapNode *node) {
    tda_dealloc(self->al, node, node_bytes(self));
}

static size_t bucket_of(const tda_HMap *self, tda_Hash hash) {
    assert(self->bucket_count > 0);

    return (size_t) hash & (self->bucket_count - 1);
}

static tda_HMapNode *find_node(const tda_HMap *self, const void *key, tda_Hash hash) {
    if (self->bucket_count == 0) {
        return nullptr;
    }

    for (tda_HMapNode *node = self->buckets[bucket_of(self, hash)]; node; node = node->next) {
        // the hash is compared first because it is a word: 'eq' is only asked about keys
        // that already agree on every mixed bit
        if (node->hash == hash && self->eq(node_key(node), key)) {
            return node;
        }
    }

    return nullptr;
}

static tda_Status add_node(tda_HMap *self, const void *key, const void *val, tda_Hash hash, tda_HMapNode **out) {
    // the room is taken first: growing relinks the buckets, so the one this node belongs
    // to is only known afterwards
    tda_Status st = reserve_one(self);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    tda_HMapNode *node;
    st = node_new(self, key, val, hash, &node);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    const size_t bucket = bucket_of(self, hash);
    node->next = self->buckets[bucket];
    self->buckets[bucket] = node;
    ++self->len;

    ASSERT_HMAP(self);

    *out = node;

    return TDA_STATUS_OK;
}

static size_t round_up_pow2(size_t want) {
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

    tda_HMapNode **buckets = tda_calloc(self->al, new_count, sizeof(tda_HMapNode *));
    if (!buckets) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    for (size_t i = 0; i < self->bucket_count; ++i) {
        tda_HMapNode *node = self->buckets[i];
        while (node) {
            tda_HMapNode *next = node->next;
            const size_t bucket = node->hash & (new_count - 1);
            node->next = buckets[bucket];
            buckets[bucket] = node;
            node = next;
        }
    }

    tda_dealloc(self->al, self->buckets, self->bucket_count * sizeof(tda_HMapNode *));
    self->buckets = buckets;
    self->bucket_count = new_count;

    ASSERT_HMAP(self);

    return TDA_STATUS_OK;
}

static tda_Status reserve_one(tda_HMap *self) {
    if (self->len < self->bucket_count) {
        return TDA_STATUS_OK;
    }

    if (self->bucket_count == 0) {
        return rehash(self, HMAP_BUCKETS_BASE);
    }

    size_t grown;
    if (ckd_mul(&grown, self->bucket_count, HMAP_GROWTH_FACTOR)) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    return rehash(self, grown);
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
    tda_dealloc(self->al, self->buckets, self->bucket_count * sizeof(tda_HMapNode *));
    self->buckets = nullptr;
    self->bucket_count = 0;
    self->len = 0;
}

static bool eq_impl(const tda_HMap *a, const tda_HMap *b, tda_Eq val_eq) {
    if (a == b) {
        return true;
    }

    if (a->len != b->len) {
        return false;
    }

    // equal lengths plus every key of 'a' found in 'b' is containment both ways, so there
    // is no second pass. 'b' answers with its own hasher and equality: the keys are being
    // looked up in it
    for (size_t i = 0; i < a->bucket_count; ++i) {
        for (const tda_HMapNode *node = a->buckets[i]; node; node = node->next) {
            const void *key = node_key(node);
            const tda_HMapNode *found = find_node(b, key, b->hasher(key));
            if (!found) {
                return false;
            }

            const void *lhs = node_val(a, node);
            const void *rhs = node_val(b, found);
            const bool same = val_eq ? val_eq(lhs, rhs) : memcmp(lhs, rhs, a->val_size) == 0;
            if (!same) {
                return false;
            }
        }
    }

    return true;
}
