#include "tda/ds/bitset.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/core/util.h"

#include "support/arena.h"
#include "support/probe.h"
#include "support/status.h"

#include <unity.h>

#include <stddef.h>
#include <stdio.h>

void setUp() {
}

void tearDown() {
}

/* ========== helpers ========== */

// The word is 64 bits wide, so every trap in this module lives at a seam: the last word
// of a universe that does not end on one, the shift by a whole word, the bits above nbits
// that no operation may ever show. These are the sizes that put one there.
static constexpr size_t SEAMS[] = {0, 1, 63, 64, 65, 127, 128, 129, 200};

[[nodiscard]]
static tda_BitSet *make_bitset(size_t nbits, const size_t *members, size_t n) {
    tda_BitSet *b = nullptr;
    TDA_TEST_OK(tda_bitset_new(nbits, tda_al_default(), &b));

    for (size_t i = 0; i < n; ++i) {
        tda_bitset_set(b, members[i]);
    }
    return b;
}

[[nodiscard]]
static tda_BitSet *make_full(size_t nbits) {
    tda_BitSet *b = make_bitset(nbits, nullptr, 0);
    tda_bitset_set_all(b);

    return b;
}

// The whole set, read every way the type offers: bit by bit, as a count, as the three
// summaries and as a walk. A bit that leaked above nbits cannot be seen through test,
// which never looks there — it shows up in count, in all, or in the scan, which is why
// this asks all four rather than trusting one.
// 'want' is written in ascending order.
static void assert_members(const tda_BitSet *b, size_t nbits, const size_t *want, size_t n) {
    TEST_ASSERT_EQUAL_size_t(nbits, tda_bitset_len(b));
    TEST_ASSERT_EQUAL_size_t(n, tda_bitset_count(b));
    TEST_ASSERT_EQUAL_INT(n > 0, tda_bitset_any(b));
    TEST_ASSERT_EQUAL_INT(n == 0, tda_bitset_none(b));
    TEST_ASSERT_EQUAL_INT(n == nbits, tda_bitset_all(b));

    size_t at = 0;
    for (size_t i = 0; i < nbits; ++i) {
        const bool member = at < n && want[at] == i;
        TEST_ASSERT_EQUAL_INT(member, tda_bitset_test(b, i));
        at += member;
    }
    TEST_ASSERT_EQUAL_size_t(n, at);

    size_t idx;
    size_t seen = 0;
    for (size_t from = 0; tda_bitset_find_next(b, from, &idx); from = idx + 1) {
        TEST_ASSERT_TRUE_MESSAGE(seen < n, "the scan runs past the members");
        TEST_ASSERT_EQUAL_size_t(want[seen], idx);
        ++seen;
    }
    TEST_ASSERT_EQUAL_size_t(n, seen);
}

// a printer writes to a stream, so a case reads one back through tmpfile, as
// test/core/test_print.c does
static void assert_prints(const char *expected, const tda_BitSet *b) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    tda_bitset_fprint(b, stream);
    rewind(stream);

    char buf[256];
    const size_t n = fread(buf, 1, sizeof buf - 1, stream);
    buf[n] = '\0';
    fclose(stream);

    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

/* ========== lifetime ========== */

static void test_new_sets_len_and_allocator() {
    tda_BitSet *b = nullptr;
    TDA_TEST_OK(tda_bitset_new(100, tda_al_default(), &b));

    TEST_ASSERT_EQUAL_size_t(100, tda_bitset_len(b));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_bitset_al(b));

    tda_bitset_drop(b);
}

static void test_new_starts_with_nothing_in_it() {
    for (size_t s = 0; s < sizeof SEAMS / sizeof SEAMS[0]; ++s) {
        tda_BitSet *b = make_bitset(SEAMS[s], nullptr, 0);

        assert_members(b, SEAMS[s], nullptr, 0);

        tda_bitset_drop(b);
    }
}

// a universe of nothing is a real set: it holds nothing, and it holds all of nothing
static void test_an_empty_universe_is_full_and_empty_at_once() {
    tda_BitSet *b = make_bitset(0, nullptr, 0);

    TEST_ASSERT_EQUAL_size_t(0, tda_bitset_len(b));
    TEST_ASSERT_EQUAL_size_t(0, tda_bitset_count(b));
    TEST_ASSERT_FALSE(tda_bitset_any(b));
    TEST_ASSERT_TRUE(tda_bitset_none(b));
    TEST_ASSERT_TRUE(tda_bitset_all(b));

    tda_bitset_drop(b);
}

static void test_drop_null_is_noop() {
    tda_bitset_drop(nullptr);
}

static void test_drop_hands_back_everything_it_took() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_BitSet *b = nullptr;
    TDA_TEST_OK(tda_bitset_new(200, &al, &b));
    TEST_ASSERT_EQUAL_size_t(2, probe.live); // the header and the words

    tda_bitset_drop(b);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_an_empty_universe_owns_no_words() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_BitSet *b = nullptr;
    TDA_TEST_OK(tda_bitset_new(0, &al, &b));
    TEST_ASSERT_EQUAL_size_t(1, probe.live); // the header alone

    tda_bitset_drop(b);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== one bit ========== */

