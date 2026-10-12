#include "tda/ds/hset.h"

#include "tda/core/check.h"
#include "tda/core/util.h"

#include "internal/hmap_impl.h"

#include <assert.h>

/* ========== internals ========== */

#define ASSERT_HSET(s) \
    (assert(s),        \
     assert((s)->map))

// two sets an op can combine: the same key size and the same key equality
#define EXPECT_SAME_KEYS(a, b)                                              \
    (TDA_EXPECT(tda_hmap_key_size((a)->map) == tda_hmap_key_size((b)->map)), \
     TDA_EXPECT(tda_hmap_key_eq((a)->map) == tda_hmap_key_eq((b)->map)))

// A set wraps a map: chaining, growth and the allocator are the map's, and the set only
// limits which operations are forwarded.
//
// The map is built through internal/hmap_impl.h with val_size 0, so nothing follows the
// key in a node and nothing pads it.
struct tda_HSet {
    tda_HMap *map;
};

/// takes ownership of 'map'; on failure it is dropped
[[nodiscard]]
static tda_Status wrap(tda_HMap *map, tda_HSet **out);

/// a tda_Pred on keys, carried through tda_hmap_retain
typedef struct {
    tda_Pred pred;
    void *ctx;
} KeyPred;

/// tda_EntryPred over a KeyPred
[[nodiscard]]
static bool key_passes(const void *key, const void *val, void *ctx);

/// tda_EntryPred: whether the map 'ctx' holds 'key'
[[nodiscard]]
static bool held_by(const void *key, const void *val, void *ctx);

/// tda_EntryPred: whether the map 'ctx' lacks 'key'
[[nodiscard]]
static bool not_held_by(const void *key, const void *val, void *ctx);

/* ========== lifetime ========== */

tda_Status tda_hset_new(size_t key_size, tda_Hasher hasher, tda_Eq eq, tda_Al *al, tda_HSet **out) {
    return tda_hset_new_cap(0, key_size, hasher, eq, al, out);
}

tda_Status tda_hset_new_cap(size_t cap, size_t key_size, tda_Hasher hasher, tda_Eq eq, tda_Al *al, tda_HSet **out) {
    assert(key_size > 0);
    assert(hasher);
    assert(eq);
    assert(al);
    assert(out);

    tda_HMap *map;
    const tda_Status st = tda_hmap_new_raw_(cap, key_size, 0, hasher, eq, al, &map);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    return wrap(map, out);
}

tda_Status tda_hset_from_span(tda_Span s, tda_Hasher hasher, tda_Eq eq, tda_Al *al, tda_HSet **out) {
    TDA_SPAN_ASSERT(s);
    assert(out);

    tda_HSet *obj;
    tda_Status st = tda_hset_new_cap(s.len, s.elem_size, hasher, eq, al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    st = tda_hset_extend(obj, s);
    if (TDA_STATUS_IS_ERR(st)) {
        tda_hset_drop(obj);
        return st;
    }

    *out = obj;
    return TDA_STATUS_OK;
}

void tda_hset_drop(tda_HSet *self) {
    if (!self) {
        return;
    }

    ASSERT_HSET(self);

    tda_Al *al_copy = tda_hmap_al(self->map);
    tda_hmap_drop(self->map);
    tda_dealloc(al_copy, self, sizeof(tda_HSet));
}

/* ========== copy ========== */

tda_Status tda_hset_copy(const tda_HSet *self, tda_HSet **out) {
    ASSERT_HSET(self);

    return tda_hset_copy_with(self, tda_hmap_al(self->map), out);
}

tda_Status tda_hset_copy_with(const tda_HSet *self, tda_Al *al, tda_HSet **out) {
    ASSERT_HSET(self);
    assert(al);
    assert(out);

    tda_HMap *map;
    const tda_Status st = tda_hmap_copy_with(self->map, al, &map);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    return wrap(map, out);
}

tda_Status tda_hset_copy_assign(tda_HSet *self, const tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);

    // the map handles self assignment; a guard here would be untestable
    return tda_hmap_copy_assign(self->map, other->map);
}

tda_Status tda_hset_move_assign(tda_HSet *self, tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);

    // the map handles self assignment; a guard here would be untestable
    return tda_hmap_move_assign(self->map, other->map);
}

/* ========== info ========== */

size_t tda_hset_len(const tda_HSet *self) {
    ASSERT_HSET(self);

    return tda_hmap_len(self->map);
}

size_t tda_hset_bucket_count(const tda_HSet *self) {
    ASSERT_HSET(self);

    return tda_hmap_bucket_count(self->map);
}

size_t tda_hset_key_size(const tda_HSet *self) {
    ASSERT_HSET(self);

    return tda_hmap_key_size(self->map);
}

tda_Al *tda_hset_al(const tda_HSet *self) {
    ASSERT_HSET(self);

    return tda_hmap_al(self->map);
}

