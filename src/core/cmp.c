#include "tda/core/cmp.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ========== internals ========== */

// The value forms hold the whole implementation but are not exported: the algorithms
// take comparators, and one form per meaning is enough. To compare two values, call the
// comparator with their addresses.

#define DEFINE_CMP_INT(name, T)       \
    static int cmp_##name(T a, T b) { \
        return (a > b) - (a < b);     \
    }

#define DEFINE_CMP_FLOAT(name, T)     \
    static int cmp_##name(T a, T b) { \
        const bool a_nan = isnan(a);  \
        const bool b_nan = isnan(b);  \
        if (a_nan || b_nan) {         \
            if (a_nan && b_nan) {     \
                return 0;             \
            }                         \
            return a_nan ? 1 : -1;    \
        }                             \
        return (a > b) - (a < b);     \
    }

#define DEFINE_EQ(name, T)             \
    static bool eq_##name(T a, T b) {  \
        return cmp_##name(a, b) == 0;  \
    }

#define DEFINE_CMP(name, T)                                    \
    int tda_cmp_##name(const void *lhs, const void *rhs) {     \
        assert(lhs);                                           \
        assert(rhs);                                           \
        return cmp_##name(*(const T *) lhs, *(const T *) rhs); \
    }

// no T: swapping the operands of the ascending comparator needs no type, so cstr can use
// this one despite its pointer-to-pointer operands
#define DEFINE_CMP_DESC(name)                                   \
    int tda_cmp_desc_##name(const void *lhs, const void *rhs) { \
        return tda_cmp_##name(rhs, lhs);                        \
    }

#define DEFINE_EQ_FORM(name, T)                               \
    bool tda_eq_##name(const void *lhs, const void *rhs) {    \
        assert(lhs);                                          \
        assert(rhs);                                          \
        return eq_##name(*(const T *) lhs, *(const T *) rhs); \
    }

#define DEFINE_INT_ALL(name, T) \
    DEFINE_CMP_INT(name, T)     \
    DEFINE_EQ(name, T)          \
    DEFINE_CMP(name, T)         \
    DEFINE_CMP_DESC(name)       \
    DEFINE_EQ_FORM(name, T)

#define DEFINE_FLOAT_ALL(name, T) \
    DEFINE_CMP_FLOAT(name, T)     \
    DEFINE_EQ(name, T)            \
    DEFINE_CMP(name, T)           \
    DEFINE_CMP_DESC(name)         \
    DEFINE_EQ_FORM(name, T)

/* ========== instantiation ========== */

DEFINE_INT_ALL(i8, int8_t)
DEFINE_INT_ALL(i16, int16_t)
DEFINE_INT_ALL(i32, int32_t)
DEFINE_INT_ALL(i64, int64_t)

DEFINE_INT_ALL(u8, uint8_t)
DEFINE_INT_ALL(u16, uint16_t)
DEFINE_INT_ALL(u32, uint32_t)
DEFINE_INT_ALL(u64, uint64_t)

DEFINE_INT_ALL(size, size_t)
DEFINE_INT_ALL(ptrdiff, ptrdiff_t)

// char is a third type, distinct from signed and unsigned char, and its signedness is
// up to the target; (a > b) - (a < b) is right either way
DEFINE_INT_ALL(char, char)

DEFINE_FLOAT_ALL(f32, float)
DEFINE_FLOAT_ALL(f64, double)

/* ========== cstr ========== */

static int cmp_cstr(const char *a, const char *b) {
    if (a == b) {
        return 0; // both null, or the same string
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }

    const int c = strcmp(a, b);

    return (c > 0) - (c < 0); // strcmp only promises a sign, so narrow it to -1/0/+1
}

DEFINE_EQ(cstr, const char *)

// the operand is a pointer to a pointer, so the cast needs a star that DEFINE_CMP
// cannot spell; the other two forms come from the shared macros
int tda_cmp_cstr(const void *lhs, const void *rhs) {
    assert(lhs);
    assert(rhs);

    return cmp_cstr(*(const char *const *) lhs, *(const char *const *) rhs);
}

bool tda_eq_cstr(const void *lhs, const void *rhs) {
    assert(lhs);
    assert(rhs);

    return eq_cstr(*(const char *const *) lhs, *(const char *const *) rhs);
}

DEFINE_CMP_DESC(cstr)

#undef DEFINE_CMP_INT
#undef DEFINE_CMP_FLOAT
#undef DEFINE_EQ
#undef DEFINE_CMP
#undef DEFINE_CMP_DESC
#undef DEFINE_EQ_FORM
#undef DEFINE_INT_ALL
#undef DEFINE_FLOAT_ALL