static void test_set_and_test_roundtrip() {
    constexpr size_t want[] = {0, 5, 64, 128};
    tda_BitSet *b = make_bitset(200, want, 4);

    assert_members(b, 200, want, 4);

    tda_bitset_drop(b);
}

static void test_clear_takes_one_out() {
    constexpr size_t start[] = {0, 5, 64, 128};
    constexpr size_t want[] = {0, 64, 128};
    tda_BitSet *b = make_bitset(200, start, 4);

    tda_bitset_clear(b, 5);
    assert_members(b, 200, want, 3);

    tda_bitset_clear(b, 5); // clearing what is already out changes nothing
    assert_members(b, 200, want, 3);

    tda_bitset_drop(b);
}

static void test_flip_toggles_one() {
    constexpr size_t none[] = {0};
    constexpr size_t one[] = {70};
    tda_BitSet *b = make_bitset(200, nullptr, 0);

    tda_bitset_flip(b, 70);
    assert_members(b, 200, one, 1);

    tda_bitset_flip(b, 70);
    assert_members(b, 200, none, 0);

    tda_bitset_drop(b);
}

static void test_set_to_writes_both_ways() {
    constexpr size_t one[] = {9};
    tda_BitSet *b = make_bitset(200, nullptr, 0);

    tda_bitset_set_to(b, 9, true);
    assert_members(b, 200, one, 1);

    tda_bitset_set_to(b, 9, true); // idempotent, not a toggle
    assert_members(b, 200, one, 1);

    tda_bitset_set_to(b, 9, false);
    assert_members(b, 200, one, 0);

    tda_bitset_drop(b);
}

// the whole point of a word is that its neighbours share it
static void test_one_bit_leaves_the_rest_of_its_word_alone() {
    tda_BitSet *b = make_bitset(200, nullptr, 0);

    for (size_t i = 0; i < 200; ++i) {
        tda_bitset_set(b, i);
        TEST_ASSERT_EQUAL_size_t(i + 1, tda_bitset_count(b));

        for (size_t j = 0; j <= i; ++j) {
            TEST_ASSERT_TRUE(tda_bitset_test(b, j));
        }
        for (size_t j = i + 1; j < 200; ++j) {
            TEST_ASSERT_FALSE(tda_bitset_test(b, j));
        }
    }

    tda_bitset_drop(b);
}

static void test_the_bits_at_the_word_seams() {
    constexpr size_t want[] = {63, 64, 65};
    tda_BitSet *b = make_bitset(129, want, 3);

    assert_members(b, 129, want, 3);

    // and the last index of the universe, which is the one the tail mask guards
    constexpr size_t seam[] = {63, 64, 65, 128};
    tda_bitset_set(b, 128);
    assert_members(b, 129, seam, 4);

    tda_bitset_drop(b);
}

/* ========== all bits ========== */

static void test_set_all_fills_the_universe() {
    for (size_t s = 0; s < sizeof SEAMS / sizeof SEAMS[0]; ++s) {
        const size_t nbits = SEAMS[s];

        tda_BitSet *b = make_full(nbits);

        // the count is the whole test: a tail left dirty makes it bigger than the universe
        TEST_ASSERT_EQUAL_size_t(nbits, tda_bitset_count(b));
        TEST_ASSERT_TRUE(tda_bitset_all(b));
        TEST_ASSERT_EQUAL_INT(nbits > 0, tda_bitset_any(b));

        for (size_t i = 0; i < nbits; ++i) {
            TEST_ASSERT_TRUE(tda_bitset_test(b, i));
        }

        tda_bitset_drop(b);
    }
}

static void test_clear_all_empties_the_set() {
    for (size_t s = 0; s < sizeof SEAMS / sizeof SEAMS[0]; ++s) {
        tda_BitSet *b = make_full(SEAMS[s]);

        tda_bitset_clear_all(b);
        assert_members(b, SEAMS[s], nullptr, 0);

        tda_bitset_drop(b);
    }
}

static void test_flip_all_is_the_complement() {
    for (size_t s = 0; s < sizeof SEAMS / sizeof SEAMS[0]; ++s) {
        const size_t nbits = SEAMS[s];

        tda_BitSet *b = make_bitset(nbits, nullptr, 0);
        for (size_t i = 0; i < nbits; i += 2) {
            tda_bitset_set(b, i);
        }
        const size_t was = tda_bitset_count(b);

        tda_bitset_flip_all(b);

        // the complement of a set of 'was' members in a universe of 'nbits' has the rest
        // and nothing above them
        TEST_ASSERT_EQUAL_size_t(nbits - was, tda_bitset_count(b));
        for (size_t i = 0; i < nbits; ++i) {
            TEST_ASSERT_EQUAL_INT(i % 2 != 0, tda_bitset_test(b, i));
        }

        tda_bitset_drop(b);
    }
}

static void test_flip_all_twice_changes_nothing() {
    constexpr size_t want[] = {0, 63, 64, 128};
    tda_BitSet *b = make_bitset(129, want, 4);

    tda_bitset_flip_all(b);
    tda_bitset_flip_all(b);
    assert_members(b, 129, want, 4);

    tda_bitset_drop(b);
}