tda_Hasher tda_hset_hasher(const tda_HSet *self) {
    ASSERT_HSET(self);

    return tda_hmap_hasher(self->map);
}

tda_Eq tda_hset_key_eq(const tda_HSet *self) {
    ASSERT_HSET(self);

    return tda_hmap_key_eq(self->map);
}

/* ========== compare ========== */

bool tda_hset_eq(const tda_HSet *a, const tda_HSet *b) {
    ASSERT_HSET(a);
    ASSERT_HSET(b);
    TDA_EXPECT(tda_hmap_key_size(a->map) == tda_hmap_key_size(b->map));
    TDA_EXPECT(tda_hmap_key_eq(a->map) == tda_hmap_key_eq(b->map));

    if (a == b) {
        return true;
    }

    if (tda_hmap_len(a->map) != tda_hmap_len(b->map)) {
        return false;
    }

    // not tda_hmap_eq: the map has val_size 0, so there are no values to compare and
    // node_val would point one past the key
    TDA_HMAP_FOR_EACH(node, a->map) {
        if (!tda_hmap_contains(b->map, tda_hmap_node_key(node))) {
            return false;
        }
    }

    return true;
}

/* ========== lookup ========== */

bool tda_hset_contains(const tda_HSet *self, const void *key) {
    ASSERT_HSET(self);
    assert(key);

    return tda_hmap_contains(self->map, key);
}

const tda_HSetNode *tda_hset_find(const tda_HSet *self, const void *key) {
    ASSERT_HSET(self);
    assert(key);

    return tda_hmap_find(self->map, key);
}

tda_HSetNode *tda_hset_find_mut(tda_HSet *self, const void *key) {
    ASSERT_HSET(self);
    assert(key);

    return tda_hmap_find_mut(self->map, key);
}

/* ========== nodes ========== */

const tda_HSetNode *tda_hset_first_node(const tda_HSet *self) {
    ASSERT_HSET(self);

    return tda_hmap_first_node(self->map);
}

tda_HSetNode *tda_hset_first_node_mut(tda_HSet *self) {
    ASSERT_HSET(self);

    return tda_hmap_first_node_mut(self->map);
}

const tda_HSetNode *tda_hset_node_next(const tda_HSet *self, const tda_HSetNode *node) {
    ASSERT_HSET(self);
    assert(node);

    return tda_hmap_node_next(self->map, node);
}

tda_HSetNode *tda_hset_node_next_mut(tda_HSet *self, tda_HSetNode *node) {
    ASSERT_HSET(self);
    assert(node);

    return tda_hmap_node_next_mut(self->map, node);
}

const void *tda_hset_node_key(const tda_HSetNode *node) {
    assert(node);

    return tda_hmap_node_key(node);
}

/* ========== mods ========== */

tda_Status tda_hset_insert(tda_HSet *self, const void *key, bool *out_is_new) {
    ASSERT_HSET(self);
    assert(key);

    // no value: the map's val_size is 0
    return tda_hmap_insert(self->map, key, nullptr, out_is_new);
}

tda_Status tda_hset_extend(tda_HSet *self, tda_Span src) {
    ASSERT_HSET(self);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(src.elem_size == tda_hmap_key_size(self->map));

    for (size_t i = 0; i < src.len; ++i) {
        const tda_Status st = tda_hmap_insert(self->map, tda_span_get(src, i), nullptr, nullptr);
        if (TDA_STATUS_IS_ERR(st)) {
            return st;
        }
    }

    return TDA_STATUS_OK;
}

bool tda_hset_remove(tda_HSet *self, const void *key) {
    ASSERT_HSET(self);
    assert(key);

    return tda_hmap_remove(self->map, key);
}

void tda_hset_remove_node(tda_HSet *self, tda_HSetNode *node) {
    ASSERT_HSET(self);
    assert(node);

    tda_hmap_remove_node(self->map, node);
}

bool tda_hset_take(tda_HSet *self, const void *key, void *out_key) {
    ASSERT_HSET(self);
    assert(key);

    // the map has no values, so only the key is copied out
    return tda_hmap_take(self->map, key, out_key, nullptr);
}

void tda_hset_retain(tda_HSet *self, tda_Pred pred, void *ctx) {
    ASSERT_HSET(self);
    assert(pred);

    tda_hmap_retain(self->map, key_passes, &(KeyPred){pred, ctx});
}

void tda_hset_clear(tda_HSet *self) {
    ASSERT_HSET(self);

    tda_hmap_clear(self->map);
}

tda_Status tda_hset_reserve(tda_HSet *self, size_t cap) {
    ASSERT_HSET(self);

    return tda_hmap_reserve(self->map, cap);
}

tda_Status tda_hset_shrink_to_fit(tda_HSet *self) {
    ASSERT_HSET(self);

    return tda_hmap_shrink_to_fit(self->map);
}

