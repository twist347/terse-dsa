#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/export.h"
#include "tda/core/status.h"

#include <stddef.h>
#include <stdio.h>

/// @file

/// @defgroup ds_bitset ds/bitset
/// @ingroup ds
/// @brief tda_BitSet — a fixed set of indices, one bit each
///
/// The universe is named once: a bitset over 'nbits' holds a subset of 0 .. nbits - 1 and
/// never grows, the line ds/arr draws against ds/vec.
///
/// Membership is a bit, so there is no elem size, no comparator and no hasher: an index
/// is its own key. That is what this buys over ds/hset — a word holds 64 members, and the
/// union of two sets is a loop over words rather than over members.
///
/// Only the constructors allocate, so nothing else can fail, and an index out of range
/// asserts. The bits above nbits are kept clear, so count, all and the scans never see
/// them.
///
/// @par Example
/// @snippet ds/example_bitset.c build
/// @snippet ds/example_bitset.c bits
/// @snippet ds/example_bitset.c scan
/// @snippet ds/example_bitset.c set ops
/// @{

/// A fixed set of indices, one bit each.
/// An opaque handle: it comes from a constructor and goes back to tda_bitset_drop
typedef struct tda_BitSet tda_BitSet;

/// @name lifetime
/// @{

/// a new bitset over 0 .. nbits - 1 with nothing in it
/// @param nbits the size of the universe; 0 owns no words and holds nothing
/// @param al the allocator, kept for everything after
/// @param[out] out the new bitset, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the words cannot be allocated
/// @bigo{n/64}
[[nodiscard]] TDA_API
tda_Status tda_bitset_new(size_t nbits, tda_Al *al, tda_BitSet **out);

/// releases the words and the bitset through the allocator it was built with
/// @param self null is a no-op, so this is safe on a partly built object
/// @bigo{1}
TDA_API
void tda_bitset_drop(tda_BitSet *self);

/// @}

/// @name copy
/// @{

/// a new bitset with the same universe and members, on the same allocator
/// @param self the bitset to copy
/// @param[out] out the new bitset, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the words cannot be allocated
/// @bigo{n/64}
[[nodiscard]] TDA_API
tda_Status tda_bitset_copy(const tda_BitSet *self, tda_BitSet **out);

/// a new bitset with the same universe and members, on 'al'
/// @param self the bitset to copy
/// @param al where the copy lives; tda_bitset_copy is this one with the allocator of 'self'
/// @param[out] out the new bitset, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the words cannot be allocated
/// @bigo{n/64}
[[nodiscard]] TDA_API
tda_Status tda_bitset_copy_with(const tda_BitSet *self, tda_Al *al, tda_BitSet **out);

/// overwrites 'self' with 'other', resizing its words when the universes differ
/// @param[in,out] self keeps its own allocator; 'self' == 'other' is a no-op
/// @param other the bitset to copy from
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the words cannot be resized, leaving 'self' as
///         it was
/// @bigo{n/64}
[[nodiscard]] TDA_API
tda_Status tda_bitset_copy_assign(tda_BitSet *self, const tda_BitSet *other);

/// moves the members of 'other' into 'self', leaving 'other' empty
/// @param[in,out] self receives the universe along with the members, releases what it held and keeps
///                     its own allocator. 'self' == 'other' is a no-op
/// @param[in,out] other the bitset to move from; emptied on success and still usable, on
///                      its own allocator
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the two sit on different allocators and the words cannot be taken,
///         leaving both as they were
/// @bigo{1} on one allocator, n/64 on two — the words belong to the allocator that
///          made them
[[nodiscard]] TDA_API
tda_Status tda_bitset_move_assign(tda_BitSet *self, tda_BitSet *other);

/// exchanges the two bitsets whole, universes and all
/// @param[in,out] self one bitset
/// @param[in,out] other must have the same allocator: the words change hands where they
///                      lie, so nothing is copied and nothing can fail. 'self' == 'other'
///                      is a no-op
/// @note two allocators are a broken precondition, not a runtime state: the words belong to
///       the one that made them. Across two: tda_bitset_copy_with, then tda_bitset_move_assign
/// @bigo{1}
TDA_API
void tda_bitset_swap(tda_BitSet *self, tda_BitSet *other);

/// @}

/// @name one bit
/// @{

/// whether 'idx' is in the set
/// @param self the bitset
/// @param idx asserts idx < len — out of range is a programmer error, not a status
/// @return whether the bit is set
/// @bigo{1}
[[nodiscard]] TDA_API
bool tda_bitset_test(const tda_BitSet *self, size_t idx);

/// puts 'idx' in the set
/// @param self the bitset
/// @param idx asserts idx < len
/// @bigo{1}
TDA_API
void tda_bitset_set(tda_BitSet *self, size_t idx);

/// takes 'idx' out of the set
/// @copydetails tda_bitset_set
TDA_API
void tda_bitset_clear(tda_BitSet *self, size_t idx);

/// puts 'idx' in when it is out and out when it is in
/// @copydetails tda_bitset_set
TDA_API
void tda_bitset_flip(tda_BitSet *self, size_t idx);

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
void tda_bitset_clear_all(tda_BitSet *self);

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

/// the size of the universe, as named at construction
/// @param self the bitset
/// @return nbits — moved only by tda_bitset_copy_assign, tda_bitset_move_assign and
///         tda_bitset_swap
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_bitset_len(const tda_BitSet *self);

/// the allocator the bitset was built with
/// @param self the bitset
/// @return the allocator, borrowed
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_bitset_al(const tda_BitSet *self);

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

/// @}

/// @name set ops
/// @{

/// whether the two name the same universe and hold the same members
/// @param a one bitset
/// @param b a different len is simply not equal, not an assert
/// @return whether they are equal
/// @bigo{n/64}
[[nodiscard]] TDA_API
bool tda_bitset_eq(const tda_BitSet *a, const tda_BitSet *b);

/// adds every member of 'other' to 'self'
/// @param[in,out] self the bitset written into
/// @param other asserts the same len — one universe, or these ops mean nothing.
///              'self' == 'other' is allowed and changes nothing
/// @bigo{n/64}
TDA_API
void tda_bitset_union(tda_BitSet *self, const tda_BitSet *other);

/// drops from 'self' everything that is not in 'other'
/// @param[in,out] self the bitset written into
/// @param other asserts the same len
/// @bigo{n/64}
TDA_API
void tda_bitset_intersect(tda_BitSet *self, const tda_BitSet *other);

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

/// writes the members as {0, 3, 7} and a newline — the indices, not a row of bits
/// @param self the bitset
/// @param stream where to write
/// @bigo{n}
TDA_API
void tda_bitset_fprint(const tda_BitSet *self, FILE *stream);

/// tda_bitset_fprint to stdout
/// @param self the bitset
/// @bigo{n}
TDA_API
void tda_bitset_print(const tda_BitSet *self);

/// @}

/// @name macros
/// @{

/// walks the indices in the set, smallest first, binding 'idx' to each in turn
/// @param idx the name the loop variable takes; it is a size_t
/// @param self the bitset
/// @note the step past the current index happens after the body, so clearing that index
///       inside the loop is safe, while setting a smaller one no longer changes the walk
/// @bigo{n/64} over the whole walk
#define TDA_BITSET_FOR_EACH(idx, self)              \
    for (size_t idx = 0, from_ = 0;                 \
         tda_bitset_find_next((self), from_, &idx); \
         from_ = idx + 1)

/// @}

/// @}
