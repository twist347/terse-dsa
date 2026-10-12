#pragma once

#include "tda/algo/fn.h"
#include "tda/alloc/alloc.h"
#include "tda/core/check.h"
#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/hash.h"
#include "tda/core/print.h"
#include "tda/core/span.h"
#include "tda/core/status.h"
#include "tda/ds/hmap.h"

#include <stddef.h>

/// @file

/// @defgroup ds_hset ds/hset
/// @ingroup ds
/// @brief tda_HSet — an owning hash set of type-erased keys
///
/// A ds/hmap with no values. C has no zero-sized type, so the map is built internally
/// with a value size of 0, and a set's node is just the header plus the key.
///
/// The hasher and the equality are fixed at construction and travel with the keys through
/// copy and swap, as in ds/hmap. Keys equal under 'eq' must hash alike.
///
/// The set narrows the map's interface and adds no invariant: there is no value to get,
/// set or hand out, and a key is either in or out.
///
/// A key never moves: growing relinks the nodes, so a borrowed node survives inserts,
/// growth and the removal of other keys. It is invalidated only when its own key is
/// removed, cleared, assigned over or dropped with the set. The iteration order is
/// unspecified and may change on any insert that grows, and a walk visits every bucket,
/// as in ds/hmap.
///
/// @par Example
/// @snippet ds/example_hset.c build
/// @snippet ds/example_hset.c membership
/// @snippet ds/example_hset.c walk
/// @snippet ds/example_hset.c pred
/// @snippet ds/example_hset.c bulk
/// @{

/// Owning hash set of type-erased keys.
/// Opaque: made by a constructor, released by tda_hset_drop
typedef struct tda_HSet tda_HSet;

/// A position in the set.
/// A set entry is a map node with no value, so both share one type
typedef tda_HMapNode tda_HSetNode;

/// @name lifetime
/// @{

/// an empty set that owns no buckets yet
/// @param key_size the size of one key, asserted greater than 0
/// @param hasher the hash the keys are placed by, fixed for the life of the set
/// @param eq the equality the keys are matched by; must agree with 'hasher'
/// @param al used for every later allocation
/// @param[out] out the new set, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header cannot be allocated
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Status tda_hset_new(size_t key_size, tda_Hasher hasher, tda_Eq eq, tda_Al *al, tda_HSet **out);

/// an empty set with room for 'cap' keys before the first growth
/// @param cap how many keys to make room for
/// @param key_size the size of one key, asserted greater than 0
/// @param hasher the hash the keys are placed by
/// @param eq the equality the keys are matched by
/// @param al the allocator
/// @param[out] out the new set, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the buckets cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_hset_new_cap(size_t cap, size_t key_size, tda_Hasher hasher, tda_Eq eq, tda_Al *al, tda_HSet **out);

/// a set holding the keys 's' views, with key_size from its elem_size
/// @param s the keys; repeats collapse into one
/// @param hasher the hash the keys are placed by
/// @param eq the equality the keys are matched by
/// @param al the allocator
/// @param[out] out the new set, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the set, its buckets or a node cannot be allocated
/// @bigo{n} expected
[[nodiscard]] TDA_API
tda_Status tda_hset_from_span(tda_Span s, tda_Hasher hasher, tda_Eq eq, tda_Al *al, tda_HSet **out);

/// releases every node, the buckets and the set through the allocator it was built with
/// @param self null is a no-op
/// @bigo{n}
TDA_API
void tda_hset_drop(tda_HSet *self);

/// @}

/// @name copy
/// @{

/// a new set with the same keys, hasher and equality, on the same allocator
/// @param self the set to copy
/// @param[out] out the new set, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the buckets or any node cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_hset_copy(const tda_HSet *self, tda_HSet **out);

/// a new set with the same keys, hasher and equality, on 'al'
/// @param self the set to copy
/// @param al the allocator for the copy
/// @param[out] out the new set, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the buckets or any node cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_hset_copy_with(const tda_HSet *self, tda_Al *al, tda_HSet **out);

