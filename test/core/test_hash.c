#include "tda/core/hash.h"
#include "tda/core/cmp.h"

#include <unity.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void setUp() {
}

void tearDown() {
}

// a NaN with a chosen payload. All NaNs are equal under tda_eq_f64, so they must all
// hash alike
static double nan_with_bits(uint64_t bits) {
    double val;
    memcpy(&val, &bits, sizeof(val));

    return val;
}

static float nan_with_bits_f32(uint32_t bits) {
    float val;
    memcpy(&val, &bits, sizeof(val));

    return val;
}

/* ========== the invariant ========== */

// the contract of a hash: equal keys agree. "Equal" means tda_eq_<T>, not memcmp, which
// is what makes the float cases below non-trivial.
static void test_hash_agrees_with_equality() {
    constexpr int32_t a = 42;
    constexpr int32_t b = 42;
    constexpr int32_t c = 43;

    TEST_ASSERT_TRUE(tda_eq_i32(&a, &b, nullptr));
    TEST_ASSERT_EQUAL_UINT64(tda_hash_i32(&a, nullptr), tda_hash_i32(&b, nullptr));
    TEST_ASSERT_NOT_EQUAL_UINT64(tda_hash_i32(&a, nullptr), tda_hash_i32(&c, nullptr));

    constexpr uint64_t big = UINT64_MAX;
    constexpr uint64_t same = UINT64_MAX;
    TEST_ASSERT_EQUAL_UINT64(tda_hash_u64(&big, nullptr), tda_hash_u64(&same, nullptr));

    constexpr size_t n = 7;
    constexpr ptrdiff_t d = -7;
    TEST_ASSERT_NOT_EQUAL_UINT64(tda_hash_size(&n, nullptr), tda_hash_ptrdiff(&d, nullptr));
}

// tda_eq_f64(-0.0, 0.0) is true while the bit patterns differ, so hashing the bits
// straight through would break the contract
static void test_hash_folds_the_two_zeroes_together() {
    constexpr double neg = -0.0;
    constexpr double pos = 0.0;
    TEST_ASSERT_TRUE(tda_eq_f64(&neg, &pos, nullptr));
    TEST_ASSERT_EQUAL_UINT64(tda_hash_f64(&neg, nullptr), tda_hash_f64(&pos, nullptr));

    constexpr float negf = -0.0f;
    constexpr float posf = 0.0f;
    TEST_ASSERT_EQUAL_UINT64(tda_hash_f32(&negf, nullptr), tda_hash_f32(&posf, nullptr));
}

// the other half of the same contract: this project orders NaN as equal to NaN, and a
// NaN carries a payload, so two of them differ bit for bit while comparing equal
static void test_hash_folds_every_nan_together() {
    const double quiet = nan_with_bits(UINT64_C(0x7ff8000000000000));
    const double payload = nan_with_bits(UINT64_C(0x7ff8000000000001));
    const double negative = nan_with_bits(UINT64_C(0xfff8000000000000));

    TEST_ASSERT_TRUE(tda_eq_f64(&quiet, &payload, nullptr));
    TEST_ASSERT_EQUAL_UINT64(tda_hash_f64(&quiet, nullptr), tda_hash_f64(&payload, nullptr));
    TEST_ASSERT_EQUAL_UINT64(tda_hash_f64(&quiet, nullptr), tda_hash_f64(&negative, nullptr));

    const float quietf = nan_with_bits_f32(UINT32_C(0x7fc00000));
    const float payloadf = nan_with_bits_f32(UINT32_C(0x7fc00001));
    TEST_ASSERT_EQUAL_UINT64(tda_hash_f32(&quietf, nullptr), tda_hash_f32(&payloadf, nullptr));

    // folding them onto zero would satisfy the contract too, and hand every table a
    // guaranteed collision between its two most ordinary keys
    constexpr double zero = 0.0;
    TEST_ASSERT_NOT_EQUAL_UINT64(tda_hash_f64(&quiet, nullptr), tda_hash_f64(&zero, nullptr));
}

// the narrow types have their own hashers, and each must keep the same contract: equal
// keys agree, a neighbour does not
static void test_hash_agrees_with_equality_for_every_width() {
    TEST_ASSERT_EQUAL_UINT64(tda_hash_i8(&(int8_t){-8}, nullptr), tda_hash_i8(&(int8_t){-8}, nullptr));
    TEST_ASSERT_NOT_EQUAL_UINT64(tda_hash_i8(&(int8_t){-8}, nullptr), tda_hash_i8(&(int8_t){-7}, nullptr));

    TEST_ASSERT_EQUAL_UINT64(tda_hash_i16(&(int16_t){-16}, nullptr), tda_hash_i16(&(int16_t){-16}, nullptr));
    TEST_ASSERT_NOT_EQUAL_UINT64(tda_hash_i16(&(int16_t){-16}, nullptr), tda_hash_i16(&(int16_t){-15}, nullptr));

    TEST_ASSERT_EQUAL_UINT64(tda_hash_u16(&(uint16_t){16}, nullptr), tda_hash_u16(&(uint16_t){16}, nullptr));
    TEST_ASSERT_NOT_EQUAL_UINT64(tda_hash_u16(&(uint16_t){16}, nullptr), tda_hash_u16(&(uint16_t){17}, nullptr));

    TEST_ASSERT_EQUAL_UINT64(tda_hash_u32(&(uint32_t){32}, nullptr), tda_hash_u32(&(uint32_t){32}, nullptr));
    TEST_ASSERT_NOT_EQUAL_UINT64(tda_hash_u32(&(uint32_t){32}, nullptr), tda_hash_u32(&(uint32_t){33}, nullptr));
}