static void test_the_whole_set_ops_survive_an_empty_universe() {
    tda_BitSet *b = make_bitset(0, nullptr, 0);

    tda_bitset_set_all(b);
    TEST_ASSERT_TRUE(tda_bitset_all(b));
    TEST_ASSERT_FALSE(tda_bitset_any(b));

    tda_bitset_flip_all(b);
    tda_bitset_clear_all(b);
    assert_members(b, 0, nullptr, 0);

    tda_bitset_drop(b);
}

/* ========== scan ========== */

static void test_find_next_walks_the_members() {
    constexpr size_t want[] = {0, 1, 63, 64, 127, 128, 199};
    tda_BitSet *b = make_bitset(200, want, 7);

    assert_members(b, 200, want, 7); // the walk is checked in there against every bit

    // and from a start that is not a member, the answer is the next one that is
    size_t idx;
    TEST_ASSERT_TRUE(tda_bitset_find_next(b, 2, &idx));
    TEST_ASSERT_EQUAL_size_t(63, idx);
    TEST_ASSERT_TRUE(tda_bitset_find_next(b, 129, &idx));
    TEST_ASSERT_EQUAL_size_t(199, idx);

    tda_bitset_drop(b);
}

static void test_find_next_from_past_the_end_is_a_miss() {
    constexpr size_t want[] = {0, 5};
    tda_BitSet *b = make_bitset(65, want, 2);

    size_t idx = 12345;
    TEST_ASSERT_FALSE(tda_bitset_find_next(b, 65, &idx));
    TEST_ASSERT_FALSE(tda_bitset_find_next(b, 66, &idx));
    TEST_ASSERT_FALSE(tda_bitset_find_next(b, SIZE_MAX, &idx));
    TEST_ASSERT_EQUAL_size_t(12345, idx); // a miss writes nothing

    tda_bitset_drop(b);
}

static void test_find_next_on_an_empty_set_is_a_miss() {
    tda_BitSet *b = make_bitset(200, nullptr, 0);

    size_t idx = 12345;
    TEST_ASSERT_FALSE(tda_bitset_find_next(b, 0, &idx));
    TEST_ASSERT_EQUAL_size_t(12345, idx);

    tda_bitset_drop(b);
}

static void test_find_next_clear_walks_the_gaps() {
    constexpr size_t members[] = {0, 1, 2, 64, 65};
    tda_BitSet *b = make_bitset(200, members, 5);

    size_t idx;
    TEST_ASSERT_TRUE(tda_bitset_find_next_clear(b, 0, &idx));
    TEST_ASSERT_EQUAL_size_t(3, idx);
    TEST_ASSERT_TRUE(tda_bitset_find_next_clear(b, 64, &idx));
    TEST_ASSERT_EQUAL_size_t(66, idx);
    TEST_ASSERT_TRUE(tda_bitset_find_next_clear(b, 199, &idx));
    TEST_ASSERT_EQUAL_size_t(199, idx);

    tda_bitset_drop(b);
}

// the complement of a full last word is a word of ones above nbits, and not one of them
// is an index of this set
static void test_find_next_clear_misses_on_a_full_set() {
    for (size_t s = 0; s < sizeof SEAMS / sizeof SEAMS[0]; ++s) {
        tda_BitSet *b = make_full(SEAMS[s]);

        size_t idx = 12345;
        for (size_t from = 0; from <= SEAMS[s]; ++from) {
            TEST_ASSERT_FALSE(tda_bitset_find_next_clear(b, from, &idx));
        }
        TEST_ASSERT_EQUAL_size_t(12345, idx);

        tda_bitset_drop(b);
    }
}

static void test_find_next_clear_on_an_empty_set_answers_the_start() {
    tda_BitSet *b = make_bitset(129, nullptr, 0);

    size_t idx;
    TEST_ASSERT_TRUE(tda_bitset_find_next_clear(b, 0, &idx));
    TEST_ASSERT_EQUAL_size_t(0, idx);
    TEST_ASSERT_TRUE(tda_bitset_find_next_clear(b, 64, &idx));
    TEST_ASSERT_EQUAL_size_t(64, idx);
    TEST_ASSERT_TRUE(tda_bitset_find_next_clear(b, 128, &idx));
    TEST_ASSERT_EQUAL_size_t(128, idx);

    TEST_ASSERT_FALSE(tda_bitset_find_next_clear(b, 129, &idx));

    tda_bitset_drop(b);
}

// a single member at the far end of every universe: the scans have to cross whole empty
// words to reach it
static void test_the_scans_cross_whole_words() {
    for (size_t s = 0; s < sizeof SEAMS / sizeof SEAMS[0]; ++s) {
        const size_t nbits = SEAMS[s];
        if (nbits == 0) {
            continue;
        }

        const size_t last = nbits - 1;
        tda_BitSet *b = make_bitset(nbits, &last, 1);

        size_t idx;
        TEST_ASSERT_TRUE(tda_bitset_find_next(b, 0, &idx));
        TEST_ASSERT_EQUAL_size_t(last, idx);

        tda_bitset_flip_all(b);
        TEST_ASSERT_TRUE(tda_bitset_find_next_clear(b, 0, &idx));
        TEST_ASSERT_EQUAL_size_t(last, idx);

        tda_bitset_drop(b);
    }
}

