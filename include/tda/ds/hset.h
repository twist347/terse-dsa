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
/// with a value of size 0, and a set's node is the map's node header plus the key, padded.
///
/// The hasher, the equality and their ctx are fixed at construction and travel with the
/// keys through copy and swap, as in ds/hmap. Keys equal under 'eq' must hash alike, the
/// set mixes the hash before it picks a bucket, and a const char * key keeps the pointer,
/// not the text, all as in ds/hmap.
///
/// The set narrows the map's interface and adds no invariant: there is no value to get,
/// set or hand out, and a key is either in or out.
///
/// A key never moves: growing relinks the nodes, so a borrowed node survives inserts,
/// growth and the removal of other keys. It is invalidated only when its own key is
/// removed, cleared, assigned over or released with the set. The iteration order is
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

/// Owning hash set of type-erased keys. A value the caller holds, like tda_HMap, whose
/// rules it follows: an init that cannot fail returns it, one that can and a from_ fill
/// it in, tda_hset_deinit releases it, `= {}` is inert, and a copy of the struct is a
/// second owner of the same entries.
typedef struct tda_HSet {
    tda_HMap map_; ///< a map whose values have size 0
} tda_HSet;

/// A position in the set. A set entry is a map node underneath, but the type is the
/// set's own, so a set node never reaches a map function nor a map node a set one.
typedef struct tda_HSetNode tda_HSetNode;

/// @name lifetime
/// @{

/// an empty set that owns no buckets yet; allocates nothing, so it cannot fail
/// @param key the layout of one key, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param hasher the hash the keys are placed by, fixed for the life of the set
/// @param eq the equality the keys are matched by; must agree with 'hasher'
/// @param ctx passed to every call of 'hasher' and 'eq'; may be null
/// @param al used for every later allocation
/// @return the new set
/// @bigo{1}
[[nodiscard]] TDA_API
tda_HSet tda_hset_init(tda_Layout key, tda_Hasher hasher, tda_Eq eq, void *ctx, tda_Al al);

/// an empty set with room for 'cap' keys before the first growth
/// @param cap how many keys to make room for
/// @param key the layout of one key, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param hasher the hash the keys are placed by
/// @param eq the equality the keys are matched by
/// @param ctx passed to every call of 'hasher' and 'eq'; may be null
/// @param al the allocator
/// @param[out] out the new set, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the buckets cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_hset_init_cap(
    size_t cap,
    tda_Layout key,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al al,
    tda_HSet *out
);

/// a set holding the keys 's' views, with the key layout of the span
/// @param s the keys; repeats collapse into one
/// @param hasher the hash the keys are placed by
/// @param eq the equality the keys are matched by
/// @param ctx passed to every call of 'hasher' and 'eq'; may be null
/// @param al the allocator
/// @param[out] out the new set, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the buckets or a node cannot be allocated
/// @bigo{n} expected
[[nodiscard]] TDA_API
tda_Status tda_hset_from_span(
    tda_Span s,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al al,
    tda_HSet *out
);

/// releases what the set holds and leaves it inert, `= {}`
/// @param self a set, or an inert one, for which this is a no-op
/// @bigo{n}
TDA_API
void tda_hset_deinit(tda_HSet *self);

/// @}

/// @name copy
/// @{

/// a new set with the same keys, hasher, equality and ctx, on the same allocator
/// @param self the set to copy
/// @param[out] out the new set, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the buckets or any node cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_hset_copy(const tda_HSet *self, tda_HSet *out);

/// a new set with the same keys, hasher, equality and ctx, on 'al'
/// @param self the set to copy
/// @param al the allocator for the copy
/// @param[out] out the new set, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the buckets or any node cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_hset_copy_with(const tda_HSet *self, tda_Al al, tda_HSet *out);

/// overwrites the keys, hasher, equality and ctx of 'self' with those of 'other'
/// @param[in,out] self must have the same key layout; keeps its allocator
/// @param other the set to copy from; may be 'self'
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when a node cannot be allocated; 'self' is unchanged
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_hset_copy_assign(tda_HSet *self, const tda_HSet *other);