/* ========== quality ========== */

// fmix64 leaves zero at zero, so without the seed in hash_mix_u64 every zero key lands
// in bucket zero of every table
static void test_hash_does_not_leave_zero_at_zero() {
    constexpr int32_t zero_i = 0;
    constexpr uint64_t zero_u = 0;
    constexpr double zero_f = 0.0;

    TEST_ASSERT_NOT_EQUAL_UINT64(0, tda_hash_i32(&zero_i, nullptr));
    TEST_ASSERT_NOT_EQUAL_UINT64(0, tda_hash_u64(&zero_u, nullptr));
    TEST_ASSERT_NOT_EQUAL_UINT64(0, tda_hash_f64(&zero_f, nullptr));
}

// keys that share their low bits are what an unmixed hash fails on: taken modulo eight,
// every multiple of eight would land in the same bucket
static void test_hash_spreads_keys_that_share_their_low_bits() {
    size_t buckets[8] = {0};

    for (int32_t i = 0; i < 256; ++i) {
        const int32_t key = i * 8;
        buckets[tda_hash_i32(&key, nullptr) % 8]++;
    }

    for (size_t i = 0; i < 8; ++i) {
        TEST_ASSERT_TRUE(buckets[i] > 0);
    }
}

/* ========== char ========== */

// the byte is read as unsigned char on purpose: reading it as char would sign-extend on
// a target where char is signed, and the same letter would hash differently there
static void test_hash_char_reads_the_byte_not_the_sign() {
    constexpr char high = (char) 200;
    constexpr unsigned char same_byte = 200;

    TEST_ASSERT_EQUAL_UINT64(tda_hash_u8(&same_byte, nullptr), tda_hash_char(&high, nullptr));

    constexpr char a = 'a';
    constexpr char b = 'b';
    TEST_ASSERT_NOT_EQUAL_UINT64(tda_hash_char(&a, nullptr), tda_hash_char(&b, nullptr));
}

/* ========== bytes ========== */

static void test_hash_bytes_follows_the_content() {
    constexpr unsigned char a[3] = {1, 2, 3};
    constexpr unsigned char same[3] = {1, 2, 3};
    constexpr unsigned char other[3] = {1, 2, 4};

    TEST_ASSERT_EQUAL_UINT64(tda_hash_bytes(a, 3), tda_hash_bytes(same, 3));
    TEST_ASSERT_NOT_EQUAL_UINT64(tda_hash_bytes(a, 3), tda_hash_bytes(other, 3));

    // the length is part of the value: a prefix is a different range, not the same one
    TEST_ASSERT_NOT_EQUAL_UINT64(tda_hash_bytes(a, 2), tda_hash_bytes(a, 3));
}

// an empty range is legal and must not touch the pointer
static void test_hash_bytes_takes_an_empty_range() {
    constexpr unsigned char a[1] = {7};

    TEST_ASSERT_EQUAL_UINT64(tda_hash_bytes(nullptr, 0), tda_hash_bytes(a, 0));
    TEST_ASSERT_NOT_EQUAL_UINT64(tda_hash_bytes(a, 0), tda_hash_bytes(a, 1));
}

// the string form is this one over strlen, which is also why "" and an empty range agree
static void test_hash_cstr_is_bytes_over_the_length() {
    const char *str = "hello";

    TEST_ASSERT_EQUAL_UINT64(tda_hash_bytes(str, 5), tda_hash_cstr(&str, nullptr));

    const char *empty = "";
    TEST_ASSERT_EQUAL_UINT64(tda_hash_bytes(nullptr, 0), tda_hash_cstr(&empty, nullptr));
}

/* ========== cstr ========== */

static void test_hash_cstr_follows_content_not_address() {
    constexpr char first[] = "hello";
    constexpr char second[] = "hello";
    const char *a = first, *b = second, *c = "hellp";

    TEST_ASSERT_NOT_EQUAL(a, b); // two distinct buffers holding the same text
    TEST_ASSERT_TRUE(tda_eq_cstr(&a, &b, nullptr));
    TEST_ASSERT_EQUAL_UINT64(tda_hash_cstr(&a, nullptr), tda_hash_cstr(&b, nullptr));
    TEST_ASSERT_NOT_EQUAL_UINT64(tda_hash_cstr(&a, nullptr), tda_hash_cstr(&c, nullptr));
}