/* ========== copy ========== */

static void test_copy_is_independent() {
    constexpr size_t want[] = {0, 64, 128};
    tda_BitSet *b = make_bitset(129, want, 3);

    tda_BitSet *copy = nullptr;
    TDA_TEST_OK(tda_bitset_copy(b, &copy));

    assert_members(copy, 129, want, 3);
    TEST_ASSERT_TRUE(tda_bitset_eq(b, copy));
    TEST_ASSERT_EQUAL_PTR(tda_bitset_al(b), tda_bitset_al(copy));

    tda_bitset_clear(copy, 64);
    TEST_ASSERT_FALSE(tda_bitset_eq(b, copy));
    assert_members(b, 129, want, 3);

    tda_bitset_drop(b);
    tda_bitset_drop(copy);
}

static void test_copy_with_builds_on_the_given_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    constexpr size_t want[] = {0, 64, 128};
    tda_BitSet *src = make_bitset(129, want, 3);

    tda_BitSet *dst = nullptr;
    TDA_TEST_OK(tda_bitset_copy_with(src, arena, &dst));

    TEST_ASSERT_EQUAL_PTR(arena, tda_bitset_al(dst));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_bitset_al(src));
    TEST_ASSERT_TRUE(tda_bitset_eq(src, dst));

    // the source is gone and the copy still holds the members: the words are its own
    tda_bitset_drop(src);
    assert_members(dst, 129, want, 3);

    tda_bitset_drop(dst);
    tda_al_arena_drop(arena);
}

// the words are asked of the allocator the copy is going to, not of the source's
static void test_copy_with_reports_an_exhausted_target_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);
    tda_test_arena_leave(arena, 0);

    constexpr size_t want[] = {0, 64, 128};
    tda_BitSet *src = make_bitset(129, want, 3);

    tda_BitSet *dst = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_bitset_copy_with(src, arena, &dst));
    TEST_ASSERT_NULL(dst);
    assert_members(src, 129, want, 3);

    tda_bitset_drop(src);
    tda_al_arena_drop(arena);
}

static void test_move_assign_hands_over_the_contents_on_one_allocator() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_BitSet *src = nullptr;
    TDA_TEST_OK(tda_bitset_new(129, &al, &src));
    tda_bitset_set(src, 0);
    tda_bitset_set(src, 128);

    tda_BitSet *dst = nullptr;
    TDA_TEST_OK(tda_bitset_new(8, &al, &dst));
    tda_bitset_set(dst, 3);

    const size_t requests = tda_test_probe_requests(&probe);
    TDA_TEST_OK(tda_bitset_move_assign(src, dst));

    // nothing was asked of the allocator: the words changed hands, universe and all
    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    assert_members(dst, 129, (const size_t[]){0, 128}, 2);
    TEST_ASSERT_EQUAL_size_t(0, tda_bitset_len(src));

    tda_bitset_drop(src);
    tda_bitset_drop(dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_move_assign_across_allocators_empties_the_source() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    constexpr size_t want[] = {0, 64, 128};
    tda_BitSet *src = make_bitset(129, want, 3);

    tda_BitSet *dst = nullptr;
    TDA_TEST_OK(tda_bitset_new(8, arena, &dst));
    tda_bitset_set(dst, 3);

    TDA_TEST_OK(tda_bitset_move_assign(src, dst));

    assert_members(dst, 129, want, 3);
    TEST_ASSERT_EQUAL_PTR(arena, tda_bitset_al(dst));

    TEST_ASSERT_EQUAL_size_t(0, tda_bitset_len(src));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_bitset_al(src));

    tda_bitset_drop(src);
    tda_bitset_drop(dst);
    tda_al_arena_drop(arena);
}

static void test_move_assign_across_allocators_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_BitSet *dst = nullptr;
    TDA_TEST_OK(tda_bitset_new(8, arena, &dst));
    tda_bitset_set(dst, 3);
    tda_test_arena_leave(arena, 0);

    constexpr size_t want[] = {0, 64, 128};
    tda_BitSet *src = make_bitset(129, want, 3);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_bitset_move_assign(src, dst));

    assert_members(src, 129, want, 3);
    assert_members(dst, 8, (const size_t[]){3}, 1);

    tda_bitset_drop(src);
    tda_bitset_drop(dst);
    tda_al_arena_drop(arena);
}

static void test_move_assign_of_itself_changes_nothing() {
    constexpr size_t want[] = {0, 64, 128};
    tda_BitSet *b = make_bitset(129, want, 3);

    TDA_TEST_OK(tda_bitset_move_assign(b, b));

    assert_members(b, 129, want, 3);

    tda_bitset_drop(b);
}

