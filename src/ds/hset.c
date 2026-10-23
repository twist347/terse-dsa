#include "tda/ds/hset.h"

#include "tda/core/check.h"
#include "tda/core/util.h"

#include "internal/hmap_impl.h"

#include <assert.h>

/* ========== internals ========== */

// the map asserts the rest of its invariants on every call
#define ASSERT_HSET(s) \
    (assert(s),        \
     assert((s)->map_.al_))

// two sets an op can combine: the same key layout and the same key equality. Their ctx
// may differ, as two seeds do: each set is searched with its own hasher and ctx
#define EXPECT_SAME_KEYS(a, b)                                                                    \
    (TDA_EXPECT(tda_layout_eq(tda_hmap_key_layout(&(a)->map_), tda_hmap_key_layout(&(b)->map_))), \
     TDA_EXPECT(tda_hmap_key_eq(&(a)->map_) == tda_hmap_key_eq(&(b)->map_)))

// A set wraps a map: chaining, growth and the allocator are the map's, and the set only
// limits which operations are forwarded.
//
// The map is built through internal/hmap_impl.h with a value of size 0, so nothing follows the
// key in a node and nothing pads it.
/// a tda_Pred on keys, carried through tda_hmap_retain
typedef struct {
    tda_Pred pred;
    void *ctx;
} KeyPred;

/// tda_EntryPred over a KeyPred
[[nodiscard]]
static bool key_passes(const void *key, const void *val, void *ctx);

/// tda_EntryPred: 'ctx' points to a pointer to a map; whether that map holds 'key'
[[nodiscard]]
static bool held_by(const void *key, const void *val, void *ctx);

/// tda_EntryPred: 'ctx' points to a pointer to a map; whether that map lacks 'key'
[[nodiscard]]
static bool not_held_by(const void *key, const void *val, void *ctx);

/* ========== lifetime ========== */

void tda_hset_init(tda_Layout key, tda_Hasher hasher, tda_Eq eq, void *ctx, tda_Al *al, tda_HSet *out) {
    // with no room asked for nothing is allocated, so this cannot fail
    [[maybe_unused]] const tda_Status st = tda_hset_init_cap(0, key, hasher, eq, ctx, al, out);
    assert(TDA_STATUS_IS_OK(st));
}

tda_Status tda_hset_init_cap(
    size_t cap,
    tda_Layout key,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al *al,
    tda_HSet *out
) {
    assert(tda_layout_is_type(key));
    assert(hasher);
    assert(eq);
    assert(al);
    assert(out);

    // no value, so nothing to lay out after the key
    return tda_hmap_init_raw_(cap, key, (tda_Layout){.size = 0, .align = 1}, hasher, eq, ctx, al, &out->map_);
}

tda_Status tda_hset_from_span(
    tda_Span s,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al *al,
    tda_HSet *out
) {
    TDA_SPAN_ASSERT(s);
    assert(out);

    tda_HSet obj;
    tda_Status st = tda_hset_init_cap(s.len, s.elem, hasher, eq, ctx, al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    st = tda_hset_extend(&obj, s);
    if (TDA_STATUS_IS_ERR(st)) {
        tda_hset_deinit(&obj);
        return st;
    }

    *out = obj;

    return TDA_STATUS_OK;
}

void tda_hset_deinit(tda_HSet *self) {
    assert(self);

    tda_hmap_deinit(&self->map_);
}

/* ========== copy ========== */

tda_Status tda_hset_copy(const tda_HSet *self, tda_HSet *out) {
    ASSERT_HSET(self);

    return tda_hset_copy_with(self, tda_hmap_al(&self->map_), out);
}

tda_Status tda_hset_copy_with(const tda_HSet *self, tda_Al *al, tda_HSet *out) {
    ASSERT_HSET(self);
    assert(al);
    assert(out);

    return tda_hmap_copy_with(&self->map_, al, &out->map_);
}

tda_Status tda_hset_copy_assign(tda_HSet *self, const tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);

    // the map handles self assignment; a guard here would be untestable
    return tda_hmap_copy_assign(&self->map_, &other->map_);
}

void tda_hset_move_assign(tda_HSet *self, tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);

    // the map handles self assignment; a guard here would be untestable
    tda_hmap_move_assign(&self->map_, &other->map_);
}

/* ========== compare ========== */

