// for @snippet

#include "tda/alloc/default.h"
#include "tda/ds/bitset.h"

#include <stdio.h>

int main() {
    /// [build]
    // the universe is fixed at construction: this set holds a subset of 0 .. 11
    tda_Al *al = tda_al_default();

    tda_BitSet *a = nullptr;
    if (TDA_STATUS_IS_ERR(tda_bitset_new(12, al, &a))) {
        return 1;
    }

    tda_bitset_set(a, 0);
    tda_bitset_set(a, 3);
    tda_bitset_set(a, 7);

    // the printer shows the members, not a row of bits
    tda_bitset_print(a); // {0, 3, 7}
    printf("%zu of %zu\n", tda_bitset_count(a), tda_bitset_len(a)); // 3 of 12
    /// [build]

    /// [bits]
    // one index at a time; an index is its own key, and one outside the universe asserts
    // instead of returning a status
    printf("%d %d\n", tda_bitset_test(a, 3), tda_bitset_test(a, 4)); // 1 0

    tda_bitset_flip(a, 3); // in, so now out
    tda_bitset_set_to(a, 4, true); // unlike flip, asking twice changes nothing
    tda_bitset_print(a); // {0, 4, 7}

    tda_bitset_clear(a, 4);
    /// [bits]

    /// [scan]
    // find_next takes a start, and a start past the universe is a miss, not an assert, so
    // a walk needs no bound of its own. TDA_BITSET_FOR_EACH hides this loop
    TDA_BITSET_FOR_EACH (idx, a) {
        printf("%zu ", idx); // 0 7
    }
    putchar('\n');

    // the same for the non-members, which have no walk of their own
    size_t gap;
    if (tda_bitset_find_next_clear(a, 0, &gap)) {
        printf("first gap at %zu\n", gap); // first gap at 1
    }
    /// [scan]

    /// [set ops]
    // the pairwise ops write into the left operand and only read the right; both must
    // have the same universe
    tda_BitSet *b = nullptr;
    tda_BitSet *both = nullptr;
    int rc = 1;

    if (TDA_STATUS_IS_ERR(tda_bitset_new(12, al, &b))) {
        goto out;
    }
    tda_bitset_set(b, 7);
    tda_bitset_set(b, 9);

    printf("%d %d\n", tda_bitset_intersects(a, b), tda_bitset_is_subset(b, a)); // 1 0

    // union and difference write into the left, so they work on a copy of it
    if (TDA_STATUS_IS_ERR(tda_bitset_copy(a, &both))) {
        goto out;
    }

    tda_bitset_union(both, b);
    tda_bitset_print(both); // {0, 7, 9}

    tda_bitset_difference(both, b);
    tda_bitset_print(both); // {0}

    // the complement is over the universe: the other eleven indices and nothing above
    tda_bitset_flip_all(both);
    printf("%zu\n", tda_bitset_count(both)); // 11

    rc = 0;
out:
    // a null handle is a no-op, so this exit is safe from anywhere above
    tda_bitset_drop(both);
    tda_bitset_drop(b);
    tda_bitset_drop(a);
    return rc;
    /// [set ops]
}
