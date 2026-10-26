#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/check.h"
#include "tda/core/export.h"
#include "tda/core/status.h"
#include "tda/core/util.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/// @file

/// @defgroup ds_bitset ds/bitset
/// @ingroup ds
/// @brief tda_BitSet — a fixed set of indices, one bit each
///
/// The universe is named once: a bitset over 'nbits' holds a subset of 0 .. nbits - 1 and
/// never grows, like ds/arr.
///
/// Membership is a bit, so there is no elem size, no comparator and no hasher: an index
/// is its own key. Compared with ds/hset, a word holds 64 members, and the union of two
/// sets is a loop over words rather than over members.
///
/// Only the constructors, the copies and copy_assign allocate, so nothing else can fail,
/// and an index out of range asserts. The bits above nbits are kept clear, so count, all and
/// the scans never see them.
///
/// @par Example
/// @snippet ds/example_bitset.c build
/// @snippet ds/example_bitset.c bits
/// @snippet ds/example_bitset.c scan
/// @snippet ds/example_bitset.c set_ops
/// @{

/// A fixed set of indices, one bit each. A value the caller holds wherever it likes:
/// tda_bitset_init_len fills it in, tda_bitset_deinit releases its words. `= {}` is the
/// inert state, which tda_bitset_deinit accepts and every other op asserts.
///
/// The fields belong to the bitset: read and change them only through the functions
/// here.
///
/// @warning a copy of the struct is a second owner of the same words: releasing both
///          frees them twice. Pass a pointer, and hand a bitset over with
///          tda_bitset_move_assign.
typedef struct tda_BitSet {
    uint64_t *words_; ///< the bits, 64 to a word; null while nwords_ is 0
    size_t nbits_;    ///< the size of the universe
    size_t nwords_;   ///< how many words hold it
    tda_Al al_;       ///< where the words come from; moves along with the bitset
} tda_BitSet;

/// @name lifetime
/// @{

/// a new bitset over 0 .. nbits - 1 with nothing in it
/// @param nbits the size of the universe; 0 owns no words and holds nothing
/// @param al used for every later allocation
/// @param[out] out the new bitset, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the words cannot be allocated
/// @bigo{n/64}
[[nodiscard]] TDA_API
tda_Status tda_bitset_init_len(size_t nbits, tda_Al al, tda_BitSet *out);

/// releases the words and leaves the bitset inert, `= {}`
/// @param self a bitset, or an inert one, for which this is a no-op
/// @bigo{1}
TDA_API
void tda_bitset_deinit(tda_BitSet *self);

/// @}

/// @name copy
/// @{

/// a new bitset with the same universe and members, on the same allocator
/// @param self the bitset to copy
/// @param[out] out the new bitset, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the words cannot be allocated
/// @bigo{n/64}
[[nodiscard]] TDA_API
tda_Status tda_bitset_copy(const tda_BitSet *self, tda_BitSet *out);

/// a new bitset with the same universe and members, on 'al'
/// @param self the bitset to copy
/// @param al the allocator for the copy
/// @param[out] out the new bitset, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the words cannot be allocated
/// @bigo{n/64}
[[nodiscard]] TDA_API
tda_Status tda_bitset_copy_with(const tda_BitSet *self, tda_Al al, tda_BitSet *out);

/// overwrites 'self' with 'other', resizing its words when the universes differ
/// @param[in,out] self keeps its allocator
/// @param other the bitset to copy from; may be 'self'
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the words cannot be resized; 'self' is unchanged
/// @bigo{n/64}
[[nodiscard]] TDA_API
tda_Status tda_bitset_copy_assign(tda_BitSet *self, const tda_BitSet *other);

/// releases the words of 'self' and hands it everything 'other' holds, universe and
/// allocator included, leaving 'other' an empty universe on that allocator
/// @param[in,out] self the bitset to overwrite
/// @param[in,out] other emptied and still usable; may be 'self'
/// @bigo{1}
TDA_API
void tda_bitset_move_assign(tda_BitSet *self, tda_BitSet *other);

/// exchanges the contents of two bitsets, universes included
/// @param[in,out] self one bitset
/// @param[in,out] other may be 'self'. Each side keeps the allocator its words came
///                      from, so the two may differ
/// @bigo{1}
TDA_API
void tda_bitset_swap(tda_BitSet *self, tda_BitSet *other);