/// releases the keys of 'self' and hands it everything 'other' holds, its allocator,
/// hasher, equality and ctx included, leaving 'other' empty on that allocator
/// @param[in,out] self must have the same key layout, or be inert
/// @param[in,out] other emptied and still usable; may be 'self'
/// @bigo{k} for the k old keys of 'self', freed one by one
TDA_API
void tda_hset_move_assign(tda_HSet *self, tda_HSet *other);

/// @}

/// @name info
/// @{

/// how many keys the set holds
/// @param self the set
/// @return the key count
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_hset_len(const tda_HSet *self) {
    assert(self);
    assert(self->map_.al_.vt);

    return tda_hmap_len(&self->map_);
}

/// how many buckets the keys are spread over
/// @param self the set
/// @return the bucket count, a power of two, or 0 while the set owns none
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_hset_bucket_count(const tda_HSet *self) {
    assert(self);
    assert(self->map_.al_.vt);

    return tda_hmap_bucket_count(&self->map_);
}

/// the layout of one key, as the set was built with
/// @param self the set
/// @return the size and the alignment; every key is aligned to it
/// @bigo{1}
[[nodiscard]]
static inline tda_Layout tda_hset_key_layout(const tda_HSet *self) {
    assert(self);
    assert(self->map_.al_.vt);

    return tda_hmap_key_layout(&self->map_);
}

/// the allocator the set was built with
/// @param self the set
/// @return the view it was built with
/// @bigo{1}
[[nodiscard]]
static inline tda_Al tda_hset_al(const tda_HSet *self) {
    assert(self);
    assert(self->map_.al_.vt);

    return tda_hmap_al(&self->map_);
}

/// the hash the keys are placed by
/// @param self the set
/// @return the hasher given at construction
/// @bigo{1}
[[nodiscard]]
static inline tda_Hasher tda_hset_hasher(const tda_HSet *self) {
    assert(self);
    assert(self->map_.al_.vt);

    return tda_hmap_hasher(&self->map_);
}

/// the equality the keys are matched by
/// @param self the set
/// @return the equality given at construction. Named key_eq because tda_hset_eq
///         compares whole sets
/// @bigo{1}
[[nodiscard]]
static inline tda_Eq tda_hset_key_eq(const tda_HSet *self) {
    assert(self);
    assert(self->map_.al_.vt);

    return tda_hmap_key_eq(&self->map_);
}

/// the ctx the hasher and the key equality are called with
/// @param self the set
/// @return the ctx given at construction, borrowed
/// @bigo{1}
[[nodiscard]]
static inline void *tda_hset_key_ctx(const tda_HSet *self) {
    assert(self);
    assert(self->map_.al_.vt);

    return tda_hmap_key_ctx(&self->map_);
}

/// @}

/// @name compare
/// @{

/// whether the two hold the same keys, looked up with the hasher of 'b'
/// @param a one set
/// @param b must have the same key layout and key equality, since the check runs one way
///          only; a different length gives false. Its key ctx may differ, a seed say, as
///          long as both equalities agree on what a key is
/// @return whether the lengths match and every key of 'a' is in 'b'
/// @bigo{n} expected over the keys and the buckets of 'a'
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
/// @warning an insert during a walk may grow the table, which relinks every entry, and
///          the walk then skips some and repeats others. Collect what to insert first
/// @bigo{1} amortized over a whole walk, which is n plus the bucket count
[[nodiscard]] TDA_API
const tda_HSetNode *tda_hset_node_next(const tda_HSet *self, const tda_HSetNode *node);

/// the entry after 'node', to remove through
/// @copydetails tda_hset_node_next
[[nodiscard]] TDA_API
tda_HSetNode *tda_hset_node_next_mut(tda_HSet *self, tda_HSetNode *node);

/// the key at 'node'
/// @param self the set 'node' is in, which knows where in a node its key lies
/// @param node the position
/// @return the key, read-only: changing a key would break its bucket
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_hset_node_key(const tda_HSet *self, const tda_HSetNode *node);

