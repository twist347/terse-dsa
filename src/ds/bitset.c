#include "tda/ds/bitset.h"

#include "tda/core/check.h"
#include "tda/core/print.h"
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

static constexpr uint64_t WORD_ONES = ~UINT64_C(0);

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
    TDA_EXPECT((b)->al_.vt))

// two bitsets a pairwise op can combine: one universe, so one word count
#define EXPECT_SAME_BITSET(a, b) \
    (TDA_EXPECT((a)->nbits_ == (b)->nbits_))

[[nodiscard]]
static size_t words_for(size_t nbits);

/// how many bytes the words take, the size the block was asked for
[[nodiscard]]
static size_t words_bytes(const tda_BitSet *self);

/// the words of 'src' over those of 'dst', which has as many
static void copy_words(tda_BitSet *dst, const tda_BitSet *src);

/// every byte of the words set to 'byte'
static void fill_words(tda_BitSet *self, unsigned char byte);

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

/// word 'w' of 'self', or of its complement when 'clear'
[[nodiscard]]
static uint64_t word_as_seen(const tda_BitSet *self, size_t w, bool clear);

/// find_next over 'self' or its complement. Walking up, it reaches the bits above the
/// universe, which a complement turns to ones, so a hit past nbits is a miss
[[nodiscard]]
static bool find_next_impl(const tda_BitSet *self, bool clear, size_t from, size_t *out_idx);

/// find_prev over 'self' or its complement. Starting inside the universe and walking
/// down, it never meets the bits above it, which a complement would turn to ones
[[nodiscard]]
static bool find_prev_impl(const tda_BitSet *self, bool clear, size_t from, size_t *out_idx);

/* ========== lifetime ========== */