// null is a value here, not a broken precondition: the operand is the pointer to it
static void test_hash_cstr_keeps_null_apart_from_empty() {
    constexpr char *null_str = nullptr;
    const char *empty = "";

    TEST_ASSERT_FALSE(tda_eq_cstr(&null_str, &empty, nullptr));
    TEST_ASSERT_NOT_EQUAL_UINT64(tda_hash_cstr(&null_str, nullptr), tda_hash_cstr(&empty, nullptr));
}

// a one-character difference must change the result: FNV-1a mixes each byte in
static void test_hash_cstr_separates_neighbours() {
    const char *strs[4] = {"ab", "ba", "abc", "abd"};
    tda_Hash hashes[4];

    for (size_t i = 0; i < 4; ++i) {
        hashes[i] = tda_hash_cstr(&strs[i], nullptr);
    }

    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = i + 1; j < 4; ++j) {
            TEST_ASSERT_NOT_EQUAL_UINT64(hashes[i], hashes[j]);
        }
    }
}

/* ========== combine ========== */

// why combine and not xor: a struct with its fields swapped is a different struct, and
// must hash differently
static void test_hash_combine_depends_on_order() {
    TEST_ASSERT_NOT_EQUAL_UINT64(tda_hash_combine(1, 2), tda_hash_combine(2, 1));
}

// the same trap as in the mixer, one level up: two zero fields are an ordinary struct,
// and folding them must not land on bucket zero
static void test_hash_combine_does_not_leave_zero_at_zero() {
    TEST_ASSERT_NOT_EQUAL_UINT64(0, tda_hash_combine(0, 0));
}

// the intended use of combine: the hash of a struct is its fields' hashes folded
// together, exactly as its comparator delegates to its fields' comparators
typedef struct {
    int32_t id;
    const char *name;
} Rec;

static tda_Hash hash_rec(const void *key, void *) {
    const Rec *rec = key;

    return tda_hash_combine(tda_hash_i32(&rec->id, nullptr), tda_hash_cstr(&rec->name, nullptr));
}

static void test_hash_combine_builds_a_struct_hasher() {
    const Rec a = {7, "alpha"};
    const Rec same = {7, "alpha"};
    const Rec other_id = {8, "alpha"};
    const Rec other_name = {7, "beta"};

    TEST_ASSERT_EQUAL_UINT64(hash_rec(&a, nullptr), hash_rec(&same, nullptr));
    TEST_ASSERT_NOT_EQUAL_UINT64(hash_rec(&a, nullptr), hash_rec(&other_id, nullptr));
    TEST_ASSERT_NOT_EQUAL_UINT64(hash_rec(&a, nullptr), hash_rec(&other_name, nullptr));

    // and it is a tda_Hasher like any other
    const tda_Hasher hasher = hash_rec;
    TEST_ASSERT_EQUAL_UINT64(hash_rec(&a, nullptr), hasher(&a, nullptr));
}

/* ========== through the type ========== */

// each of these is meant to be passed as a tda_Hasher; passing them here also checks
// that the signatures match the typedef
static void test_hashers_travel_as_the_typedef() {
    const tda_Hasher hashers[3] = {tda_hash_i32, tda_hash_f64, tda_hash_cstr};

    constexpr int32_t i = 5;
    constexpr double d = 5.0;
    const char *s = "5";
    const void *operands[3] = {&i, &d, &s};

    for (size_t k = 0; k < 3; ++k) {
        TEST_ASSERT_NOT_EQUAL_UINT64(0, hashers[k](operands[k], nullptr));
    }
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_hash_agrees_with_equality);
    RUN_TEST(test_hash_agrees_with_equality_for_every_width);
    RUN_TEST(test_hash_folds_the_two_zeroes_together);
    RUN_TEST(test_hash_folds_every_nan_together);

    RUN_TEST(test_hash_does_not_leave_zero_at_zero);
    RUN_TEST(test_hash_spreads_keys_that_share_their_low_bits);

    RUN_TEST(test_hash_char_reads_the_byte_not_the_sign);

    RUN_TEST(test_hash_bytes_follows_the_content);
    RUN_TEST(test_hash_bytes_takes_an_empty_range);
    RUN_TEST(test_hash_cstr_is_bytes_over_the_length);

    RUN_TEST(test_hash_cstr_follows_content_not_address);
    RUN_TEST(test_hash_cstr_keeps_null_apart_from_empty);
    RUN_TEST(test_hash_cstr_separates_neighbours);

    RUN_TEST(test_hash_combine_depends_on_order);
    RUN_TEST(test_hash_combine_does_not_leave_zero_at_zero);
    RUN_TEST(test_hash_combine_builds_a_struct_hasher);

    RUN_TEST(test_hashers_travel_as_the_typedef);

    return UNITY_END();
}
