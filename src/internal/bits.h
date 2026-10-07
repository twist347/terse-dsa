#pragma once

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

// <stdbit.h> ships with the C library, not the compiler, and some still lack it (macOS,
// the UCRT); the builtins stand in there
#if __has_include(<stdbit.h>)
    #include <stdbit.h>
    #define TDA_HAS_STDBIT
#endif

/* ========== bit queries ========== */

[[nodiscard]]
static inline size_t tda_bit_count(uint64_t word) {
#ifdef TDA_HAS_STDBIT
    return stdc_count_ones(word);
#else
    return (size_t) __builtin_popcountg(word);
#endif
}

// index of the lowest set bit; a word with no bits at all has none
[[nodiscard]]
static inline size_t tda_bit_ctz(uint64_t word) {
    assert(word != 0);

#ifdef TDA_HAS_STDBIT
    return stdc_trailing_zeros(word);
#else
    return (size_t) __builtin_ctzg(word);
#endif
}

// zero bits above the highest set bit; a word with no bits at all has none
[[nodiscard]]
static inline size_t tda_bit_clz(uint64_t word) {
    assert(word != 0);

#ifdef TDA_HAS_STDBIT
    return stdc_leading_zeros(word);
#else
    return (size_t) __builtin_clzg(word);
#endif
}

[[nodiscard]]
static inline size_t tda_bit_log2_floor(size_t n) {
    assert(n > 0);

#ifdef TDA_HAS_STDBIT
    return stdc_bit_width(n) - 1;
#else
    return sizeof(size_t) * CHAR_BIT - 1 - (size_t) __builtin_clzg(n);
#endif
}
