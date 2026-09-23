#include "tda/core/rng.h"

#include "tda/core/check.h"

#include <assert.h>
#include <limits.h>
#include <math.h>

/* ========== internals ========== */

// SplitMix64, the seeder xoshiro's authors point at: it turns one word into state that is
// already well spread, which xoshiro itself does not do — seeded with a small number by
// hand it needs thousands of draws to shake the zeroes out.
static constexpr uint64_t SPLITMIX_GAMMA = UINT64_C(0x9e3779b97f4a7c15);
static constexpr uint64_t SPLITMIX_C1 = UINT64_C(0xbf58476d1ce4e5b9);
static constexpr uint64_t SPLITMIX_C2 = UINT64_C(0x94d049bb133111eb);

// the top half of a 64x64 product is what puts a draw into a range without a division.
// C23 spells that width without an extension, and the assert is what turns a platform
// that cannot into a build error rather than a quietly different generator.
static_assert(BITINT_MAXWIDTH >= 128, "tda_rng_u64_max needs a 128-bit product");

typedef unsigned _BitInt(128) u128;

[[nodiscard]]
static uint64_t splitmix64(uint64_t *state);

[[nodiscard]]
static uint64_t rotl64(uint64_t val, int count);

[[nodiscard]]
static uint64_t next_u64(tda_Rng *self);

/* ========== seeding ========== */

tda_Rng tda_rng_from_seed(uint64_t seed) {
    tda_Rng obj;
    uint64_t state = seed;

    // SplitMix64 is a bijection over four states that differ, so the four words differ
    // too and at most one of them can come out zero — the all-zero state xoshiro must
    // never hold is unreachable from here, for any seed.
    for (size_t i = 0; i < 4; ++i) {
        obj.s[i] = splitmix64(&state);
    }

    return obj;
}

/* ========== raw draws ========== */

uint32_t tda_rng_u32(tda_Rng *self) {
    assert(self);

    return (uint32_t) (next_u64(self) >> 32);
}

uint64_t tda_rng_u64(tda_Rng *self) {
    assert(self);

    return next_u64(self);
}

/* ========== bounded ints ========== */

uint32_t tda_rng_u32_max(tda_Rng *self, uint32_t max) {
    assert(self);

    if (max == UINT32_MAX) {
        return (uint32_t) (next_u64(self) >> 32); // range would overflow, and need not
    }

    // Lemire's nearly divisionless method. One multiply carries the draw into
    // [0, range) in the top half of the product; the bottom half says whether this draw
    // fell in the slot that the range does not divide evenly — the one that would be
    // handed out once too often. Only that slot costs a division, and it comes up
    // range/2^32 of the time.
    //
    // The modulo everyone writes first skips the multiply and takes the bias; masking
    // down to a power of two takes no division at all, but throws away up to half the
    // draws, and an unpredictable branch costs more than the multiply saves.
    const uint32_t range = max + 1;
    uint64_t wide = (uint64_t) (uint32_t) (next_u64(self) >> 32) * range;
    uint32_t low = (uint32_t) wide;

    if (low < range) {
        const uint32_t threshold = (uint32_t) (-range) % range; // 2^32 mod range

        while (low < threshold) {
            wide = (uint64_t) (uint32_t) (next_u64(self) >> 32) * range;
            low = (uint32_t) wide;
        }
    }

    return (uint32_t) (wide >> 32);
}

uint64_t tda_rng_u64_max(tda_Rng *self, uint64_t max) {
    assert(self);

    if (max == UINT64_MAX) {
        return next_u64(self);
    }

    // the same method one width up, where the product is the reason for the 128-bit type
    const uint64_t range = max + 1;
    u128 wide = (u128) next_u64(self) * range;
    uint64_t low = (uint64_t) wide;

    if (low < range) {
        const uint64_t threshold = (uint64_t) (-range) % range; // 2^64 mod range

        while (low < threshold) {
            wide = (u128) next_u64(self) * range;
            low = (uint64_t) wide;
        }
    }

    return (uint64_t) (wide >> 64);
}