static void test_for_each_walks_the_members_in_order() {
    // members on both sides of a word seam: the walk has to cross it
    constexpr size_t want[] = {0, 63, 64, 128};
    tda_BitSet *b = make_bitset(129, want, 4);

    size_t seen[4];
    size_t n = 0;
    TDA_BITSET_FOR_EACH(idx, b) {
        TEST_ASSERT_TRUE(n < 4);
        seen[n++] = idx;
    }

    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_size_t_ARRAY(want, seen, 4);

    tda_bitset_drop(b);
}

static void test_for_each_over_an_empty_set_runs_no_body() {
    tda_BitSet *b = make_bitset(129, nullptr, 0);

    size_t n = 0;
    TDA_BITSET_FOR_EACH(idx, b) {
        TDA_UNUSED(idx);
        ++n;
    }

    TEST_ASSERT_EQUAL_size_t(0, n);

    tda_bitset_drop(b);
}

static void test_swap_exchanges_the_contents() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_BitSet *a = nullptr;
    TDA_TEST_OK(tda_bitset_new(129, &al, &a));
    tda_bitset_set(a, 128);

    tda_BitSet *b = nullptr;
    TDA_TEST_OK(tda_bitset_new(8, &al, &b));
    tda_bitset_set(b, 3);

    const size_t requests = tda_test_probe_requests(&probe);
    tda_bitset_swap(a, b);

    // the words change hands where they lie: nothing is asked of the allocator
    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    // the universe travels with the words, so the two swap lengths as well as members
    assert_members(a, 8, (const size_t[]){3}, 1);
    assert_members(b, 129, (const size_t[]){128}, 1);

    tda_bitset_drop(a);
    tda_bitset_drop(b);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_swap_of_itself_changes_nothing() {
    constexpr size_t want[] = {0, 64, 128};
    tda_BitSet *b = make_bitset(129, want, 3);

    tda_bitset_swap(b, b);

    assert_members(b, 129, want, 3);

    tda_bitset_drop(b);
}

static void test_copy_of_an_empty_universe() {
    tda_BitSet *b = make_bitset(0, nullptr, 0);

    tda_BitSet *copy = nullptr;
    TDA_TEST_OK(tda_bitset_copy(b, &copy));

    assert_members(copy, 0, nullptr, 0);
    TEST_ASSERT_TRUE(tda_bitset_eq(b, copy));

    tda_bitset_drop(b);
    tda_bitset_drop(copy);
}

static void test_copy_assign_grow_shrink_empty() {
    constexpr size_t want[] = {0, 64, 128};
    tda_BitSet *src = make_bitset(129, want, 3);

    // grow: 65 -> 129
    tda_BitSet *dst = make_full(65);
    TDA_TEST_OK(tda_bitset_copy_assign(src, dst));
    assert_members(dst, 129, want, 3);
    tda_bitset_drop(dst);

    // shrink: 200 -> 129
    dst = make_full(200);
    TDA_TEST_OK(tda_bitset_copy_assign(src, dst));
    assert_members(dst, 129, want, 3);
    tda_bitset_drop(dst);

    // same size, no resize at all
    dst = make_full(129);
    TDA_TEST_OK(tda_bitset_copy_assign(src, dst));
    assert_members(dst, 129, want, 3);
    tda_bitset_drop(dst);

    // down to an empty universe: the words are handed back, not kept
    tda_BitSet *empty = make_bitset(0, nullptr, 0);
    dst = make_full(129);
    TDA_TEST_OK(tda_bitset_copy_assign(empty, dst));
    assert_members(dst, 0, nullptr, 0);

    // and back up from one
    TDA_TEST_OK(tda_bitset_copy_assign(src, dst));
    assert_members(dst, 129, want, 3);

    tda_bitset_drop(dst);
    tda_bitset_drop(empty);
    tda_bitset_drop(src);
}

static void test_copy_assign_self_is_noop() {
    constexpr size_t want[] = {0, 64, 128};
    tda_BitSet *b = make_bitset(129, want, 3);

    TDA_TEST_OK(tda_bitset_copy_assign(b, b));
    assert_members(b, 129, want, 3);

    tda_bitset_drop(b);
}

static void test_copy_assign_keeps_the_target_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 4096);
    TEST_ASSERT_NOT_NULL(arena);

    tda_BitSet *dst = nullptr;
    TDA_TEST_OK(tda_bitset_new(65, arena, &dst));

    constexpr size_t want[] = {0, 128};
    tda_BitSet *src = make_bitset(129, want, 2);

    TDA_TEST_OK(tda_bitset_copy_assign(src, dst));
    assert_members(dst, 129, want, 2);
    TEST_ASSERT_EQUAL_PTR(arena, tda_bitset_al(dst));

    tda_bitset_drop(dst);
    tda_bitset_drop(src);
    tda_al_arena_drop(arena);
}

/* ========== set ops ========== */

static void test_eq_matches_the_same_members() {
    constexpr size_t want[] = {0, 64, 128};
    tda_BitSet *a = make_bitset(129, want, 3);
    tda_BitSet *b = make_bitset(129, want, 3);

    TEST_ASSERT_TRUE(tda_bitset_eq(a, a));
    TEST_ASSERT_TRUE(tda_bitset_eq(a, b));
    TEST_ASSERT_TRUE(tda_bitset_eq(b, a));

    tda_bitset_flip(b, 7);
    TEST_ASSERT_FALSE(tda_bitset_eq(a, b));
    TEST_ASSERT_FALSE(tda_bitset_eq(b, a));

    tda_bitset_drop(a);
    tda_bitset_drop(b);
}

