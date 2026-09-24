#include "tda/ds/hset.h"

#include "tda/core/check.h"

#include "internal/hmap_impl.h"

#include <assert.h>

/* ========== internals ========== */

#define ASSERT_HSET(s) \
    (assert(s),        \
     assert((s)->map))

// A set is a map seen through a smaller keyhole: the chaining, the growth and the allocator
// stay the map's, and what this type adds is the operations it does NOT forward.
//
// The map is built through internal/hmap_impl.h with val_size 0, which is the whole point of
// that door: nothing follows the key in a node, so nothing pads it either.
struct tda_HSet {
    tda_HMap *map;
};

/// takes ownership of 'map' either way: on failure it is dropped, not handed back
[[nodiscard]]
static tda_Status wrap(tda_HMap *map, tda_HSet **out);

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

    // self assignment is left to the map, which already returns early on it: a guard
    // repeated here would be a branch no test could tell from its absence
    return tda_hmap_copy_assign(self->map, other->map);
}

tda_Status tda_hset_move_assign(tda_HSet *self, tda_HSet *other) {
    ASSERT_HSET(self);
    ASSERT_HSET(other);

    // self assignment is left to the map, which already returns early on it: a guard
    // repeated here would be a branch no test could tell from its absence
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

    // not tda_hmap_eq: the map under a set carries val_size 0, so it has no value side to
    // compare and node_val would point one past the key
    for (const tda_HMapNode *node = tda_hmap_first_node(a->map); node;
         node = tda_hmap_node_next(a->map, node)) {
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

    // no value to hand over: the map's val_size is 0, so it asks for none
    return tda_hmap_insert(self->map, key, nullptr, out_is_new);
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

/* ========== print ========== */

void tda_hset_fprint(const tda_HSet *self, FILE *stream, tda_FPrint fprint) {
    ASSERT_HSET(self);
    assert(stream);
    assert(fprint);

    fputc('{', stream);
    bool first = true;
    for (const tda_HSetNode *node = tda_hset_first_node(self); node; node = tda_hset_node_next(self, node)) {
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
