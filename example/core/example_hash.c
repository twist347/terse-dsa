// for @snippet

#include "tda/alloc/default.h"
#include "tda/core/hash.h"
#include "tda/ds/hset.h"

#include <stdint.h>
#include <stdio.h>

/// [struct]
// a hasher and an equality go together under one rule: what the equality calls equal
// must hash alike. Build both from the fields. Point would survive tda_hash_bytes, but
// a struct with padding or a float field would not: its bytes can differ where its
// values agree
typedef struct {
    int32_t x;
    int32_t y;
} Point;

static tda_Hash hash_point(const void *key, void *) {
    const Point *p = key;

    return tda_hash_combine(tda_hash_i32(&p->x, nullptr), tda_hash_i32(&p->y, nullptr));
}

static bool eq_point(const void *lhs, const void *rhs, void *) {
    const Point *a = lhs;
    const Point *b = rhs;

    return a->x == b->x && a->y == b->y;
}
/// [struct]

int main() {
    /// [use]
    tda_HSet seen;
    TDA_HSET_INIT(Point, hash_point, eq_point, nullptr, tda_al_default(), &seen);

    bool is_new = false;
    tda_Status st = tda_hset_insert(&seen, &(Point){ 1, 2 }, &is_new);
    if (TDA_STATUS_IS_ERR(st)) {
        goto out;
    }
    printf("%s\n", is_new ? "new" : "seen"); // new

    // an equal point hashes alike, so the set finds it and does not store it twice
    st = tda_hset_insert(&seen, &(Point){ 1, 2 }, &is_new);
    if (TDA_STATUS_IS_ERR(st)) {
        goto out;
    }
    printf("%s, %zu in all\n", is_new ? "new" : "seen", tda_hset_len(&seen)); // seen, 1 in all

out:
    tda_hset_deinit(&seen);

    return TDA_STATUS_IS_ERR(st) ? 1 : 0;
    /// [use]
}
