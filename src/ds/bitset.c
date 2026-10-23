#include "tda/ds/bitset.h"

#include "tda/core/check.h"
#include "tda/core/util.h"

#include "internal/bits.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

/* ========== internals ========== */

static constexpr size_t WORD_BITS = 64;

// the words are uint64_t, sized by sizeof elsewhere; the two spellings must agree
static_assert(WORD_BITS == sizeof(uint64_t) * CHAR_BIT);

// the inline bit ops in the header divide by 64 themselves
static_assert(WORD_BITS == 64);

// The bits above the last live one are not part of the set, and every read here counts on
// them being clear: count sums whole words, any tests whole words, find_next trusts the
// first one bit it sees. Only the ops that touch a whole word at once can dirty them,
// set_all and flip_all, and both end with clear_tail. The pairwise ops cannot: | & ^ and
// &~ of two clear tails is a clear tail.
#define ASSERT_BITSET(b)                                                                         \
    (assert(b),                                                                                  \
    assert((b)->nwords_ == words_for((b)->nbits_)),                                              \
    assert((b)->nwords_ > 0 || !(b)->words_),                                                    \
    assert((b)->nwords_ == 0 || (b)->words_),                                                    \
    assert((b)->nwords_ == 0 || ((b)->words_[(b)->nwords_ - 1] & ~tail_mask((b)->nbits_)) == 0), \
    assert((b)->al_))

[[nodiscard]]
static size_t words_for(size_t nbits);

/// how many bytes the words take, the size the block was asked for
[[nodiscard]]
static size_t words_bytes(const tda_BitSet *self);

[[nodiscard]]
static size_t word_of(size_t idx);

/// the live bits of the last word; all of them when nbits lands on a word boundary
[[nodiscard]]
static uint64_t tail_mask(size_t nbits);

static void clear_tail(tda_BitSet *self);

/// the word with the bits below 'from' masked off; 'from' must be inside the set
[[nodiscard]]
static uint64_t word_from(uint64_t word, size_t from);

/// where the lowest one bit of 'word' sits, given that 'word' is word number 'w'
[[nodiscard]]
static size_t idx_of_first_one(size_t w, uint64_t word);

/// the word with the bits above 'upto' masked off; 'upto' must be inside the set
[[nodiscard]]
static uint64_t word_upto(uint64_t word, size_t upto);

/// where the highest one bit of 'word' sits, given that 'word' is word number 'w'
[[nodiscard]]
static size_t idx_of_last_one(size_t w, uint64_t word);

/// find_next over 'self' or its complement. Walking up, it reaches the bits above the
/// universe, which a complement turns to ones, so a hit past nbits is a miss
[[nodiscard]]
static bool find_next_impl(const tda_BitSet *self, bool clear, size_t from, size_t *out_idx);

/// find_prev over 'self' or its complement. Starting inside the universe and walking
/// down, it never meets the bits above it, which a complement would turn to ones
[[nodiscard]]
static bool find_prev_impl(const tda_BitSet *self, bool clear, size_t from, size_t *out_idx);

/* ========== lifetime ========== */

tda_Status tda_bitset_init_len(size_t nbits, tda_Al *al, tda_BitSet *out) {
    assert(al);
    assert(out);

    tda_BitSet obj = {.nbits_ = nbits, .nwords_ = words_for(nbits), .al_ = al};
    if (obj.nwords_ > 0) {
        obj.words_ = TDA_ALLOC_ZEROED(uint64_t, al, obj.nwords_);
        if (!obj.words_) {
            return TDA_STATUS_ERR_NO_MEM;
        }
    }

    *out = obj;

    ASSERT_BITSET(out);

    return TDA_STATUS_OK;
}

void tda_bitset_deinit(tda_BitSet *self) {
    assert(self);

    // the inert state, never initialized or already released
    if (!self->al_) {
        assert(!self->words_);
        return;
    }

    ASSERT_BITSET(self);

    TDA_DEALLOC(uint64_t, self->al_, self->words_, self->nwords_);
    *self = (tda_BitSet){};
}