/// overwrites the keys, hasher and equality of 'self' with those of 'other'
/// @param[in,out] self must have the same key_size; keeps its allocator
/// @param other the set to copy from; may be 'self'
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when a node cannot be allocated; 'self' is unchanged
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_hset_copy_assign(tda_HSet *self, const tda_HSet *other);

/// moves the keys, hasher and equality of 'other' into 'self', leaving 'other' empty
/// @param[in,out] self must have the same key_size; old keys released, allocator kept
/// @param[in,out] other emptied on success and still usable; may be 'self'
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the allocators differ and the copy cannot be
///         allocated; both are left unchanged
/// @bigo{1} on one allocator, n across two; there the keys are rebuilt, so borrowed
///          nodes do not survive
[[nodiscard]] TDA_API
tda_Status tda_hset_move_assign(tda_HSet *self, tda_HSet *other);

/// @}

/// @name info
/// @{

/// how many keys the set holds
/// @param self the set
/// @return the key count
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_hset_len(const tda_HSet *self);

/// how many buckets the keys are spread over
/// @param self the set
/// @return the bucket count, a power of two, or 0 while the set owns none
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_hset_bucket_count(const tda_HSet *self);

/// the size of one key
/// @param self the set
/// @return key_size
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_hset_key_size(const tda_HSet *self);

/// the allocator the set was built with
/// @param self the set
/// @return the allocator, borrowed
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_hset_al(const tda_HSet *self);

/// the hash the keys are placed by
/// @param self the set
/// @return the hasher given at construction
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Hasher tda_hset_hasher(const tda_HSet *self);

/// the equality the keys are matched by
/// @param self the set
/// @return the equality given at construction. Named key_eq because tda_hset_eq
///         compares whole sets
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Eq tda_hset_key_eq(const tda_HSet *self);

/// @}

/// @name compare
/// @{

/// whether the two hold the same keys, looked up with the hasher of 'b'
/// @param a one set
/// @param b must have the same key_size and key equality, since the check runs one way
///          only; a different length gives false
/// @return whether the lengths match and every key of 'a' is in 'b'
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_hset_eq(const tda_HSet *a, const tda_HSet *b);

/// @}

/// @name lookup
/// @{

/// whether 'key' is in the set
/// @param self the set
/// @param key the key to look for
/// @return whether an entry matched it
/// @bigo{1} expected
[[nodiscard]] TDA_API
bool tda_hset_contains(const tda_HSet *self, const void *key);

/// the entry for 'key' as a position
/// @param self the set
/// @param key the key to look for
/// @return the node, or null when the set holds no such key
/// @bigo{1} expected
[[nodiscard]] TDA_API
const tda_HSetNode *tda_hset_find(const tda_HSet *self, const void *key);

/// the entry for 'key' as a position to remove through
/// @copydetails tda_hset_find
[[nodiscard]] TDA_API
tda_HSetNode *tda_hset_find_mut(tda_HSet *self, const void *key);

/// @}

/// @name nodes
/// @{

/// the first entry of a walk
/// @param self the set
/// @return a node, or null on an empty set; the order is unspecified
/// @bigo{n} in the bucket count
[[nodiscard]] TDA_API
const tda_HSetNode *tda_hset_first_node(const tda_HSet *self);

/// the first entry of a walk, to remove through
/// @copydetails tda_hset_first_node
[[nodiscard]] TDA_API
tda_HSetNode *tda_hset_first_node_mut(tda_HSet *self);

/// the entry after 'node'
/// @param self the set, needed to step past the end of a chain into the next bucket
/// @param node the position to step from
/// @return the next node, or null at the end
/// @bigo{1} amortized over a whole walk, which is n plus the bucket count
[[nodiscard]] TDA_API
const tda_HSetNode *tda_hset_node_next(const tda_HSet *self, const tda_HSetNode *node);

/// the entry after 'node', to remove through
/// @copydetails tda_hset_node_next
[[nodiscard]] TDA_API
tda_HSetNode *tda_hset_node_next_mut(tda_HSet *self, tda_HSetNode *node);