// a different universe is simply not equal, and that is not an assert
static void test_eq_parts_different_universes() {
    tda_BitSet *a = make_bitset(64, nullptr, 0);
    tda_BitSet *b = make_bitset(65, nullptr, 0);
    tda_BitSet *empty = make_bitset(0, nullptr, 0);

    TEST_ASSERT_FALSE(tda_bitset_eq(a, b));
    TEST_ASSERT_FALSE(tda_bitset_eq(b, a));
    TEST_ASSERT_FALSE(tda_bitset_eq(a, empty));
    TEST_ASSERT_TRUE(tda_bitset_eq(empty, empty));

    tda_bitset_drop(a);
    tda_bitset_drop(b);
    tda_bitset_drop(empty);
}

static void test_union_adds_the_other_members() {
    constexpr size_t lhs[] = {0, 64};
    constexpr size_t rhs[] = {64, 128};
    constexpr size_t want[] = {0, 64, 128};

    tda_BitSet *a = make_bitset(129, lhs, 2);
    tda_BitSet *b = make_bitset(129, rhs, 2);

    tda_bitset_union(a, b);
    assert_members(a, 129, want, 3);
    assert_members(b, 129, rhs, 2); // the other side is read, never written

    tda_bitset_drop(a);
    tda_bitset_drop(b);
}

static void test_intersect_keeps_what_both_hold() {
    constexpr size_t lhs[] = {0, 64};
    constexpr size_t rhs[] = {64, 128};
    constexpr size_t want[] = {64};

    tda_BitSet *a = make_bitset(129, lhs, 2);
    tda_BitSet *b = make_bitset(129, rhs, 2);

    tda_bitset_intersect(a, b);
    assert_members(a, 129, want, 1);
    assert_members(b, 129, rhs, 2);

    tda_bitset_drop(a);
    tda_bitset_drop(b);
}

static void test_difference_drops_what_the_other_holds() {
    constexpr size_t lhs[] = {0, 64};
    constexpr size_t rhs[] = {64, 128};
    constexpr size_t want[] = {0};

    tda_BitSet *a = make_bitset(129, lhs, 2);
    tda_BitSet *b = make_bitset(129, rhs, 2);

    tda_bitset_difference(a, b);
    assert_members(a, 129, want, 1);
    assert_members(b, 129, rhs, 2);

    tda_bitset_drop(a);
    tda_bitset_drop(b);
}

static void test_symmetric_difference_keeps_what_exactly_one_holds() {
    constexpr size_t lhs[] = {0, 64};
    constexpr size_t rhs[] = {64, 128};
    constexpr size_t want[] = {0, 128};

    tda_BitSet *a = make_bitset(129, lhs, 2);
    tda_BitSet *b = make_bitset(129, rhs, 2);

    tda_bitset_symmetric_difference(a, b);
    assert_members(a, 129, want, 2);
    assert_members(b, 129, rhs, 2);

    tda_bitset_drop(a);
    tda_bitset_drop(b);
}

// every pairwise op takes the same set on both sides, and each has to mean what it says
static void test_the_pairwise_ops_on_one_set() {
    constexpr size_t want[] = {0, 64, 128};

    tda_BitSet *b = make_bitset(129, want, 3);
    tda_bitset_union(b, b);
    assert_members(b, 129, want, 3);
    tda_bitset_intersect(b, b);
    assert_members(b, 129, want, 3);
    tda_bitset_drop(b);

    b = make_bitset(129, want, 3);
    tda_bitset_difference(b, b);
    assert_members(b, 129, nullptr, 0);
    tda_bitset_drop(b);

    b = make_bitset(129, want, 3);
    tda_bitset_symmetric_difference(b, b);
    assert_members(b, 129, nullptr, 0);
    tda_bitset_drop(b);
}

// the pairwise ops cannot dirty the tail, whatever the operands: this is the case that
// would catch it if one ever did
static void test_the_pairwise_ops_keep_the_tail_clear() {
    for (size_t s = 0; s < sizeof SEAMS / sizeof SEAMS[0]; ++s) {
        const size_t nbits = SEAMS[s];

        tda_BitSet *a = make_full(nbits);
        tda_BitSet *b = make_full(nbits);

        tda_bitset_union(a, b);
        TEST_ASSERT_EQUAL_size_t(nbits, tda_bitset_count(a));

        tda_bitset_symmetric_difference(a, b);
        TEST_ASSERT_EQUAL_size_t(0, tda_bitset_count(a));

        tda_bitset_flip_all(a);
        tda_bitset_intersect(a, b);
        TEST_ASSERT_EQUAL_size_t(nbits, tda_bitset_count(a));

        tda_bitset_drop(a);
        tda_bitset_drop(b);
    }
}