/* ========== copy ========== */

tda_Status tda_bitset_copy(const tda_BitSet *self, tda_BitSet *out) {
    ASSERT_BITSET(self);

    return tda_bitset_copy_with(self, self->al_, out);
}

tda_Status tda_bitset_copy_with(const tda_BitSet *self, tda_Al *al, tda_BitSet *out) {
    ASSERT_BITSET(self);
    assert(al);
    assert(out);

    tda_BitSet copy;
    const tda_Status st = tda_bitset_init_len(self->nbits_, al, &copy);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    if (self->nwords_ > 0) {
        memcpy(copy.words_, self->words_, words_bytes(self));
    }

    *out = copy;

    return TDA_STATUS_OK;
}

tda_Status tda_bitset_copy_assign(tda_BitSet *self, const tda_BitSet *other) {
    ASSERT_BITSET(self);
    ASSERT_BITSET(other);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    if (self->nwords_ != other->nwords_) {
        uint64_t *new_words = TDA_REALLOC(uint64_t, self->al_, self->words_, self->nwords_, other->nwords_);
        // a new_size of 0 hands the block back and answers null, which is not a failure
        if (other->nwords_ > 0 && !new_words) {
            return TDA_STATUS_ERR_NO_MEM;
        }

        self->words_ = new_words;
        self->nwords_ = other->nwords_;
    }

    self->nbits_ = other->nbits_;

    if (other->nwords_ > 0) {
        memcpy(self->words_, other->words_, words_bytes(other));
    }

    ASSERT_BITSET(self);

    return TDA_STATUS_OK;
}

void tda_bitset_move_assign(tda_BitSet *self, tda_BitSet *other) {
    ASSERT_BITSET(self);
    ASSERT_BITSET(other);

    if (self == other) {
        return;
    }

    // the allocator goes along with the words, and 'other' is left empty on it
    tda_bitset_deinit(self);
    *self = *other;
    *other = (tda_BitSet){.al_ = self->al_};

    ASSERT_BITSET(self);
    ASSERT_BITSET(other);
}

void tda_bitset_swap(tda_BitSet *self, tda_BitSet *other) {
    ASSERT_BITSET(self);
    ASSERT_BITSET(other);

    if (self == other) {
        return;
    }

    TDA_SWAP(*self, *other);

    ASSERT_BITSET(self);
    ASSERT_BITSET(other);
}

/* ========== one bit ========== */

void tda_bitset_set_to(tda_BitSet *self, size_t idx, bool val) {
    if (val) {
        tda_bitset_set(self, idx);
    } else {
        tda_bitset_reset(self, idx);
    }
}

/* ========== all bits ========== */

void tda_bitset_set_all(tda_BitSet *self) {
    ASSERT_BITSET(self);

    // a byte of ones is a word of ones, and memset over no bytes wants a real pointer
    if (self->nwords_ > 0) {
        memset(self->words_, 0xFF, words_bytes(self));
    }

    clear_tail(self);
}

void tda_bitset_reset_all(tda_BitSet *self) {
    ASSERT_BITSET(self);

    if (self->nwords_ > 0) {
        memset(self->words_, 0, words_bytes(self));
    }
}

void tda_bitset_flip_all(tda_BitSet *self) {
    ASSERT_BITSET(self);

    for (size_t w = 0; w < self->nwords_; ++w) {
        self->words_[w] = ~self->words_[w];
    }

    clear_tail(self);
}

/* ========== info ========== */

size_t tda_bitset_count(const tda_BitSet *self) {
    ASSERT_BITSET(self);

    size_t count = 0;
    for (size_t w = 0; w < self->nwords_; ++w) {
        count += tda_bit_count(self->words_[w]);
    }

    return count;
}