/// the key at 'node'
/// @param node the position
/// @return the key, read-only: changing a key would break its bucket
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_hset_node_key(const tda_HSetNode *node);

/// @}

/// @name mods
/// @{

/// puts 'key' in the set
/// @param self the set
/// @param key the key to copy in; an equal key already there is kept as is
/// @param[out] out_is_new whether the key was absent; may be null, set only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when a node or the bucket array cannot be allocated
/// @bigo{1} expected, plus the amortized cost of growing
[[nodiscard]] TDA_API
tda_Status tda_hset_insert(tda_HSet *self, const void *key, bool *out_is_new);

/// tda_hset_insert of every key 'src' views
/// @param self the set
/// @param src the keys, of the set's key_size
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when a node or the bucket array cannot be allocated;
///         the keys before the failing one stay inserted
/// @bigo{n} expected, plus the amortized cost of growing
[[nodiscard]] TDA_API
tda_Status tda_hset_extend(tda_HSet *self, tda_Span src);

/// drops 'key' if it is there
/// @param self the set
/// @param key the key to drop
/// @return whether it was there
/// @bigo{1} expected
TDA_API
bool tda_hset_remove(tda_HSet *self, const void *key);

/// drops the entry 'node' names
/// @param self the set
/// @param node a position in this set (checked); pointers into it become invalid, other
///             entries are untouched
/// @bigo{1} expected, as in tda_hmap_remove_node
TDA_API
void tda_hset_remove_node(tda_HSet *self, tda_HSetNode *node);

/// drops 'key' if it is there, copying out the stored key first, so the caller can free
/// what it owns
/// @param self the set
/// @param key the key to drop
/// @param[out] out_key the stored key, which may differ from 'key' in what 'eq' ignores;
///                     may be null or 'key' itself, written only on a hit
/// @return whether it was there
/// @bigo{1} expected
TDA_API
bool tda_hset_take(tda_HSet *self, const void *key, void *out_key);

/// keeps the keys 'pred' passes and drops the rest
/// @param self the set; kept nodes do not move, so pointers into them stay valid
/// @param pred asked once per key, in walk order; must not change the set
/// @param ctx handed to 'pred'
/// @bigo{n} plus the bucket count
TDA_API
void tda_hset_retain(tda_HSet *self, tda_Pred pred, void *ctx);

/// drops every key, keeping the buckets
/// @param self the set
/// @bigo{n} plus the bucket count
TDA_API
void tda_hset_clear(tda_HSet *self);

/// makes room for 'cap' keys before the next growth
/// @param self the set
/// @param cap a capacity at or below the one it has is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the array cannot be allocated; the set is unchanged
/// @bigo{n}; the nodes are relinked, not rebuilt, so borrowed nodes survive
[[nodiscard]] TDA_API
tda_Status tda_hset_reserve(tda_HSet *self, size_t cap);

/// gives back the buckets the keys no longer need
/// @param self an empty set is left owning no buckets
/// @retval TDA_STATUS_OK on success, including when there was nothing to give back
/// @retval TDA_STATUS_ERR_NO_MEM when the smaller array is refused; the set is unchanged
/// @bigo{n}; the nodes are relinked, not rebuilt, so borrowed nodes survive
[[nodiscard]] TDA_API
tda_Status tda_hset_shrink_to_fit(tda_HSet *self);

/// exchanges the contents of two sets, hashers and equalities included
/// @param self one set
/// @param other must have the same key_size and allocator; may be 'self'. The nodes do
///              not move, so borrowed nodes stay valid
/// @note a node belongs to the allocator that made it. Across allocators use
///       tda_hset_copy_with, then tda_hset_move_assign
/// @bigo{1}
TDA_API
void tda_hset_swap(tda_HSet *self, tda_HSet *other);

/// @}

/// @name set ops
/// @{