static void test_is_subset() {
    constexpr size_t few[] = {0, 64};
    constexpr size_t many[] = {0, 64, 128};

    tda_BitSet *small = make_bitset(129, few, 2);
    tda_BitSet *big = make_bitset(129, many, 3);
    tda_BitSet *empty = make_bitset(129, nullptr, 0);

    TEST_ASSERT_TRUE(tda_bitset_is_subset(small, big));
    TEST_ASSERT_FALSE(tda_bitset_is_subset(big, small));
    TEST_ASSERT_TRUE(tda_bitset_is_subset(small, small));
    TEST_ASSERT_TRUE(tda_bitset_is_subset(empty, big)); // the empty set is in everything
    TEST_ASSERT_FALSE(tda_bitset_is_subset(big, empty));

    tda_bitset_drop(small);
    tda_bitset_drop(big);
    tda_bitset_drop(empty);
}

static void test_intersects() {
    constexpr size_t lhs[] = {0, 64};
    constexpr size_t shares[] = {64, 128};
    constexpr size_t apart[] = {1, 128};

    tda_BitSet *a = make_bitset(129, lhs, 2);
    tda_BitSet *b = make_bitset(129, shares, 2);
    tda_BitSet *c = make_bitset(129, apart, 2);
    tda_BitSet *empty = make_bitset(129, nullptr, 0);

    TEST_ASSERT_TRUE(tda_bitset_intersects(a, b));
    TEST_ASSERT_TRUE(tda_bitset_intersects(b, a));
    TEST_ASSERT_FALSE(tda_bitset_intersects(a, c));
    TEST_ASSERT_FALSE(tda_bitset_intersects(c, a));
    TEST_ASSERT_FALSE(tda_bitset_intersects(a, empty));
    TEST_ASSERT_TRUE(tda_bitset_intersects(a, a));

    tda_bitset_drop(a);
    tda_bitset_drop(b);
    tda_bitset_drop(c);
    tda_bitset_drop(empty);
}

static void test_the_set_ops_survive_an_empty_universe() {
    tda_BitSet *a = make_bitset(0, nullptr, 0);
    tda_BitSet *b = make_bitset(0, nullptr, 0);

    tda_bitset_union(a, b);
    tda_bitset_intersect(a, b);
    tda_bitset_difference(a, b);
    tda_bitset_symmetric_difference(a, b);

    TEST_ASSERT_TRUE(tda_bitset_eq(a, b));
    TEST_ASSERT_TRUE(tda_bitset_is_subset(a, b));
    TEST_ASSERT_FALSE(tda_bitset_intersects(a, b));

    tda_bitset_drop(a);
    tda_bitset_drop(b);
}

/* ========== print ========== */

static void test_fprint_writes_the_members() {
    constexpr size_t want[] = {0, 3, 64, 69};
    tda_BitSet *b = make_bitset(129, want, 4);

    assert_prints("{0, 3, 64, 69}\n", b);

    tda_bitset_drop(b);
}

static void test_fprint_of_an_empty_set() {
    tda_BitSet *b = make_bitset(129, nullptr, 0);
    assert_prints("{}\n", b);
    tda_bitset_drop(b);

    b = make_bitset(0, nullptr, 0);
    assert_prints("{}\n", b);
    tda_bitset_drop(b);
}

static void test_fprint_shows_no_bit_above_the_universe() {
    tda_BitSet *b = make_full(65);

    assert_prints(
        "{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, "
        "22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, "
        "42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, "
        "62, 63, 64}\n",
        b
    );

    tda_bitset_drop(b);
}

// the stdout twin takes no stream and cannot be captured portably, so a case can only
// say that it runs and reaches the same printer
static void test_print_writes_to_stdout() {
    constexpr size_t members[] = {0, 3};
    tda_BitSet *b = make_bitset(8, members, 2);

    tda_bitset_print(b);

    tda_bitset_drop(b);
}

/* ========== allocation failure ========== */

static void test_new_reports_a_refused_header() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);
    tda_test_probe_fail_after_next(&probe, 0);

    tda_BitSet *b = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_bitset_new(200, &al, &b));

    TEST_ASSERT_NULL(b);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_refused_words_free_the_header() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);
    tda_test_probe_fail_after_next(&probe, 1); // the header goes through, the words do not

    tda_BitSet *b = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_bitset_new(200, &al, &b));

    TEST_ASSERT_NULL(b);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_copy_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 512);
    TEST_ASSERT_NOT_NULL(arena);

    tda_BitSet *src = nullptr;
    TDA_TEST_OK(tda_bitset_new(200, arena, &src));
    tda_bitset_set(src, 7);
    tda_test_arena_leave(arena, 0);

    tda_BitSet *copy = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_bitset_copy(src, &copy));
    TEST_ASSERT_NULL(copy);

    // the source is untouched by the failure
    constexpr size_t want[] = {7};
    assert_members(src, 200, want, 1);

    tda_al_arena_drop(arena);
}