/// @}

/// @name mods
/// @{

/// puts 'key' in the set
/// @param self the set
/// @param key the key to copy in; an equal key already there is kept as is. A key that
///            points at memory, such as a string, is then the caller's to free
/// @param[out] out_is_new whether the key was absent; may be null, set only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when a node or the bucket array cannot be allocated
/// @bigo{1} expected, plus the amortized cost of growing
[[nodiscard]] TDA_API
tda_Status tda_hset_insert(tda_HSet *self, const void *key, bool *out_is_new);

/// tda_hset_insert of every key 'src' views
/// @param self the set
/// @param src the keys, of the set's key layout
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

/// makes room for 'new_cap' keys before the next growth
/// @param self the set
/// @param new_cap a capacity at or below the one it has is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the array cannot be allocated; the set is unchanged
/// @bigo{n}; the nodes are relinked, not rebuilt, so borrowed nodes survive
[[nodiscard]] TDA_API
tda_Status tda_hset_reserve(tda_HSet *self, size_t new_cap);

/// gives back the buckets the keys no longer need
/// @param self an empty set is left owning no buckets
/// @retval TDA_STATUS_OK on success, including when there was nothing to give back
/// @retval TDA_STATUS_ERR_NO_MEM when the smaller array is refused; the set is unchanged
/// @bigo{n}; the nodes are relinked, not rebuilt, so borrowed nodes survive
[[nodiscard]] TDA_API
tda_Status tda_hset_shrink_to_fit(tda_HSet *self);

/// exchanges the contents of two sets, hashers, equalities and ctx included
/// @param self one set
/// @param other must have the same key layout; may be 'self'. The nodes do not move, so
///              borrowed nodes stay valid, but each now belongs to the other set and
///              goes to it as 'self'. Each side keeps the allocator its nodes came from,
///              so the two may differ
/// @bigo{1}
TDA_API
void tda_hset_swap(tda_HSet *self, tda_HSet *other);

/// @}

/// @name set ops
/// @{

/// adds every key of 'other' to 'self'
/// @param[in,out] self the set written into
/// @param other must have the same key layout and key equality, as in tda_hset_eq; may be
///              'self', which changes nothing
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when a node or the bucket array cannot be allocated;
///         'self' keeps its keys and holds some of those of 'other'
/// @bigo{m} expected over the m keys and the buckets of 'other', plus the amortized
///         cost of growing
[[nodiscard]] TDA_API
tda_Status tda_hset_union(tda_HSet *self, const tda_HSet *other);

/// drops from 'self' every key that is not in 'other'
/// @param[in,out] self the set written into
/// @param other as in tda_hset_union
/// @bigo{n} expected over the n keys and the buckets of 'self'
TDA_API
void tda_hset_intersection(tda_HSet *self, const tda_HSet *other);

/// drops from 'self' every key that is in 'other'
/// @param[in,out] self the set written into
/// @param other as in tda_hset_union; 'self' empties the set
/// @bigo{min(n, m)} expected; the smaller of the two is walked, its buckets included
TDA_API
void tda_hset_difference(tda_HSet *self, const tda_HSet *other);

/// keeps in 'self' the keys that are in exactly one of the two
/// @param[in,out] self the set written into
/// @param other as in tda_hset_union; 'self' empties the set
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when a node or the bucket array cannot be allocated;
///         'self' is left partly updated
/// @bigo{m} expected over the m keys and the buckets of 'other', plus the amortized
///         cost of growing
[[nodiscard]] TDA_API
tda_Status tda_hset_symmetric_difference(tda_HSet *self, const tda_HSet *other);

/// whether every key of 'self' is in 'other'
/// @param self the set that may be contained
/// @param other as in tda_hset_union
/// @return true when 'self' is empty
/// @bigo{n} expected over the n keys and the buckets of 'self'; O(1) when it is the
///         longer one
[[nodiscard]] TDA_API
bool tda_hset_is_subset(const tda_HSet *self, const tda_HSet *other);