bool tda_bitset_any(const tda_BitSet *self) {
    ASSERT_BITSET(self);

    for (size_t w = 0; w < self->nwords_; ++w) {
        if (self->words_[w] != 0) {
            return true;
        }
    }

    return false;
}

bool tda_bitset_all(const tda_BitSet *self) {
    ASSERT_BITSET(self);

    for (size_t w = 0; w < self->nwords_; ++w) {
        const uint64_t want = w + 1 == self->nwords_ ? tail_mask(self->nbits_) : ~UINT64_C(0);
        if (self->words_[w] != want) {
            return false;
        }
    }

    return true;
}

bool tda_bitset_none(const tda_BitSet *self) {
    ASSERT_BITSET(self);

    return !tda_bitset_any(self);
}

/* ========== scan ========== */

bool tda_bitset_find_next(const tda_BitSet *self, size_t from, size_t *out_idx) {
    ASSERT_BITSET(self);
    assert(out_idx);

    return find_next_impl(self, false, from, out_idx);
}

bool tda_bitset_find_next_clear(const tda_BitSet *self, size_t from, size_t *out_idx) {
    ASSERT_BITSET(self);
    assert(out_idx);

    return find_next_impl(self, true, from, out_idx);
}

bool tda_bitset_find_prev(const tda_BitSet *self, size_t from, size_t *out_idx) {
    ASSERT_BITSET(self);
    assert(out_idx);

    return find_prev_impl(self, false, from, out_idx);
}

bool tda_bitset_find_prev_clear(const tda_BitSet *self, size_t from, size_t *out_idx) {
    ASSERT_BITSET(self);
    assert(out_idx);

    return find_prev_impl(self, true, from, out_idx);
}

/* ========== set ops ========== */

bool tda_bitset_eq(const tda_BitSet *a, const tda_BitSet *b) {
    ASSERT_BITSET(a);
    ASSERT_BITSET(b);

    if (a == b) {
        return true;
    }

    if (a->nbits_ != b->nbits_) {
        return false;
    }

    return a->nwords_ == 0 || memcmp(a->words_, b->words_, words_bytes(a)) == 0;
}

void tda_bitset_union(tda_BitSet *self, const tda_BitSet *other) {
    ASSERT_BITSET(self);
    ASSERT_BITSET(other);
    TDA_EXPECT(self->nbits_ == other->nbits_);

    for (size_t w = 0; w < self->nwords_; ++w) {
        self->words_[w] |= other->words_[w];
    }
}

void tda_bitset_intersection(tda_BitSet *self, const tda_BitSet *other) {
    ASSERT_BITSET(self);
    ASSERT_BITSET(other);
    TDA_EXPECT(self->nbits_ == other->nbits_);

    for (size_t w = 0; w < self->nwords_; ++w) {
        self->words_[w] &= other->words_[w];
    }
}

void tda_bitset_difference(tda_BitSet *self, const tda_BitSet *other) {
    ASSERT_BITSET(self);
    ASSERT_BITSET(other);
    TDA_EXPECT(self->nbits_ == other->nbits_);

    for (size_t w = 0; w < self->nwords_; ++w) {
        self->words_[w] &= ~other->words_[w];
    }
}

void tda_bitset_symmetric_difference(tda_BitSet *self, const tda_BitSet *other) {
    ASSERT_BITSET(self);
    ASSERT_BITSET(other);
    TDA_EXPECT(self->nbits_ == other->nbits_);

    for (size_t w = 0; w < self->nwords_; ++w) {
        self->words_[w] ^= other->words_[w];
    }
}

bool tda_bitset_is_subset(const tda_BitSet *self, const tda_BitSet *other) {
    ASSERT_BITSET(self);
    ASSERT_BITSET(other);
    TDA_EXPECT(self->nbits_ == other->nbits_);

    for (size_t w = 0; w < self->nwords_; ++w) {
        if ((self->words_[w] & ~other->words_[w]) != 0) {
            return false;
        }
    }

    return true;
}