tda_Status tda_bitset_init_nbits(size_t nbits, tda_Al al, tda_BitSet *out) {
    assert(al.vt);
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

tda_Status tda_bitset_init_len(size_t nbits, tda_Al al, tda_BitSet *out) {
    return tda_bitset_init_nbits(nbits, al, out);
}

tda_Status tda_bitset_from_span(size_t nbits, tda_Span s, tda_Al al, tda_BitSet *out) {
    TDA_SPAN_ASSERT(s);
    assert(al.vt);
    assert(out);
    TDA_EXPECT(tda_layout_eq(TDA_LAYOUT_OF(size_t), s.elem));

    tda_BitSet obj;
    const tda_Status st = tda_bitset_init_nbits(nbits, al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    // tda_bitset_set expects each index below nbits, so a stray one asserts there
    for (size_t i = 0; i < s.len; ++i) {
        tda_bitset_set(&obj, *(const size_t *) tda_span_get(s, i));
    }

    *out = obj;

    ASSERT_BITSET(out);

    return TDA_STATUS_OK;
}

void tda_bitset_deinit(tda_BitSet *self) {
    assert(self);

    // the inert state, never initialized or already released
    if (!self->al_.vt) {
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
    assert(out);

    return tda_bitset_copy_with(self, self->al_, out);
}

tda_Status tda_bitset_copy_with(const tda_BitSet *self, tda_Al al, tda_BitSet *out) {
    ASSERT_BITSET(self);
    assert(al.vt);
    assert(out);

    tda_BitSet copy;
    const tda_Status st = tda_bitset_init_nbits(self->nbits_, al, &copy);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    copy_words(&copy, self);

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

    copy_words(self, other);

    ASSERT_BITSET(self);

    return TDA_STATUS_OK;
}

void tda_bitset_move_assign(tda_BitSet *self, tda_BitSet *other) {
    assert(self);
    ASSERT_BITSET(other);
    // an inert 'self', `= {}`, is a target too: it takes on everything 'other' has
    if (self->al_.vt) {
        ASSERT_BITSET(self);
    }

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
    ASSERT_BITSET(self);
    TDA_EXPECT(idx < self->nbits_);

    if (val) {
        tda_bitset_set(self, idx);
    } else {
        tda_bitset_reset(self, idx);
    }
}

/* ========== all bits ========== */

void tda_bitset_set_all(tda_BitSet *self) {
    ASSERT_BITSET(self);

    // a byte of ones is a word of ones
    fill_words(self, 0xFF);

    clear_tail(self);

    ASSERT_BITSET(self);
}

void tda_bitset_reset_all(tda_BitSet *self) {
    ASSERT_BITSET(self);

    fill_words(self, 0);

    ASSERT_BITSET(self);
}

void tda_bitset_flip_all(tda_BitSet *self) {
    ASSERT_BITSET(self);

    for (size_t w = 0; w < self->nwords_; ++w) {
        self->words_[w] = ~self->words_[w];
    }

    clear_tail(self);

    ASSERT_BITSET(self);
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
        const uint64_t want = w + 1 == self->nwords_ ? tail_mask(self->nbits_) : WORD_ONES;
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
    EXPECT_SAME_BITSET(self, other);

    for (size_t w = 0; w < self->nwords_; ++w) {
        self->words_[w] |= other->words_[w];
    }

    ASSERT_BITSET(self);
}

void tda_bitset_intersection(tda_BitSet *self, const tda_BitSet *other) {
    ASSERT_BITSET(self);
    ASSERT_BITSET(other);
    EXPECT_SAME_BITSET(self, other);

    for (size_t w = 0; w < self->nwords_; ++w) {
        self->words_[w] &= other->words_[w];
    }

    ASSERT_BITSET(self);
}

void tda_bitset_difference(tda_BitSet *self, const tda_BitSet *other) {
    ASSERT_BITSET(self);
    ASSERT_BITSET(other);
    EXPECT_SAME_BITSET(self, other);

    for (size_t w = 0; w < self->nwords_; ++w) {
        self->words_[w] &= ~other->words_[w];
    }

    ASSERT_BITSET(self);
}

void tda_bitset_symmetric_difference(tda_BitSet *self, const tda_BitSet *other) {
    ASSERT_BITSET(self);
    ASSERT_BITSET(other);
    EXPECT_SAME_BITSET(self, other);

    for (size_t w = 0; w < self->nwords_; ++w) {
        self->words_[w] ^= other->words_[w];
    }

    ASSERT_BITSET(self);
}

bool tda_bitset_is_subset(const tda_BitSet *self, const tda_BitSet *other) {
    ASSERT_BITSET(self);
    ASSERT_BITSET(other);
    EXPECT_SAME_BITSET(self, other);

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
    EXPECT_SAME_BITSET(self, other);

    for (size_t w = 0; w < self->nwords_; ++w) {
        if ((self->words_[w] & other->words_[w]) != 0) {
            return true;
        }
    }

    return false;
}

/* ========== print ========== */

int tda_bitset_fprint(const tda_BitSet *self, FILE *stream) {
    ASSERT_BITSET(self);
    assert(stream);

    int rc = fputc('{', stream);

    bool first = true;
    size_t idx;
    for (size_t from = 0; tda_bitset_find_next(self, from, &idx); from = idx + 1) {
        if (!first) {
            rc = tda_fprint_join_(rc, fputs(", ", stream));
        }
        first = false;
        rc = tda_fprint_join_(rc, fprintf(stream, "%zu", idx));
    }

    return tda_fprint_join_(rc, fputc('}', stream));
}

int tda_bitset_print(const tda_BitSet *self) {
    ASSERT_BITSET(self);

    const int rc = tda_bitset_fprint(self, stdout);

    return tda_fprint_join_(rc, fputc('\n', stdout));
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

// both skip an empty set: its words are null, and memcpy and memset want a real pointer
// even for no bytes

static void copy_words(tda_BitSet *dst, const tda_BitSet *src) {
    assert(dst->nwords_ == src->nwords_);

    if (src->nwords_ > 0) {
        memcpy(dst->words_, src->words_, words_bytes(src));
    }
}

static void fill_words(tda_BitSet *self, unsigned char byte) {
    if (self->nwords_ > 0) {
        memset(self->words_, byte, words_bytes(self));
    }
}

static size_t word_of(size_t idx) {
    return idx / WORD_BITS;
}

static uint64_t tail_mask(size_t nbits) {
    const size_t rem = nbits % WORD_BITS;

    // rem == 0 means the last word is full; writing it as a shift would be a shift by 64
    return rem == 0 ? WORD_ONES : (UINT64_C(1) << rem) - 1;
}

static void clear_tail(tda_BitSet *self) {
    assert(self);

    if (self->nwords_ > 0) {
        self->words_[self->nwords_ - 1] &= tail_mask(self->nbits_);
    }
}

static uint64_t word_from(uint64_t word, size_t from) {
    return word & (WORD_ONES << (from % WORD_BITS));
}

static size_t idx_of_first_one(size_t w, uint64_t word) {
    return w * WORD_BITS + tda_bit_ctz(word);
}

static uint64_t word_upto(uint64_t word, size_t upto) {
    return word & (WORD_ONES >> (WORD_BITS - 1 - upto % WORD_BITS));
}

static size_t idx_of_last_one(size_t w, uint64_t word) {
    return w * WORD_BITS + (WORD_BITS - 1 - tda_bit_clz(word));
}

static uint64_t word_as_seen(const tda_BitSet *self, size_t w, bool clear) {
    return self->words_[w] ^ (clear ? WORD_ONES : 0);
}

static bool find_next_impl(const tda_BitSet *self, bool clear, size_t from, size_t *out_idx) {
    if (from >= self->nbits_) {
        return false;
    }

    size_t w = word_of(from);
    uint64_t word = word_from(word_as_seen(self, w, clear), from);

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

        word = word_as_seen(self, w, clear);
    }
}

static bool find_prev_impl(const tda_BitSet *self, bool clear, size_t from, size_t *out_idx) {
    if (self->nbits_ == 0) {
        return false;
    }

    if (from >= self->nbits_) {
        from = self->nbits_ - 1;
    }

    size_t w = word_of(from);
    uint64_t word = word_upto(word_as_seen(self, w, clear), from);

    for (;;) {
        if (word != 0) {
            *out_idx = idx_of_last_one(w, word);
            return true;
        }

        if (w == 0) {
            return false;
        }

        --w;
        word = word_as_seen(self, w, clear);
    }
}