/// @}

/// @name one bit
/// @{

/// whether 'idx' is in the set
/// @param self the bitset
/// @param idx asserts idx < len
/// @return whether the bit is set
/// @bigo{1}
[[nodiscard]]
static inline bool tda_bitset_test(const tda_BitSet *self, size_t idx) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(idx < self->nbits_);

    return (self->words_[idx / 64] & (UINT64_C(1) << idx % 64)) != 0;
}

/// puts 'idx' in the set
/// @param self the bitset
/// @param idx asserts idx < len
/// @bigo{1}
static inline void tda_bitset_set(tda_BitSet *self, size_t idx) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(idx < self->nbits_);

    self->words_[idx / 64] |= (UINT64_C(1) << idx % 64);
}

/// takes 'idx' out of the set
/// @copydetails tda_bitset_set
static inline void tda_bitset_reset(tda_BitSet *self, size_t idx) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(idx < self->nbits_);

    self->words_[idx / 64] &= ~(UINT64_C(1) << idx % 64);
}

/// puts 'idx' in when it is out and out when it is in
/// @copydetails tda_bitset_set
static inline void tda_bitset_flip(tda_BitSet *self, size_t idx) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(idx < self->nbits_);

    self->words_[idx / 64] ^= (UINT64_C(1) << idx % 64);
}

/// sets the bit at 'idx' to 'val'
/// @param self the bitset
/// @param idx asserts idx < len
/// @param val unlike tda_bitset_flip, asking twice changes nothing
/// @bigo{1}
TDA_API
void tda_bitset_set_to(tda_BitSet *self, size_t idx, bool val);

/// @}

/// @name all bits
/// @{

/// puts every index of the universe in the set
/// @param self the bitset
/// @bigo{n/64}
TDA_API
void tda_bitset_set_all(tda_BitSet *self);

/// empties the set
/// @param self the bitset
/// @bigo{n/64}
TDA_API
void tda_bitset_reset_all(tda_BitSet *self);

/// replaces the set with its complement, taken over the universe and nothing above it
/// @param self the bitset
/// @bigo{n/64}
TDA_API
void tda_bitset_flip_all(tda_BitSet *self);

/// @}

/// @name info
/// @{

/// how many indices are in the set
/// @param self the bitset
/// @return the count, 0 when empty
/// @bigo{n/64}
[[nodiscard]] TDA_API
size_t tda_bitset_count(const tda_BitSet *self);

/// whether the set holds anything
/// @param self the bitset
/// @return false for an empty universe
/// @bigo{n/64}
[[nodiscard]] TDA_API
bool tda_bitset_any(const tda_BitSet *self);

/// whether the set is the whole universe
/// @param self the bitset
/// @return true for an empty universe, which holds all of nothing
/// @bigo{n/64}
[[nodiscard]] TDA_API
bool tda_bitset_all(const tda_BitSet *self);

/// whether the set is empty
/// @param self the bitset
/// @return the negation of tda_bitset_any
/// @bigo{n/64}
[[nodiscard]] TDA_API
bool tda_bitset_none(const tda_BitSet *self);

/// the size of the universe
/// @param self the bitset
/// @return nbits; changed only by copy_assign, move_assign and swap
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_bitset_len(const tda_BitSet *self) {
    assert(self);
    assert(self->al_.vt);

    return self->nbits_;
}

/// the allocator the bitset was built with
/// @param self the bitset
/// @return the view it was built with
/// @bigo{1}
[[nodiscard]]
static inline tda_Al tda_bitset_al(const tda_BitSet *self) {
    assert(self);
    assert(self->al_.vt);

    return self->al_;
}

/// @}

/// @name scan
/// @{

/// the first index in the set at or after 'from'
/// @param self the bitset
/// @param from a 'from' past the universe is a miss, not an assert, so a walk needs no
///             bound of its own
/// @param[out] out_idx written only on a hit
/// @return whether there is one
/// @bigo{n/64}
[[nodiscard]] TDA_API
bool tda_bitset_find_next(const tda_BitSet *self, size_t from, size_t *out_idx);

/// the first index outside the set at or after 'from'
/// @param self the bitset
/// @param from a 'from' past the universe is a miss
/// @param[out] out_idx written only on a hit
/// @return false for a full set: the bits above the universe are not indices of it
/// @bigo{n/64}
[[nodiscard]] TDA_API
bool tda_bitset_find_next_clear(const tda_BitSet *self, size_t from, size_t *out_idx);

