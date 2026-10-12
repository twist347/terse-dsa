// for @snippet

#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
#include "tda/core/hash.h"
#include "tda/core/print.h"
#include "tda/core/span.h"
#include "tda/core/util.h"
#include "tda/ds/hset.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/// [pred]
static bool is_odd(const void *key, void *ctx) {
    TDA_UNUSED(ctx);

    return *(const int32_t *) key % 2 != 0;
}

/// [pred]

int main() {
    /// [build]
    // the hasher and the equality are fixed here for every key. They must agree: keys
    // equal under 'eq' must hash alike
    tda_Al *al = tda_al_default();

    tda_HSet *s = nullptr;
    if (TDA_STATUS_IS_ERR(TDA_HSET_NEW(int32_t, tda_hash_i32, tda_eq_i32, al, &s))) {
        return 1;
    }

    int rc = 1;
    for (int32_t i = 1; i <= 4; ++i) {
        if (TDA_STATUS_IS_ERR(TDA_HSET_INSERT(int32_t, s, i * 10, nullptr))) {
            goto out;
        }
    }
    printf("%zu keys over %zu buckets\n", tda_hset_len(s), tda_hset_bucket_count(s)); // 4 keys over 8 buckets
    /// [build]

    /// [membership]
    // a key is either in or out; unlike ds/hmap, there is no value to get or set
    printf("%d %d\n", TDA_HSET_CONTAINS(int32_t, s, 20), TDA_HSET_CONTAINS(int32_t, s, 25)); // 1 0

    // insert says whether the key was new, which is how a set reports a duplicate; the
    // existing key is left as it is
    bool is_new;
    if (TDA_STATUS_IS_ERR(TDA_HSET_INSERT(int32_t, s, 20, &is_new))) {
        goto out;
    }
    printf("%d %zu\n", is_new, tda_hset_len(s)); // 0 4

    // remove says whether the key was there and cannot fail, since nothing is allocated.
    // The two calls are separate statements because argument evaluation order is
    // unspecified and each changes what the other sees
    const bool was_there = TDA_HSET_REMOVE(int32_t, s, 20);
    const bool again = TDA_HSET_REMOVE(int32_t, s, 20);
    printf("%d %d\n", was_there, again); // 1 0
    /// [membership]

    /// [walk]
    // the walk needs the set as well as the node, since a chain ends before the buckets
    // do. The order follows the buckets and is unspecified
    int32_t sum = 0;
    TDA_HSET_FOR_EACH (node, s) {
        sum += *TDA_HSET_NODE_KEY_AS(int32_t, node);
    }
    printf("%" PRId32 "\n", sum); // 80

    // two sets are equal when they hold the same keys, regardless of insertion order and
    // bucket count
    tda_HSet *twin = nullptr;
    if (TDA_STATUS_IS_ERR(TDA_HSET_NEW_CAP(int32_t, 64, tda_hash_i32, tda_eq_i32, al, &twin))) {
        goto out;
    }
    for (int32_t i = 4; i >= 1; --i) {
        if (i != 2 && TDA_STATUS_IS_ERR(TDA_HSET_INSERT(int32_t, twin, i * 10, nullptr))) {
            goto out_twin;
        }
    }
    printf(
        "%zu vs %zu buckets, equal: %d\n", tda_hset_bucket_count(s),
        tda_hset_bucket_count(twin), tda_hset_eq(s, twin)
    ); // 8 vs 64 buckets, equal: 1
    /// [walk]

    /// [bulk]
    // from_span collapses repeats, and extend adds a run to an existing set; both insert
    // one key at a time
    const tda_Span keys = TDA_SPAN_OF(int32_t, 1, 2, 2, 3, 3, 3);

    tda_HSet *bulk = nullptr;
    if (TDA_STATUS_IS_ERR(tda_hset_from_span(keys, tda_hash_i32, tda_eq_i32, al, &bulk))) {
        goto out_twin;
    }
    if (TDA_STATUS_IS_ERR(TDA_HSET_EXTEND(int32_t, bulk, 3, 4, 5, 6))) {
        goto out_bulk;
    }
    printf("%zu keys\n", tda_hset_len(bulk)); // 6 keys

    // retain keeps the keys a tda_Pred passes, the same test a span filter takes
    tda_hset_retain(bulk, is_odd, nullptr);
    printf("%zu keys\n", tda_hset_len(bulk)); // 3 keys

    rc = 0;
out_bulk:
    tda_hset_drop(bulk);
out_twin:
    tda_hset_drop(twin);
out:
    // a null handle is a no-op, so this exit is safe from anywhere above
    tda_hset_drop(s);
    return rc;
    /// [bulk]
}