size_t tda_rng_idx(tda_Rng *self, size_t len) {
    assert(self);
    TDA_EXPECT(len > 0);

    // len - 1 is the last index, and it fits a uint64_t on every platform size_t does
    return (size_t) tda_rng_u64_max(self, (uint64_t) len - 1);
}

int32_t tda_rng_i32_range(tda_Rng *self, int32_t lo, int32_t hi) {
    assert(self);
    assert(lo <= hi);

    // the width is taken in unsigned, where INT32_MIN..INT32_MAX still fits and nothing
    // overflows; C23 defines the conversion back as the two's-complement wrap this needs
    const uint32_t width = (uint32_t) hi - (uint32_t) lo;

    return (int32_t) ((uint32_t) lo + tda_rng_u32_max(self, width));
}

int64_t tda_rng_i64_range(tda_Rng *self, int64_t lo, int64_t hi) {
    assert(self);
    assert(lo <= hi);

    const uint64_t width = (uint64_t) hi - (uint64_t) lo;

    return (int64_t) ((uint64_t) lo + tda_rng_u64_max(self, width));
}

/* ========== floats ========== */

float tda_rng_f32(tda_Rng *self) {
    assert(self);

    // 24 bits is what a float holds whole, so every draw names a distinct multiple of
    // 2^-24 and the scaling is exact
    return (float) ((uint32_t) (next_u64(self) >> 32) >> 8) * 0x1.0p-24f;
}

double tda_rng_f64(tda_Rng *self) {
    assert(self);

    // and 53 for a double. Taking the top bits works on any generator; here the bottom
    // ones would have done as well.
    return (double) (next_u64(self) >> 11) * 0x1.0p-53;
}

float tda_rng_f32_range(tda_Rng *self, float lo, float hi) {
    assert(self);
    assert(isfinite(lo));
    assert(isfinite(hi));
    assert(lo <= hi);
    assert(isfinite(hi - lo)); // FLT_MAX apart is a width no float can name

    const float val = lo + (hi - lo) * tda_rng_f32(self);

    // the multiply rounds, and rounding up at the top of the range would hand back 'hi'
    // itself; nextafter walks it back to the largest value below. lo == hi is the empty
    // range, where nextafter(hi, lo) is lo — the only answer there is.
    return val < hi ? val : nextafterf(hi, lo);
}

double tda_rng_f64_range(tda_Rng *self, double lo, double hi) {
    assert(self);
    assert(isfinite(lo));
    assert(isfinite(hi));
    assert(lo <= hi);
    assert(isfinite(hi - lo));

    const double val = lo + (hi - lo) * tda_rng_f64(self);

    return val < hi ? val : nextafter(hi, lo);
}

/* ========== bool ========== */

bool tda_rng_bool(tda_Rng *self) {
    assert(self);

    return (next_u64(self) >> 63) != 0;
}

/* ========== internals ========== */

static uint64_t splitmix64(uint64_t *state) {
    uint64_t val = (*state += SPLITMIX_GAMMA);

    val = (val ^ (val >> 30)) * SPLITMIX_C1;
    val = (val ^ (val >> 27)) * SPLITMIX_C2;

    return val ^ (val >> 31);
}

static uint64_t rotl64(uint64_t val, int count) {
    assert(count > 0 && count < 64); // a shift by 64 is undefined, not a no-op

    return (val << count) | (val >> (64 - count));
}

static uint64_t next_u64(tda_Rng *self) {
    // xoshiro256++. The state advances by a linear step — shift, xor, rotate — which is
    // what gives the period and nothing else; the ++ on the front, rotate-add over two
    // words, is the scrambler that makes the output look random at all. The plain
    // xoshiro256 underneath fails its low bits, and this is what fixes them.
    const uint64_t result = rotl64(self->s[0] + self->s[3], 23) + self->s[0];
    const uint64_t tmp = self->s[1] << 17;

    self->s[2] ^= self->s[0];
    self->s[3] ^= self->s[1];
    self->s[1] ^= self->s[2];
    self->s[0] ^= self->s[3];
    self->s[2] ^= tmp;
    self->s[3] = rotl64(self->s[3], 45);

    return result;
}