bool tda_hset_eq(const tda_HSet *a, const tda_HSet *b) {
    ASSERT_HSET(a);
    ASSERT_HSET(b);
    TDA_EXPECT(tda_layout_eq(tda_hmap_key_layout(&a->map_), tda_hmap_key_layout(&b->map_)));
    TDA_EXPECT(tda_hmap_key_eq(&a->map_) == tda_hmap_key_eq(&b->map_));

    if (a == b) {
        return true;
    }

    if (tda_hmap_len(&a->map_) != tda_hmap_len(&b->map_)) {
        return false;
    }

    // not tda_hmap_eq: the map's value has size 0, so there are no values to compare
    TDA_HMAP_FOR_EACH(node, &a->map_) {
        if (!tda_hmap_contains(&b->map_, tda_hmap_node_key(&a->map_, node))) {
            return false;
        }
    }

    return true;
}

/* ========== lookup ========== */

bool tda_hset_contains(const tda_HSet *self, const void *key) {
    ASSERT_HSET(self);
    assert(key);

    return tda_hmap_contains(&self->map_, key);
}

const tda_HSetNode *tda_hset_find(const tda_HSet *self, const void *key) {
    ASSERT_HSET(self);
    assert(key);

    return tda_hmap_find(&self->map_, key);
}

tda_HSetNode *tda_hset_find_mut(tda_HSet *self, const void *key) {
    ASSERT_HSET(self);
    assert(key);

    return tda_hmap_find_mut(&self->map_, key);
}

/* ========== nodes ========== */

const tda_HSetNode *tda_hset_first_node(const tda_HSet *self) {
    ASSERT_HSET(self);

    return tda_hmap_first_node(&self->map_);
}

tda_HSetNode *tda_hset_first_node_mut(tda_HSet *self) {
    ASSERT_HSET(self);

    return tda_hmap_first_node_mut(&self->map_);
}

const tda_HSetNode *tda_hset_node_next(const tda_HSet *self, const tda_HSetNode *node) {
    ASSERT_HSET(self);
    assert(node);

    return tda_hmap_node_next(&self->map_, node);
}

tda_HSetNode *tda_hset_node_next_mut(tda_HSet *self, tda_HSetNode *node) {
    ASSERT_HSET(self);
    assert(node);

    return tda_hmap_node_next_mut(&self->map_, node);
}

const void *tda_hset_node_key(const tda_HSet *self, const tda_HSetNode *node) {
    ASSERT_HSET(self);
    assert(node);

    return tda_hmap_node_key(&self->map_, node);
}

/* ========== mods ========== */

tda_Status tda_hset_insert(tda_HSet *self, const void *key, bool *out_is_new) {
    ASSERT_HSET(self);
    assert(key);

    // no value: the map's value has size 0
    return tda_hmap_insert(&self->map_, key, nullptr, out_is_new);
}

tda_Status tda_hset_extend(tda_HSet *self, tda_Span src) {
    ASSERT_HSET(self);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(tda_layout_eq(src.elem, tda_hmap_key_layout(&self->map_)));

    for (size_t i = 0; i < src.len; ++i) {
        const tda_Status st = tda_hmap_insert(&self->map_, tda_span_get(src, i), nullptr, nullptr);
        if (TDA_STATUS_IS_ERR(st)) {
            return st;
        }
    }

    return TDA_STATUS_OK;
}

bool tda_hset_remove(tda_HSet *self, const void *key) {
    ASSERT_HSET(self);
    assert(key);

    return tda_hmap_remove(&self->map_, key);
}

void tda_hset_remove_node(tda_HSet *self, tda_HSetNode *node) {
    ASSERT_HSET(self);
    assert(node);

    tda_hmap_remove_node(&self->map_, node);
}

bool tda_hset_take(tda_HSet *self, const void *key, void *out_key) {
    ASSERT_HSET(self);
    assert(key);

    // the map has no values, so only the key is copied out
    return tda_hmap_take(&self->map_, key, out_key, nullptr);
}

void tda_hset_retain(tda_HSet *self, tda_Pred pred, void *ctx) {
    ASSERT_HSET(self);
    assert(pred);

    tda_hmap_retain(&self->map_, key_passes, &(KeyPred){pred, ctx});
}