/// the last index in the set at or before 'from'
/// @param self the bitset
/// @param from a 'from' past the universe starts at its last index, so SIZE_MAX finds the
///             greatest member
/// @param[out] out_idx written only on a hit
/// @return whether there is one
/// @bigo{n/64}
[[nodiscard]] TDA_API
bool tda_bitset_find_prev(const tda_BitSet *self, size_t from, size_t *out_idx);

/// the last index outside the set at or before 'from'
/// @param self the bitset
/// @param from a 'from' past the universe starts at its last index
/// @param[out] out_idx written only on a hit
/// @return false for a full set
/// @bigo{n/64}
[[nodiscard]] TDA_API
bool tda_bitset_find_prev_clear(const tda_BitSet *self, size_t from, size_t *out_idx);

/// @}

/// @name set ops
/// @{

/// whether the two name the same universe and hold the same members
/// @param a one bitset
/// @param b a different len gives false, not an assert
/// @return whether they are equal
/// @bigo{n/64}
[[nodiscard]] TDA_API
bool tda_bitset_eq(const tda_BitSet *a, const tda_BitSet *b);

/// adds every member of 'other' to 'self'
/// @param[in,out] self the bitset written into
/// @param other asserts the same len, as these ops need one universe; may be 'self'
/// @bigo{n/64}
TDA_API
void tda_bitset_union(tda_BitSet *self, const tda_BitSet *other);

/// drops from 'self' everything that is not in 'other'
/// @param[in,out] self the bitset written into
/// @param other asserts the same len
/// @bigo{n/64}
TDA_API
void tda_bitset_intersection(tda_BitSet *self, const tda_BitSet *other);

/// drops from 'self' everything that is in 'other'
/// @param[in,out] self the bitset written into
/// @param other asserts the same len; 'self' == 'other' empties the set
/// @bigo{n/64}
TDA_API
void tda_bitset_difference(tda_BitSet *self, const tda_BitSet *other);

/// keeps in 'self' what is in exactly one of the two
/// @copydetails tda_bitset_difference
TDA_API
void tda_bitset_symmetric_difference(tda_BitSet *self, const tda_BitSet *other);

/// whether every member of 'self' is a member of 'other'
/// @param self the bitset that may be contained
/// @param other asserts the same len
/// @return true when 'self' is empty
/// @bigo{n/64}
[[nodiscard]] TDA_API
bool tda_bitset_is_subset(const tda_BitSet *self, const tda_BitSet *other);

/// whether the two share a member
/// @param self one bitset
/// @param other asserts the same len
/// @return whether the intersection is non-empty
/// @bigo{n/64}
[[nodiscard]] TDA_API
bool tda_bitset_intersects(const tda_BitSet *self, const tda_BitSet *other);

/// @}

/// @name print
/// @{

/// writes the members as {0, 3, 7}, with no newline: the indices, not a row of bits
/// @param self the bitset
/// @param stream where to write
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
TDA_API
int tda_bitset_fprint(const tda_BitSet *self, FILE *stream);

/// tda_bitset_fprint to stdout, and a newline
/// @param self the bitset
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
TDA_API
int tda_bitset_print(const tda_BitSet *self);

/// @}

/// @name macros
/// @{

/// @cond
// the body of the walk; the hidden names come in already unique
#define TDA_BITSET_WALK_(idx, self, bitset, once, from)                             \
    for (const tda_BitSet *bitset = (self), **once = &bitset; once; once = nullptr) \
        for (size_t idx = 0, from = 0;                                              \
             tda_bitset_find_next(bitset, from, &idx);                              \
             from = idx + 1)
/// @endcond

/// walks the indices in the set, smallest first, binding 'idx' to each in turn
/// @param idx the name of the loop variable, a size_t
/// @param self the bitset
/// @note the step past the current index happens after the body, so clearing that index
///       inside the loop is safe, while setting a smaller one no longer changes the walk
/// @bigo{n/64} over the whole walk
#define TDA_BITSET_FOR_EACH(idx, self) \
    TDA_BITSET_WALK_(idx, self, TDA_LINE_NAME_(bitset_), TDA_LINE_NAME_(once_), TDA_LINE_NAME_(from_))

/// @}

/// @}