void tda_hset_swap(tda_HSet *self, tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);

    tda_hmap_swap(self->map, other->map);
}

/* ========== set ops ========== */

tda_Status tda_hset_union(tda_HSet *self, const tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);
    EXPECT_SAME_KEYS(self, other);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    TDA_HMAP_FOR_EACH(node, other->map) {
        const tda_Status st = tda_hmap_insert(self->map, tda_hmap_node_key(node), nullptr, nullptr);
        if (TDA_STATUS_IS_ERR(st)) {
            return st;
        }
    }

    return TDA_STATUS_OK;
}

void tda_hset_intersect(tda_HSet *self, const tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);
    EXPECT_SAME_KEYS(self, other);

    if (self == other) {
        return;
    }

    tda_hmap_retain(self->map, held_by, other->map);
}

void tda_hset_difference(tda_HSet *self, const tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);
    EXPECT_SAME_KEYS(self, other);

    if (self == other) {
        tda_hmap_clear(self->map);
        return;
    }

    // either side can drive: remove each key of 'other' from 'self', or check each key of
    // 'self' against 'other'. Both give the same set, so walk the shorter one
    if (tda_hmap_len(other->map) < tda_hmap_len(self->map)) {
        TDA_HMAP_FOR_EACH(node, other->map) {
            tda_hmap_remove(self->map, tda_hmap_node_key(node));
        }
        return;
    }

    tda_hmap_retain(self->map, not_held_by, other->map);
}

tda_Status tda_hset_symmetric_difference(tda_HSet *self, const tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);
    EXPECT_SAME_KEYS(self, other);

    if (self == other) {
        tda_hmap_clear(self->map);
        return TDA_STATUS_OK;
    }

    // each key of 'other' flips: removed from 'self' if there, inserted if not. The keys
    // of 'other' are distinct, so none flips twice
    TDA_HMAP_FOR_EACH(node, other->map) {
        const void *key = tda_hmap_node_key(node);
        if (tda_hmap_remove(self->map, key)) {
            continue;
        }

        const tda_Status st = tda_hmap_insert(self->map, key, nullptr, nullptr);
        if (TDA_STATUS_IS_ERR(st)) {
            return st;
        }
    }

    return TDA_STATUS_OK;
}

bool tda_hset_is_subset(const tda_HSet *self, const tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);
    EXPECT_SAME_KEYS(self, other);

    if (self == other) {
        return true;
    }

    // a larger set cannot be contained in a smaller one
    if (tda_hmap_len(self->map) > tda_hmap_len(other->map)) {
        return false;
    }

    TDA_HMAP_FOR_EACH(node, self->map) {
        if (!tda_hmap_contains(other->map, tda_hmap_node_key(node))) {
            return false;
        }
    }

    return true;
}

bool tda_hset_intersects(const tda_HSet *self, const tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);
    EXPECT_SAME_KEYS(self, other);

    // intersection is symmetric, so walk the shorter set and look up in the longer
    const tda_HSet *walked = self;
    const tda_HSet *asked = other;
    if (tda_hmap_len(other->map) < tda_hmap_len(self->map)) {
        walked = other;
        asked = self;
    }

    TDA_HMAP_FOR_EACH(node, walked->map) {
        if (tda_hmap_contains(asked->map, tda_hmap_node_key(node))) {
            return true;
        }
    }

    return false;
}

/* ========== print ========== */

void tda_hset_fprint(const tda_HSet *self, FILE *stream, tda_FPrint fprint) {
    ASSERT_HSET(self);
    assert(stream);
    assert(fprint);

    fputc('{', stream);
    bool first = true;
    TDA_HSET_FOR_EACH(node, self) {
        if (!first) {
            fputs(", ", stream);
        }
        first = false;
        fprint(stream, tda_hset_node_key(node));
    }
    fputs("}\n", stream);
}

void tda_hset_print(const tda_HSet *self, tda_FPrint fprint) {
    tda_hset_fprint(self, stdout, fprint);
}

/* ========== internals ========== */

static tda_Status wrap(tda_HMap *map, tda_HSet **out) {
    assert(map);
    assert(out);

    tda_HSet *obj = tda_alloc(tda_hmap_al(map), sizeof(tda_HSet));
    if (!obj) {
        tda_hmap_drop(map);
        return TDA_STATUS_ERR_NO_MEM;
    }

    obj->map = map;

    *out = obj;

    return TDA_STATUS_OK;
}

static bool key_passes(const void *key, const void *val, void *ctx) {
    TDA_UNUSED(val);
    const KeyPred *key_pred = ctx;

    return key_pred->pred(key, key_pred->ctx);
}

static bool held_by(const void *key, const void *val, void *ctx) {
    TDA_UNUSED(val);

    return tda_hmap_contains(ctx, key);
}

static bool not_held_by(const void *key, const void *val, void *ctx) {
    TDA_UNUSED(val);

    return !tda_hmap_contains(ctx, key);
}