static void test_copy_assign_leaves_the_target_untouched_on_failure() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    constexpr size_t kept[] = {0, 64};
    tda_BitSet *dst = nullptr;
    TDA_TEST_OK(tda_bitset_new(65, &al, &dst));
    tda_bitset_set(dst, 0);
    tda_bitset_set(dst, 64);

    tda_BitSet *src = make_bitset(200, nullptr, 0);

    // the resize is the only request copy_assign makes, so refusing the next one hits it
    tda_test_probe_fail_after_next(&probe, 0);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_bitset_copy_assign(src, dst));

    assert_members(dst, 65, kept, 2);

    tda_bitset_drop(dst);
    tda_bitset_drop(src);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a target that already has the right number of words needs no allocator at all
static void test_copy_assign_of_the_same_universe_never_allocates() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_BitSet *dst = nullptr;
    TDA_TEST_OK(tda_bitset_new(129, &al, &dst));
    const size_t before = tda_test_probe_requests(&probe);

    constexpr size_t want[] = {0, 128};
    tda_BitSet *src = make_bitset(129, want, 2);

    TDA_TEST_OK(tda_bitset_copy_assign(src, dst));

    TEST_ASSERT_EQUAL_size_t(before, tda_test_probe_requests(&probe));
    assert_members(dst, 129, want, 2);

    tda_bitset_drop(dst);
    tda_bitset_drop(src);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_new_sets_len_and_allocator);
    RUN_TEST(test_new_starts_with_nothing_in_it);
    RUN_TEST(test_an_empty_universe_is_full_and_empty_at_once);
    RUN_TEST(test_drop_null_is_noop);
    RUN_TEST(test_drop_hands_back_everything_it_took);
    RUN_TEST(test_an_empty_universe_owns_no_words);

    RUN_TEST(test_set_and_test_roundtrip);
    RUN_TEST(test_clear_takes_one_out);
    RUN_TEST(test_flip_toggles_one);
    RUN_TEST(test_set_to_writes_both_ways);
    RUN_TEST(test_one_bit_leaves_the_rest_of_its_word_alone);
    RUN_TEST(test_the_bits_at_the_word_seams);

    RUN_TEST(test_set_all_fills_the_universe);
    RUN_TEST(test_clear_all_empties_the_set);
    RUN_TEST(test_flip_all_is_the_complement);
    RUN_TEST(test_flip_all_twice_changes_nothing);
    RUN_TEST(test_the_whole_set_ops_survive_an_empty_universe);

    RUN_TEST(test_find_next_walks_the_members);
    RUN_TEST(test_find_next_from_past_the_end_is_a_miss);
    RUN_TEST(test_find_next_on_an_empty_set_is_a_miss);
    RUN_TEST(test_find_next_clear_walks_the_gaps);
    RUN_TEST(test_find_next_clear_misses_on_a_full_set);
    RUN_TEST(test_find_next_clear_on_an_empty_set_answers_the_start);
    RUN_TEST(test_the_scans_cross_whole_words);

    RUN_TEST(test_copy_is_independent);
    RUN_TEST(test_copy_with_builds_on_the_given_allocator);
    RUN_TEST(test_copy_with_reports_an_exhausted_target_arena);
    RUN_TEST(test_move_assign_hands_over_the_contents_on_one_allocator);
    RUN_TEST(test_move_assign_across_allocators_empties_the_source);
    RUN_TEST(test_move_assign_across_allocators_reports_an_exhausted_arena);
    RUN_TEST(test_move_assign_of_itself_changes_nothing);
    RUN_TEST(test_for_each_walks_the_members_in_order);
    RUN_TEST(test_for_each_over_an_empty_set_runs_no_body);

    RUN_TEST(test_swap_exchanges_the_contents);
    RUN_TEST(test_swap_of_itself_changes_nothing);
    RUN_TEST(test_copy_of_an_empty_universe);
    RUN_TEST(test_copy_assign_grow_shrink_empty);
    RUN_TEST(test_copy_assign_self_is_noop);
    RUN_TEST(test_copy_assign_keeps_the_target_allocator);

    RUN_TEST(test_eq_matches_the_same_members);
    RUN_TEST(test_eq_parts_different_universes);
    RUN_TEST(test_union_adds_the_other_members);
    RUN_TEST(test_intersect_keeps_what_both_hold);
    RUN_TEST(test_difference_drops_what_the_other_holds);
    RUN_TEST(test_symmetric_difference_keeps_what_exactly_one_holds);
    RUN_TEST(test_the_pairwise_ops_on_one_set);
    RUN_TEST(test_the_pairwise_ops_keep_the_tail_clear);
    RUN_TEST(test_is_subset);
    RUN_TEST(test_intersects);
    RUN_TEST(test_the_set_ops_survive_an_empty_universe);

    RUN_TEST(test_fprint_writes_the_members);
    RUN_TEST(test_fprint_of_an_empty_set);
    RUN_TEST(test_fprint_shows_no_bit_above_the_universe);
    RUN_TEST(test_print_writes_to_stdout);

    RUN_TEST(test_new_reports_a_refused_header);
    RUN_TEST(test_refused_words_free_the_header);
    RUN_TEST(test_copy_reports_an_exhausted_arena);
    RUN_TEST(test_copy_assign_leaves_the_target_untouched_on_failure);
    RUN_TEST(test_copy_assign_of_the_same_universe_never_allocates);

    return UNITY_END();
}