/// whether the two share a key
/// @param self one set
/// @param other as in tda_hset_union
/// @return whether the intersection is non-empty
/// @bigo{min(n, m)} expected; the smaller of the two is walked, its buckets included
[[nodiscard]] TDA_API
bool tda_hset_intersects(const tda_HSet *self, const tda_HSet *other);

/// @}

/// @name print
/// @{

/// writes the keys as {a, b, c}, with no newline, in walk order
/// @param self the set
/// @param stream where to write
/// @param fprint the printer, called once per key
/// @param ctx passed to every call of 'fprint'; may be null
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
TDA_API
int tda_hset_fprint(const tda_HSet *self, FILE *stream, tda_FPrint fprint, void *ctx);

/// tda_hset_fprint to stdout, and a newline
/// @param self the set
/// @param fprint the printer, called once per key
/// @param ctx passed to every call of 'fprint'; may be null
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
TDA_API
int tda_hset_print(const tda_HSet *self, tda_FPrint fprint, void *ctx);

/// @}

/// @name macros
/// @{

/// @cond
// returns 'self' after checking TDA_LAYOUT_OF(K), so a macro evaluates its pointer
// once; _Generic keeps the pointer's constness
[[nodiscard]]
static inline const tda_HSet *tda_hset_typed_(const tda_HSet *self, [[maybe_unused]] tda_Layout key) {
    TDA_EXPECT(tda_layout_eq(tda_hset_key_layout(self), key));
    return self;
}

[[nodiscard]]
static inline tda_HSet *tda_hset_typed_mut_(tda_HSet *self, [[maybe_unused]] tda_Layout key) {
    TDA_EXPECT(tda_layout_eq(tda_hset_key_layout(self), key));
    return self;
}

#define TDA_HSET_TYPED_(K, self) \
    _Generic((self), const tda_HSet *: tda_hset_typed_, tda_HSet *: tda_hset_typed_mut_)((self), TDA_LAYOUT_OF(K))
/// @endcond

/// tda_hset_init with TDA_LAYOUT_OF(K)
/// @param K the key type
/// @param hasher the hash the keys are placed by
/// @param eq the equality the keys are matched by
/// @param ctx passed to every call of 'hasher' and 'eq'; may be null
/// @param al the allocator
/// @bigo{1}
#define TDA_HSET_INIT(K, hasher, eq, ctx, al) \
    tda_hset_init(TDA_LAYOUT_OF(K), (hasher), (eq), (ctx), (al))

/// tda_hset_init_cap with TDA_LAYOUT_OF(K)
/// @param K the key type
/// @param cap how many keys to make room for
/// @param hasher the hash the keys are placed by
/// @param eq the equality the keys are matched by
/// @param ctx passed to every call of 'hasher' and 'eq'; may be null
/// @param al the allocator
/// @param[out] out where the new set is written
/// @bigo{n}
#define TDA_HSET_INIT_CAP(K, cap, hasher, eq, ctx, al, out) \
    tda_hset_init_cap((cap), TDA_LAYOUT_OF(K), (hasher), (eq), (ctx), (al), (out))

/// a new set from the keys written out:
/// TDA_HSET_OF(int32_t, tda_hash_i32, tda_eq_i32, nullptr, al, &s, 5, 3, 5)
/// @param K the key type
/// @param hasher the hash the keys are placed by
/// @param eq the equality the keys are matched by
/// @param ctx passed to every call of 'hasher' and 'eq'; may be null
/// @param al the allocator
/// @param[out] out where the new set is written
/// @param ... the keys, as a K initializer list, not empty; a repeat is kept once
/// @bigo{n} expected
#define TDA_HSET_OF(K, hasher, eq, ctx, al, out, ...) \
    tda_hset_from_span(TDA_SPAN_OF(K, __VA_ARGS__), (hasher), (eq), (ctx), (al), (out))

/// tda_hset_insert from a value rather than an address
/// @param K the key type; 'key' may be any K, a struct literal in parentheses too
/// @param self the set
/// @param key the value to copy in
/// @param[out] out_is_new whether the key was absent; may be null
/// @bigo{1} expected
#define TDA_HSET_INSERT(K, self, key, out_is_new) \
    tda_hset_insert(TDA_HSET_TYPED_(K, self), &(K[1]){ (key) }[0], (out_is_new))

