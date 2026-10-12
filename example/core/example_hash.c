// for @snippet

#include "tda/alloc/default.h"
#include "tda/core/hash.h"
#include "tda/ds/hset.h"

#include <stdint.h>
#include <stdio.h>

/// [struct]
// a hasher and an equality go together under one rule: what the equality calls equal
// must hash alike. Build both from the fields, never the bytes: tda_hash_bytes would
// read the padding between them
typedef struct {
    int32_t x;
    int32_t y;
} Point;

static tda_Hash hash_point(const void *v) {
    const Point *p = v;

    return tda_hash_combine(tda_hash_i32(&p->x), tda_hash_i32(&p->y));
}

static bool eq_point(const void *lhs, const void *rhs) {
    const Point *a = lhs;
    const Point *b = rhs;

    return a->x == b->x && a->y == b->y;
}
/// [struct]

int main() {
    /// [use]
    tda_HSet *seen = nullptr;
    if (TDA_STATUS_IS_ERR(TDA_HSET_NEW(Point, hash_point, eq_point, tda_al_default(), &seen))) {
        return 1;
    }

    bool is_new = false;
    tda_Status st = tda_hset_insert(seen, &(Point){ 1, 2 }, &is_new);
    if (TDA_STATUS_IS_ERR(st)) {
        goto out;
    }
    printf("%s\n", is_new ? "new" : "seen"); // new

    // an equal point hashes alike, so the set finds it and does not store it twice
    st = tda_hset_insert(seen, &(Point){ 1, 2 }, &is_new);
    if (TDA_STATUS_IS_ERR(st)) {
        goto out;
    }
    printf("%s, %zu in all\n", is_new ? "new" : "seen", tda_hset_len(seen)); // seen, 1 in all

out:
    tda_hset_drop(seen);
    return TDA_STATUS_IS_ERR(st) ? 1 : 0;
    /// [use]
}