/// adds every key of 'other' to 'self'
/// @param[in,out] self the set written into
/// @param other must have the same key_size and key equality, as in tda_hset_eq; may be
///              'self', which changes nothing
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when a node or the bucket array cannot be allocated;
///         'self' keeps its keys and holds some of those of 'other'
/// @bigo{m} expected over the m keys of 'other', plus the amortized cost of growing
[[nodiscard]] TDA_API
tda_Status tda_hset_union(tda_HSet *self, const tda_HSet *other);

/// drops from 'self' every key that is not in 'other'
/// @param[in,out] self the set written into
/// @param other as in tda_hset_union
/// @bigo{n} expected over the n keys of 'self'
TDA_API
void tda_hset_intersect(tda_HSet *self, const tda_HSet *other);

/// drops from 'self' every key that is in 'other'
/// @param[in,out] self the set written into
/// @param other as in tda_hset_union; 'self' empties the set
/// @bigo{min(n, m)} expected; the smaller of the two is walked
TDA_API
void tda_hset_difference(tda_HSet *self, const tda_HSet *other);

/// keeps in 'self' the keys that are in exactly one of the two
/// @param[in,out] self the set written into
/// @param other as in tda_hset_union; 'self' empties the set
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when a node or the bucket array cannot be allocated;
///         'self' is left partly updated
/// @bigo{m} expected over the m keys of 'other', plus the amortized cost of growing
[[nodiscard]] TDA_API
tda_Status tda_hset_symmetric_difference(tda_HSet *self, const tda_HSet *other);

/// whether every key of 'self' is in 'other'
/// @param self the set that may be contained
/// @param other as in tda_hset_union
/// @return true when 'self' is empty
/// @bigo{n} expected over the n keys of 'self'; O(1) when it is the longer one
[[nodiscard]] TDA_API
bool tda_hset_is_subset(const tda_HSet *self, const tda_HSet *other);

/// whether the two share a key
/// @param self one set
/// @param other as in tda_hset_union
/// @return whether the intersection is non-empty
/// @bigo{min(n, m)} expected; the smaller of the two is walked
[[nodiscard]] TDA_API
bool tda_hset_intersects(const tda_HSet *self, const tda_HSet *other);

/// @}

/// @name print
/// @{

/// writes the keys as {a, b, c} and a newline, in walk order
/// @param self the set
/// @param stream where to write
/// @param fprint the printer, called once per key
/// @bigo{n}
TDA_API
void tda_hset_fprint(const tda_HSet *self, FILE *stream, tda_FPrint fprint);

/// tda_hset_fprint to stdout
/// @param self the set
/// @param fprint the printer, called once per key
/// @bigo{n}
TDA_API
void tda_hset_print(const tda_HSet *self, tda_FPrint fprint);

/// @}

/// @name macros
/// @{

/// @cond
// returns 'self' after checking sizeof(K), so a macro evaluates its handle once;
// _Generic keeps the handle's constness
[[nodiscard]]
static inline const tda_HSet *tda_hset_typed_c_(const tda_HSet *self, [[maybe_unused]] size_t size) {
    TDA_EXPECT(tda_hset_key_size(self) == size);
    return self;
}

[[nodiscard]]
static inline tda_HSet *tda_hset_typed_(tda_HSet *self, [[maybe_unused]] size_t size) {
    TDA_EXPECT(tda_hset_key_size(self) == size);
    return self;
}

#define TDA_HSET_TYPED_(K, self) \
    _Generic((self), const tda_HSet *: tda_hset_typed_c_, tda_HSet *: tda_hset_typed_)((self), sizeof(K))
/// @endcond

/// tda_hset_new with sizeof(K) for the key size
/// @param K the key type
/// @param hasher the hash the keys are placed by
/// @param eq the equality the keys are matched by
/// @param al the allocator
/// @param[out] out where the new set is written
/// @bigo{1}
#define TDA_HSET_NEW(K, hasher, eq, al, out) \
    tda_hset_new(sizeof(K), (hasher), (eq), (al), (out))

/// tda_hset_new_cap with sizeof(K)
/// @param K the key type
/// @param cap how many keys to make room for
/// @param hasher the hash the keys are placed by
/// @param eq the equality the keys are matched by
/// @param al the allocator
/// @param[out] out where the new set is written
/// @bigo{n}
#define TDA_HSET_NEW_CAP(K, cap, hasher, eq, al, out) \
    tda_hset_new_cap((cap), sizeof(K), (hasher), (eq), (al), (out))