void tda_hset_clear(tda_HSet *self) {
    ASSERT_HSET(self);

    tda_hmap_clear(&self->map_);
}

tda_Status tda_hset_reserve(tda_HSet *self, size_t new_cap) {
    ASSERT_HSET(self);

    return tda_hmap_reserve(&self->map_, new_cap);
}

tda_Status tda_hset_shrink_to_fit(tda_HSet *self) {
    ASSERT_HSET(self);

    return tda_hmap_shrink_to_fit(&self->map_);
}

void tda_hset_swap(tda_HSet *self, tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);

    tda_hmap_swap(&self->map_, &other->map_);
}

/* ========== set ops ========== */

tda_Status tda_hset_union(tda_HSet *self, const tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);
    EXPECT_SAME_KEYS(self, other);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    TDA_HMAP_FOR_EACH(node, &other->map_) {
        const tda_Status st = tda_hmap_insert(&self->map_, tda_hmap_node_key(&other->map_, node), nullptr, nullptr);
        if (TDA_STATUS_IS_ERR(st)) {
            return st;
        }
    }

    return TDA_STATUS_OK;
}

void tda_hset_intersection(tda_HSet *self, const tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);
    EXPECT_SAME_KEYS(self, other);

    if (self == other) {
        return;
    }

    // the predicate's ctx is not const, so it gets the address of a pointer to the map
    const tda_HMap *map = &other->map_;
    tda_hmap_retain(&self->map_, held_by, &map);
}

void tda_hset_difference(tda_HSet *self, const tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);
    EXPECT_SAME_KEYS(self, other);

    if (self == other) {
        tda_hmap_clear(&self->map_);
        return;
    }

    // either side can drive: remove each key of 'other' from 'self', or check each key of
    // 'self' against 'other'. Both give the same set, so walk the shorter one
    if (tda_hmap_len(&other->map_) < tda_hmap_len(&self->map_)) {
        TDA_HMAP_FOR_EACH(node, &other->map_) {
            tda_hmap_remove(&self->map_, tda_hmap_node_key(&other->map_, node));
        }
        return;
    }

    const tda_HMap *map = &other->map_;
    tda_hmap_retain(&self->map_, not_held_by, &map);
}

tda_Status tda_hset_symmetric_difference(tda_HSet *self, const tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);
    EXPECT_SAME_KEYS(self, other);

    if (self == other) {
        tda_hmap_clear(&self->map_);
        return TDA_STATUS_OK;
    }

    // each key of 'other' flips: removed from 'self' if there, inserted if not. The keys
    // of 'other' are distinct, so none flips twice
    TDA_HMAP_FOR_EACH(node, &other->map_) {
        const void *key = tda_hmap_node_key(&other->map_, node);
        if (tda_hmap_remove(&self->map_, key)) {
            continue;
        }

        const tda_Status st = tda_hmap_insert(&self->map_, key, nullptr, nullptr);
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
    if (tda_hmap_len(&self->map_) > tda_hmap_len(&other->map_)) {
        return false;
    }

    TDA_HMAP_FOR_EACH(node, &self->map_) {
        if (!tda_hmap_contains(&other->map_, tda_hmap_node_key(&self->map_, node))) {
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
    if (tda_hmap_len(&other->map_) < tda_hmap_len(&self->map_)) {
        walked = other;
        asked = self;
    }

    TDA_HMAP_FOR_EACH(node, &walked->map_) {
        if (tda_hmap_contains(&asked->map_, tda_hmap_node_key(&walked->map_, node))) {
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
        fprint(stream, tda_hset_node_key(self, node));
    }
    fputs("}\n", stream);
}

void tda_hset_print(const tda_HSet *self, tda_FPrint fprint) {
    tda_hset_fprint(self, stdout, fprint);
}

/* ========== internals ========== */

static bool key_passes(const void *key, const void *val, void *ctx) {
    TDA_UNUSED(val);
    const KeyPred *key_pred = ctx;

    return key_pred->pred(key, key_pred->ctx);
}

static bool held_by(const void *key, const void *val, void *ctx) {
    TDA_UNUSED(val);

    return tda_hmap_contains(*(const tda_HMap **) ctx, key);
}

static bool not_held_by(const void *key, const void *val, void *ctx) {
    TDA_UNUSED(val);

    return !tda_hmap_contains(*(const tda_HMap **) ctx, key);
}