/// tda_hset_contains from a value rather than an address
/// @param K the key type; 'key' may be any K, a struct literal in parentheses too
/// @param self the set
/// @param key the value to look for
/// @bigo{1} expected
#define TDA_HSET_CONTAINS(K, self, key) \
    tda_hset_contains(TDA_HSET_TYPED_(K, self), &(K[1]){ (key) }[0])

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
    tda_hset_find(TDA_HSET_TYPED_(K, self), &(K[1]){ (key) }[0])

/// tda_hset_find_mut from a value rather than an address
/// @copydetails TDA_HSET_CONTAINS
#define TDA_HSET_FIND_MUT(K, self, key) \
    tda_hset_find_mut(TDA_HSET_TYPED_(K, self), &(K[1]){ (key) }[0])

/// tda_hset_remove from a value rather than an address
/// @copydetails TDA_HSET_CONTAINS
#define TDA_HSET_REMOVE(K, self, key) \
    tda_hset_remove(TDA_HSET_TYPED_(K, self), &(K[1]){ (key) }[0])

/// tda_hset_take from a key value rather than an address, 'out_key' typechecked as K *
/// @param K the key type; 'key' may be any K, a struct literal in parentheses too
/// @param self the set
/// @param key the key to drop
/// @param[out] out_key the stored key, or nullptr
/// @bigo{1} expected
#define TDA_HSET_TAKE(K, self, key, out_key) \
    tda_hset_take(TDA_HSET_TYPED_(K, self), &(K[1]){ (key) }[0], (K *){ (out_key) })

/// @cond
// the body of both walks; the hidden names come in already unique, and 'self' is
// read once. The node after 'node' is taken before the body runs, so the body may
// remove 'node'
#define TDA_HSET_WALK_(SelfT, NodeT, node, self, first, next, hset, once, after)    \
    for (SelfT *hset = (self), **once = &hset; once; once = nullptr)                \
        for (NodeT *node = first(hset), *after = node ? next(hset, node) : nullptr; \
             node;                                                                  \
             node = after, after = node ? next(hset, node) : nullptr)
/// @endcond

/// walks every key, binding 'node' to each in turn, in unspecified (bucket) order
/// @param node the name of the loop variable, a const tda_HSetNode *
/// @param self the set
/// @note the next node is taken before the body runs, so the body of
///       TDA_HSET_FOR_EACH_MUT may drop 'node' with tda_hset_remove_node. Dropping any
///       other node, or inserting, is not allowed; see tda_hset_node_next
#define TDA_HSET_FOR_EACH(node, self) \
    TDA_HSET_WALK_(                   \
        const tda_HSet,               \
        const tda_HSetNode,           \
        node,                         \
        self,                         \
        tda_hset_first_node,          \
        tda_hset_node_next,           \
        TDA_LINE_NAME_(hset_),        \
        TDA_LINE_NAME_(once_),        \
        TDA_LINE_NAME_(after_)        \
    )

/// the same walk, over nodes that can be removed through
/// @copydetails TDA_HSET_FOR_EACH
#define TDA_HSET_FOR_EACH_MUT(node, self) \
    TDA_HSET_WALK_(                       \
        tda_HSet,                         \
        tda_HSetNode,                     \
        node,                             \
        self,                             \
        tda_hset_first_node_mut,          \
        tda_hset_node_next_mut,           \
        TDA_LINE_NAME_(hset_),            \
        TDA_LINE_NAME_(once_),            \
        TDA_LINE_NAME_(after_)            \
    )

/// tda_hset_node_key as a const K *
/// @param K the key type
/// @param self the set 'node' is in
/// @param node the position
/// @bigo{1}
#define TDA_HSET_NODE_KEY_AS(K, self, node) \
    ((const typeof(K) *) tda_hset_node_key(TDA_HSET_TYPED_(K, self), (node)))

/// @}

/// @}