/// tda_hset_insert from a value rather than an address
/// @param K the key type; scalars only, as 'key' becomes a compound literal
/// @param self the set
/// @param key the value to copy in
/// @param[out] out_is_new whether the key was absent; may be null
/// @bigo{1} expected
#define TDA_HSET_INSERT(K, self, key, out_is_new) \
    tda_hset_insert(TDA_HSET_TYPED_(K, self), &(K){ (key) }, (out_is_new))

/// tda_hset_contains from a value rather than an address
/// @param K the key type; scalars only, as 'key' becomes a compound literal
/// @param self the set
/// @param key the value to look for
/// @bigo{1} expected
#define TDA_HSET_CONTAINS(K, self, key) \
    tda_hset_contains(TDA_HSET_TYPED_(K, self), &(K){ (key) })

/// tda_hset_extend from the keys written out
/// @param K the key type
/// @param self the set
/// @param ... the keys, as a K initializer list
/// @bigo{n} expected
#define TDA_HSET_EXTEND(K, self, ...) \
    tda_hset_extend((self), TDA_SPAN_OF(K, __VA_ARGS__))

/// tda_hset_find from a value rather than an address
/// @copydetails TDA_HSET_CONTAINS
#define TDA_HSET_FIND(K, self, key) \
    tda_hset_find(TDA_HSET_TYPED_(K, self), &(K){ (key) })

/// tda_hset_find_mut from a value rather than an address
/// @copydetails TDA_HSET_CONTAINS
#define TDA_HSET_FIND_MUT(K, self, key) \
    tda_hset_find_mut(TDA_HSET_TYPED_(K, self), &(K){ (key) })

/// tda_hset_remove from a value rather than an address
/// @copydetails TDA_HSET_CONTAINS
#define TDA_HSET_REMOVE(K, self, key) \
    tda_hset_remove(TDA_HSET_TYPED_(K, self), &(K){ (key) })

/// tda_hset_take from a key value rather than an address, 'out_key' typechecked as K *
/// @param K the key type; scalars only, as 'key' becomes a compound literal
/// @param self the set
/// @param key the key to drop
/// @param[out] out_key the stored key, or nullptr
/// @bigo{1} expected
#define TDA_HSET_TAKE(K, self, key, out_key) \
    tda_hset_take(TDA_HSET_TYPED_(K, self), &(K){ (key) }, (K *){ (out_key) })

/// walks every key, binding 'node' to each in turn, in unspecified (bucket) order
/// @param node the name of the loop variable, a const tda_HSetNode *
/// @param self the set
/// @warning the loop steps through 'node' after the body, so removing it inside the loop
///          uses a freed node. To remove while walking, take the next node first:
/// @code
/// for (tda_HSetNode *node = tda_hset_first_node_mut(s), *next; node; node = next) {
///     next = tda_hset_node_next_mut(s, node);
///     if (drop_it(node)) {
///         tda_hset_remove_node(s, node);
///     }
/// }
/// @endcode
#define TDA_HSET_FOR_EACH(node, self)                          \
    for (const tda_HSetNode *node = tda_hset_first_node(self); \
         node;                                                 \
         node = tda_hset_node_next((self), node))

/// the same walk, over nodes that can be removed through
/// @copydetails TDA_HSET_FOR_EACH
#define TDA_HSET_FOR_EACH_MUT(node, self)                         \
    for (tda_HSetNode *node = tda_hset_first_node_mut(self);      \
         node;                                                    \
         node = tda_hset_node_next_mut((self), node))

/// tda_hset_node_key as a const K *
/// @param K the key type
/// @param node the position
/// @warning K is not checked: a node does not know its key size
/// @bigo{1}
#define TDA_HSET_NODE_KEY_AS(K, node) \
    ((const K *) tda_hset_node_key((node)))

/// @}

/// @}