bool tda_bitset_intersects(const tda_BitSet *self, const tda_BitSet *other) {
    ASSERT_BITSET(self);
    ASSERT_BITSET(other);
    TDA_EXPECT(self->nbits_ == other->nbits_);

    for (size_t w = 0; w < self->nwords_; ++w) {
        if ((self->words_[w] & other->words_[w]) != 0) {
            return true;
        }
    }

    return false;
}

/* ========== print ========== */

void tda_bitset_fprint(const tda_BitSet *self, FILE *stream) {
    ASSERT_BITSET(self);
    assert(stream);

    fputc('{', stream);

    bool first = true;
    size_t idx;
    for (size_t from = 0; tda_bitset_find_next(self, from, &idx); from = idx + 1) {
        if (!first) {
            fputs(", ", stream);
        }
        first = false;
        fprintf(stream, "%zu", idx);
    }

    fputs("}\n", stream);
}

void tda_bitset_print(const tda_BitSet *self) {
    tda_bitset_fprint(self, stdout);
}

/* ========== internals ========== */

static size_t words_for(size_t nbits) {
    // not (nbits + 63) / 64: that addition overflows for an nbits near SIZE_MAX
    return nbits / WORD_BITS + (nbits % WORD_BITS != 0);
}

static size_t words_bytes(const tda_BitSet *self) {
    assert(self);

    return self->nwords_ * sizeof(uint64_t);
}

static size_t word_of(size_t idx) {
    return idx / WORD_BITS;
}

static uint64_t tail_mask(size_t nbits) {
    const size_t rem = nbits % WORD_BITS;

    // rem == 0 means the last word is full; writing it as a shift would be a shift by 64
    return rem == 0 ? ~UINT64_C(0) : (UINT64_C(1) << rem) - 1;
}

static void clear_tail(tda_BitSet *self) {
    assert(self);

    if (self->nwords_ > 0) {
        self->words_[self->nwords_ - 1] &= tail_mask(self->nbits_);
    }
}

static uint64_t word_from(uint64_t word, size_t from) {
    return word & (~UINT64_C(0) << (from % WORD_BITS));
}

static size_t idx_of_first_one(size_t w, uint64_t word) {
    return w * WORD_BITS + tda_bit_ctz(word);
}

static uint64_t word_upto(uint64_t word, size_t upto) {
    return word & (~UINT64_C(0) >> (WORD_BITS - 1 - upto % WORD_BITS));
}

static size_t idx_of_last_one(size_t w, uint64_t word) {
    return w * WORD_BITS + (WORD_BITS - 1 - tda_bit_clz(word));
}

static bool find_next_impl(const tda_BitSet *self, bool clear, size_t from, size_t *out_idx) {
    if (from >= self->nbits_) {
        return false;
    }

    const uint64_t flip = clear ? ~UINT64_C(0) : 0;
    size_t w = word_of(from);
    uint64_t word = word_from(self->words_[w] ^ flip, from);

    for (;;) {
        if (word != 0) {
            // with the tail kept clear, only a complement can find a one past nbits
            const size_t idx = idx_of_first_one(w, word);
            if (idx >= self->nbits_) {
                return false;
            }

            *out_idx = idx;
            return true;
        }

        if (++w == self->nwords_) {
            return false;
        }

        word = self->words_[w] ^ flip;
    }
}

static bool find_prev_impl(const tda_BitSet *self, bool clear, size_t from, size_t *out_idx) {
    if (self->nbits_ == 0) {
        return false;
    }

    if (from >= self->nbits_) {
        from = self->nbits_ - 1;
    }

    const uint64_t flip = clear ? ~UINT64_C(0) : 0;
    size_t w = word_of(from);
    uint64_t word = word_upto(self->words_[w] ^ flip, from);

    for (;;) {
        if (word != 0) {
            *out_idx = idx_of_last_one(w, word);
            return true;
        }

        if (w == 0) {
            return false;
        }

        word = self->words_[--w] ^ flip;
    }
}
