#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/check.h"
#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/hash.h"
#include "tda/core/print.h"
#include "tda/core/span.h"
#include "tda/core/status.h"

#include <stddef.h>

/// @file

/// @defgroup ds_hmap ds/hmap
/// @ingroup ds
/// @brief tda_HMap — an owning hash map from keys to values
///
/// Separate chaining: an array of buckets, each a chain of the nodes that hashed to it.
///
/// The hasher, the equality and their ctx are fixed at construction and travel with the
/// entries through copy and swap, since the buckets depend on the hash. Keys equal under 'eq'
/// must hash alike. The map mixes the hash before it picks a bucket, so hashes that
/// differ in any bit spread over the buckets: an identity hasher works, even on keys that
/// differ only in high bits. Keys a hasher maps to one hash still share a chain.
///
/// A key is stored as its layout's bytes. With tda_hash_cstr and tda_eq_cstr the key is
/// a const char *, so the map keeps the pointer and not the text: the string must outlive
/// its entry and stay unchanged, and keys read one after another into a single buffer
/// all point at it.
///
/// An entry never moves. Growing reallocates the bucket array and relinks the nodes, so a
/// borrowed node survives inserts, growth and the removal of other entries. It is
/// invalidated only when its own entry is removed, cleared, assigned over or released with
/// the map. This is why a position is a node and not a slot index.
///
/// The iteration order is unspecified and may change on any insert that grows. A walk
/// (and so clear, copy, eq, print and deinit) visits every bucket as well as every entry,
/// so a map that grew and then emptied walks as slowly as when full, until
/// tda_hmap_shrink_to_fit gives the buckets back.
///
/// @par Example
/// @snippet ds/example_hmap.c build
/// @snippet ds/example_hmap.c lookup
/// @snippet ds/example_hmap.c count
/// @snippet ds/example_hmap.c pred
/// @snippet ds/example_hmap.c walk
/// @{

/// A position in the map.
/// Borrowed, and valid until its own entry is removed, cleared, assigned over or released
/// with the map. The key is read-only: a changed key would sit in the wrong bucket, and
/// the map could not notice
typedef struct tda_HMapNode tda_HMapNode;

/// Owning hash map from type-erased keys to type-erased values. A value the caller holds
/// wherever it likes: an init returns it, a from_ fills it in, tda_hmap_deinit releases
/// its entries. `= {}` is the inert state, which tda_hmap_deinit accepts and every other
/// op asserts.
///
/// The fields belong to the map: read and change them only through the functions here.
/// No entry points back at the struct, so it can be moved as bytes.
///
/// @warning a copy of the struct is a second owner of the same entries: releasing both
///          frees them twice. Pass a pointer, and hand a map over with
///          tda_hmap_move_assign.
typedef struct tda_HMap {
    tda_HMapNode **buckets_; ///< the chains; null while bucket_count_ is 0
    size_t bucket_count_;    ///< a power of two, or 0 while the map owns none
    size_t len_;             ///< how many entries it holds
    tda_Layout key_;         ///< the layout of one key
    tda_Layout val_;         ///< the layout of one value; size 0 for a set's map
    size_t key_offset_;      ///< where in a node the key starts
    size_t val_offset_;      ///< where in a node the value starts
    tda_Layout node_;        ///< the layout of one node: header, key, value
    tda_Hasher hasher_;      ///< the hash the keys are placed by
    tda_Eq eq_;              ///< the equality the keys are matched by
    void *ctx_;              ///< passed to both: they agree on what a key is
    tda_Al al_;              ///< where the entries come from; moves along with the map
} tda_HMap;

/// A test of one entry, key and value together; the map's tda_Pred
typedef bool (*tda_EntryPred)(const void *key, const void *val, void *ctx);

/// @name lifetime
/// @{

/// an empty map that owns no buckets yet; allocates nothing, so it cannot fail
/// @param key the layout of one key, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param val the layout of one value, asserted to be a type's (for none, use ds/hset)
/// @param hasher the hash the keys are placed by, fixed for the life of the map
/// @param eq the equality the keys are matched by; must agree with 'hasher'
/// @param ctx passed to every call of 'hasher' and 'eq'; may be null
/// @param al used for every later allocation
/// @return the new map
/// @bigo{1}
[[nodiscard]] TDA_API
tda_HMap tda_hmap_init(
    tda_Layout key, tda_Layout val,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al al
);

/// an empty map with room for 'cap' entries before the first growth
/// @param cap how many entries to make room for
/// @param key the layout of one key, asserted to be a type's
/// @param val the layout of one value, asserted to be a type's
/// @param hasher the hash the keys are placed by
/// @param eq the equality the keys are matched by
/// @param ctx passed to every call of 'hasher' and 'eq'; may be null
/// @param al the allocator
/// @param[out] out the new map, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the buckets cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_hmap_init_cap(
    size_t cap,
    tda_Layout key, tda_Layout val,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al al,
    tda_HMap *out
);

/// a map holding keys[i] -> vals[i], with the key and value layouts of the two spans
/// @param keys the keys; a repeated key keeps its last value
/// @param vals the values; asserts the same len as 'keys'
/// @param hasher the hash the keys are placed by
/// @param eq the equality the keys are matched by
/// @param ctx passed to every call of 'hasher' and 'eq'; may be null
/// @param al the allocator
/// @param[out] out the new map, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the buckets or a node cannot be allocated
/// @bigo{n} expected
[[nodiscard]] TDA_API
tda_Status tda_hmap_from_spans(
    tda_Span keys, tda_Span vals,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al al,
    tda_HMap *out
);

/// releases what the map holds and leaves it inert, `= {}`
/// @param self a map, or an inert one, for which this is a no-op
/// @bigo{n}
TDA_API
void tda_hmap_deinit(tda_HMap *self);

/// @}

/// @name copy
/// @{

/// a new map with the same entries, hasher, equality and ctx, on the same allocator
/// @param self the map to copy
/// @param[out] out the new map, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the buckets or any node cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_hmap_copy(const tda_HMap *self, tda_HMap *out);

/// a new map with the same entries, hasher, equality and ctx, on 'al'
/// @param self the map to copy
/// @param al the allocator for the copy
/// @param[out] out the new map, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the buckets or any node cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_hmap_copy_with(const tda_HMap *self, tda_Al al, tda_HMap *out);

/// overwrites the entries, hasher, equality and ctx of 'self' with those of 'other'
/// @param[in,out] self must have the same key and value layouts; keeps its allocator
/// @param other the map to copy from; may be 'self'
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when a node cannot be allocated; 'self' is unchanged
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_hmap_copy_assign(tda_HMap *self, const tda_HMap *other);

/// releases the entries of 'self' and hands it everything 'other' holds, its allocator,
/// hasher, equality and ctx included, leaving 'other' empty on that allocator
/// @param[in,out] self must have the same key and value layouts
/// @param[in,out] other emptied and still usable; may be 'self'
/// @bigo{k} for the k old entries of 'self', freed one by one
TDA_API
void tda_hmap_move_assign(tda_HMap *self, tda_HMap *other);

/// @}

/// @name info
/// @{

/// how many entries the map holds
/// @param self the map
/// @return the entry count
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_hmap_len(const tda_HMap *self) {
    assert(self);
    assert(self->al_.vt);

    return self->len_;
}

/// how many buckets the entries are spread over
/// @param self the map
/// @return the bucket count: a power of two, or 0 while the map owns none
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_hmap_bucket_count(const tda_HMap *self) {
    assert(self);
    assert(self->al_.vt);

    return self->bucket_count_;
}

/// the layout of one key, as the map was built with
/// @param self the map
/// @return the size and the alignment; every key is aligned to it
/// @bigo{1}
[[nodiscard]]
static inline tda_Layout tda_hmap_key_layout(const tda_HMap *self) {
    assert(self);
    assert(self->al_.vt);

    return self->key_;
}

/// the layout of one value, as the map was built with
/// @param self the map
/// @return the size and the alignment; every value is aligned to it
/// @bigo{1}
[[nodiscard]]
static inline tda_Layout tda_hmap_val_layout(const tda_HMap *self) {
    assert(self);
    assert(self->al_.vt);

    return self->val_;
}

/// the allocator the map was built with
/// @param self the map
/// @return the view it was built with
/// @bigo{1}
[[nodiscard]]
static inline tda_Al tda_hmap_al(const tda_HMap *self) {
    assert(self);
    assert(self->al_.vt);

    return self->al_;
}

/// the hash the keys are placed by
/// @param self the map
/// @return the hasher given at construction
/// @bigo{1}
[[nodiscard]]
static inline tda_Hasher tda_hmap_hasher(const tda_HMap *self) {
    assert(self);
    assert(self->al_.vt);

    return self->hasher_;
}

/// the equality the keys are matched by
/// @param self the map
/// @return the equality given at construction. Named key_eq because tda_hmap_eq
///         compares whole maps and values have no equality of their own
/// @bigo{1}
[[nodiscard]]
static inline tda_Eq tda_hmap_key_eq(const tda_HMap *self) {
    assert(self);
    assert(self->al_.vt);

    return self->eq_;
}

/// the ctx the hasher and the key equality are called with
/// @param self the map
/// @return the ctx given at construction, borrowed
/// @bigo{1}
[[nodiscard]]
static inline void *tda_hmap_key_ctx(const tda_HMap *self) {
    assert(self);
    assert(self->al_.vt);

    return self->ctx_;
}

/// @}

/// @name compare
/// @{

/// whether the two hold the same keys with equal values, looked up with the hasher of 'b'
/// @param a one map
/// @param b must have the same key and value layouts and key equality, since the check runs
///          one way only; a different length gives false. Its key ctx may differ, a seed
///          say, as long as both equalities agree on what a key is
/// @return whether the lengths match and every entry of 'a' is in 'b' with the same value
///         byte for byte (a tda_Eq takes no size, so it cannot do this)
/// @bigo{n} expected over the entries and the buckets of 'a'
[[nodiscard]] TDA_API
bool tda_hmap_eq(const tda_HMap *a, const tda_HMap *b);

/// the same, with 'val_eq' deciding what an equal value is
/// @param a one map
/// @param b must have the same key and value layouts and key equality
/// @param val_eq the value equality, which the map does not carry; needed for values with
///               padding or fields that do not count
/// @param ctx passed to every call of 'val_eq'; may be null
/// @return whether the lengths match and every entry of 'a' is in 'b' with an equal value
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_hmap_eq_by(const tda_HMap *a, const tda_HMap *b, tda_Eq val_eq, void *ctx);

/// @}

/// @name lookup
/// @{

/// the value stored under 'key'
/// @param self the map
/// @param key the key to look for
/// @return the value, or null when the key is absent; a miss is not an error
/// @bigo{1} expected
[[nodiscard]] TDA_API
const void *tda_hmap_get(const tda_HMap *self, const void *key);

/// the value stored under 'key', to write through
/// @copydetails tda_hmap_get
[[nodiscard]] TDA_API
void *tda_hmap_get_mut(tda_HMap *self, const void *key);

/// whether the map holds an entry under 'key'
/// @param self the map
/// @param key the key to look for
/// @return whether an entry matched it
/// @bigo{1} expected
[[nodiscard]] TDA_API
bool tda_hmap_contains(const tda_HMap *self, const void *key);

/// the entry under 'key' as a position
/// @param self the map
/// @param key the key to look for
/// @return the node, or null. It gives the key and value together and removes the entry
///         without hashing again
/// @bigo{1} expected
[[nodiscard]] TDA_API
const tda_HMapNode *tda_hmap_find(const tda_HMap *self, const void *key);

/// the entry under 'key' as a position to write or remove through
/// @copydetails tda_hmap_find
[[nodiscard]] TDA_API
tda_HMapNode *tda_hmap_find_mut(tda_HMap *self, const void *key);

/// @}

/// @name nodes
/// @{

/// the first entry of a walk
/// @param self the map
/// @return a node, or null on an empty map; the order is unspecified
/// @bigo{n} in the bucket count
[[nodiscard]] TDA_API
const tda_HMapNode *tda_hmap_first_node(const tda_HMap *self);

/// the first entry of a walk, to write or remove through
/// @copydetails tda_hmap_first_node
[[nodiscard]] TDA_API
tda_HMapNode *tda_hmap_first_node_mut(tda_HMap *self);

/// the entry after 'node'
/// @param self the map, needed to step past the end of a chain into the next bucket
/// @param node the position to step from
/// @return the next node, or null at the end
/// @warning an insert during a walk may grow the table, which relinks every entry, and
///          the walk then skips some and repeats others. Collect what to insert first
/// @bigo{1} amortized over a whole walk, which is n plus the bucket count
[[nodiscard]] TDA_API
const tda_HMapNode *tda_hmap_node_next(const tda_HMap *self, const tda_HMapNode *node);

/// the entry after 'node', to write or remove through
/// @copydetails tda_hmap_node_next
[[nodiscard]] TDA_API
tda_HMapNode *tda_hmap_node_next_mut(tda_HMap *self, tda_HMapNode *node);

/// the key at 'node'
/// @param self the map 'node' is in, which knows where in a node its key lies
/// @param node the position
/// @return the key, read-only; there is no node_key_mut, as a changed key would sit in
///         the wrong bucket
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_hmap_node_key(const tda_HMap *self, const tda_HMapNode *node);

/// the value at 'node'
/// @param self the map, which knows where in a node its value lies
/// @param node the position
/// @return the value
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_hmap_node_val(const tda_HMap *self, const tda_HMapNode *node);

/// the value at 'node', to write through
/// @copydetails tda_hmap_node_val
[[nodiscard]] TDA_API
void *tda_hmap_node_val_mut(tda_HMap *self, tda_HMapNode *node);

/// @}

/// @name mods
/// @{

/// stores 'val' under 'key', overwriting the value if the key is present
/// @param self the map
/// @param key the key to copy in
/// @param val the value to copy in
/// @param[out] out_is_new whether the key was absent; may be null, set only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when a node or the bucket array cannot be allocated
/// @bigo{1} expected, plus the amortized cost of growing
[[nodiscard]] TDA_API
tda_Status tda_hmap_insert(tda_HMap *self, const void *key, const void *val, bool *out_is_new);

/// tda_hmap_insert of keys[i] -> vals[i] for every i, in order
/// @param self the map
/// @param keys the keys, of the map's key layout; the last value given for a key wins
/// @param vals the values, of the map's value layout; asserts the same len as 'keys'
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when a node or the bucket array cannot be allocated;
///         the pairs before the failing one stay inserted
/// @bigo{n} expected, plus the amortized cost of growing
[[nodiscard]] TDA_API
tda_Status tda_hmap_extend(tda_HMap *self, tda_Span keys, tda_Span vals);

/// drops the entry under 'key' if there is one
/// @param self the map
/// @param key the key to drop
/// @return whether there was an entry
/// @bigo{1} expected
TDA_API
bool tda_hmap_remove(tda_HMap *self, const void *key);

/// drops the entry under 'key' if there is one, copying out its key and value first, so
/// the caller can free what they own
/// @param self the map
/// @param key the key to drop
/// @param[out] out_key the stored key, which may differ from 'key' in what 'eq' ignores;
///                     may be null or 'key' itself, written only on a hit
/// @param[out] out_val the stored value; may be null, written only on a hit
/// @return whether there was an entry
/// @bigo{1} expected
TDA_API
bool tda_hmap_take(tda_HMap *self, const void *key, void *out_key, void *out_val);

/// drops the entry 'node' names
/// @param self the map
/// @param node a position in this map (checked); pointers into it become invalid, other
///             entries are untouched. The bucket comes from the cached hash, so the key
///             is not hashed again
/// @bigo{1} expected; the bucket is walked to find the link to the node
TDA_API
void tda_hmap_remove_node(tda_HMap *self, tda_HMapNode *node);

/// keeps the entries 'pred' passes and drops the rest
/// @param self the map; kept nodes do not move, so pointers into them stay valid
/// @param pred asked once per entry, in walk order; must not change the map
/// @param ctx handed to 'pred'
/// @bigo{n} plus the bucket count
TDA_API
void tda_hmap_retain(tda_HMap *self, tda_EntryPred pred, void *ctx);

/// the entry for 'key', first inserted with 'val_if_absent' if missing (for counting)
/// @param self the map
/// @param key the key to look for or copy in
/// @param val_if_absent the starting value for a missing key; ignored otherwise
/// @param[out] out_node the entry either way, written only on TDA_STATUS_OK
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when a node or the bucket array cannot be allocated
/// @bigo{1} expected; one hash and one walk, where a lookup then an insert does both
///          twice for a missing key
[[nodiscard]] TDA_API
tda_Status tda_hmap_get_or_insert(
    tda_HMap *self,
    const void *key,
    const void *val_if_absent,
    tda_HMapNode **out_node
);

/// drops every entry, keeping the buckets
/// @param self the map
/// @bigo{n} plus the bucket count
TDA_API
void tda_hmap_clear(tda_HMap *self);

/// makes room for 'new_cap' entries without a growth in between
/// @param self the map
/// @param new_cap a capacity at or below the one it has is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the array cannot be allocated; the map is unchanged
/// @bigo{n}; the nodes are relinked, not reallocated, so borrowed nodes survive
[[nodiscard]] TDA_API
tda_Status tda_hmap_reserve(tda_HMap *self, size_t new_cap);

/// gives back unneeded buckets, down to the smallest power of two that holds the
/// entries, but never below the count a fresh map starts with
/// @param self an empty map is left owning no buckets
/// @retval TDA_STATUS_OK on success, including when there was nothing to give back
/// @retval TDA_STATUS_ERR_NO_MEM when the smaller array is refused; the map is unchanged
/// @bigo{n}; the nodes are relinked, not rebuilt, so borrowed nodes survive
[[nodiscard]] TDA_API
tda_Status tda_hmap_shrink_to_fit(tda_HMap *self);

/// exchanges the contents of two maps, hashers, equalities and ctx included
/// @param self one map
/// @param other must have the same key and value layouts; may be 'self'. The nodes do not
///              move, so borrowed nodes stay valid. Different hashers and allocators are
///              fine, as each travels with its entries
/// @bigo{1}
TDA_API
void tda_hmap_swap(tda_HMap *self, tda_HMap *other);

/// @}

/// @name print
/// @{

/// writes the entries as {a: 1, b: 2}, with no newline, in walk order
/// @param self the map
/// @param stream where to write
/// @param key_fprint the printer for a key
/// @param key_ctx passed to every call of 'key_fprint'; may be null
/// @param val_fprint the printer for a value
/// @param val_ctx passed to every call of 'val_fprint'; may be null
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
TDA_API
int tda_hmap_fprint(
    const tda_HMap *self, FILE *stream,
    tda_FPrint key_fprint, void *key_ctx,
    tda_FPrint val_fprint, void *val_ctx
);

/// tda_hmap_fprint to stdout, and a newline
/// @param self the map
/// @param key_fprint the printer for a key
/// @param key_ctx passed to every call of 'key_fprint'; may be null
/// @param val_fprint the printer for a value
/// @param val_ctx passed to every call of 'val_fprint'; may be null
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
TDA_API
int tda_hmap_print(
    const tda_HMap *self,
    tda_FPrint key_fprint, void *key_ctx,
    tda_FPrint val_fprint, void *val_ctx
);

/// @}

/// @name macros
/// @{

/// @cond
// return 'self' after checking the key or the value layout against TDA_LAYOUT_OF of
// the type, so a macro evaluates its pointer once; _Generic keeps the pointer's constness.
// A macro that knows both types checks one and then the other
[[nodiscard]]
static inline const tda_HMap *tda_hmap_key_typed_(const tda_HMap *self, [[maybe_unused]] tda_Layout key) {
    TDA_EXPECT(tda_layout_eq(tda_hmap_key_layout(self), key));
    return self;
}

[[nodiscard]]
static inline tda_HMap *tda_hmap_key_typed_mut_(tda_HMap *self, [[maybe_unused]] tda_Layout key) {
    TDA_EXPECT(tda_layout_eq(tda_hmap_key_layout(self), key));
    return self;
}

[[nodiscard]]
static inline const tda_HMap *tda_hmap_val_typed_(const tda_HMap *self, [[maybe_unused]] tda_Layout val) {
    TDA_EXPECT(tda_layout_eq(tda_hmap_val_layout(self), val));
    return self;
}

[[nodiscard]]
static inline tda_HMap *tda_hmap_val_typed_mut_(tda_HMap *self, [[maybe_unused]] tda_Layout val) {
    TDA_EXPECT(tda_layout_eq(tda_hmap_val_layout(self), val));
    return self;
}

#define TDA_HMAP_KEY_TYPED_(K, self) \
    _Generic((self), const tda_HMap *: tda_hmap_key_typed_, tda_HMap *: tda_hmap_key_typed_mut_)((self), TDA_LAYOUT_OF(K))

#define TDA_HMAP_VAL_TYPED_(V, self) \
    _Generic((self), const tda_HMap *: tda_hmap_val_typed_, tda_HMap *: tda_hmap_val_typed_mut_)((self), TDA_LAYOUT_OF(V))

#define TDA_HMAP_TYPED_(K, V, self) \
    TDA_HMAP_VAL_TYPED_(V, TDA_HMAP_KEY_TYPED_(K, self))
/// @endcond

/// tda_hmap_init with TDA_LAYOUT_OF(K) and TDA_LAYOUT_OF(V)
/// @param K the key type
/// @param V the value type
/// @param hasher the hash the keys are placed by
/// @param eq the equality the keys are matched by
/// @param ctx passed to every call of 'hasher' and 'eq'; may be null
/// @param al the allocator
/// @bigo{1}
#define TDA_HMAP_INIT(K, V, hasher, eq, ctx, al) \
    tda_hmap_init(TDA_LAYOUT_OF(K), TDA_LAYOUT_OF(V), (hasher), (eq), (ctx), (al))

/// tda_hmap_init_cap with TDA_LAYOUT_OF(K) and TDA_LAYOUT_OF(V)
/// @param K the key type
/// @param V the value type
/// @param cap how many entries to make room for
/// @param hasher the hash the keys are placed by
/// @param eq the equality the keys are matched by
/// @param ctx passed to every call of 'hasher' and 'eq'; may be null
/// @param al the allocator
/// @param[out] out where the new map is written
/// @bigo{n}
#define TDA_HMAP_INIT_CAP(K, V, cap, hasher, eq, ctx, al, out) \
    tda_hmap_init_cap((cap), TDA_LAYOUT_OF(K), TDA_LAYOUT_OF(V), (hasher), (eq), (ctx), (al), (out))

/// tda_hmap_get as a const V *, from a key value rather than an address
/// @param K the key type; 'key' may be any K, a struct literal in parentheses too
/// @param V the value type
/// @param self the map
/// @param key the value to look for
/// @bigo{1} expected
#define TDA_HMAP_GET_AS(K, V, self, key) \
    ((const typeof(V) *) tda_hmap_get(TDA_HMAP_TYPED_(K, V, self), &(K[1]){ (key) }[0]))

/// tda_hmap_get_mut as a V *
/// @copydetails TDA_HMAP_GET_AS
#define TDA_HMAP_GET_MUT_AS(K, V, self, key) \
    ((V *) tda_hmap_get_mut(TDA_HMAP_TYPED_(K, V, self), &(K[1]){ (key) }[0]))

/// tda_hmap_contains from a key value rather than an address
/// @param K the key type; 'key' may be any K, a struct literal in parentheses too
/// @param self the map
/// @param key the value to look for
/// @bigo{1} expected
#define TDA_HMAP_CONTAINS(K, self, key) \
    tda_hmap_contains(TDA_HMAP_KEY_TYPED_(K, self), &(K[1]){ (key) }[0])

/// tda_hmap_find from a key value rather than an address
/// @copydetails TDA_HMAP_CONTAINS
#define TDA_HMAP_FIND(K, self, key) \
    tda_hmap_find(TDA_HMAP_KEY_TYPED_(K, self), &(K[1]){ (key) }[0])

/// tda_hmap_find_mut from a key value rather than an address
/// @copydetails TDA_HMAP_CONTAINS
#define TDA_HMAP_FIND_MUT(K, self, key) \
    tda_hmap_find_mut(TDA_HMAP_KEY_TYPED_(K, self), &(K[1]){ (key) }[0])

/// tda_hmap_insert from values rather than addresses
/// @param K the key type; 'key' may be any K, a struct literal in parentheses too
/// @param V the value type; 'val' may be any V, a struct literal in parentheses too
/// @param self the map
/// @param key the key value to copy in
/// @param val the value to copy in
/// @param[out] out_is_new whether the key was absent; may be null
/// @bigo{1} expected
#define TDA_HMAP_INSERT(K, V, self, key, val, out_is_new) \
    tda_hmap_insert(TDA_HMAP_TYPED_(K, V, self), &(K[1]){ (key) }[0], &(V[1]){ (val) }[0], (out_is_new))

/// tda_hmap_get_or_insert from values rather than addresses
/// @param K the key type; 'key' may be any K, a struct literal in parentheses too
/// @param V the value type; 'val' may be any V, a struct literal in parentheses too
/// @param self the map
/// @param key the key value to look for or copy in
/// @param val the value to start from when the key is absent
/// @param[out] out_node the entry either way
/// @bigo{1} expected
#define TDA_HMAP_GET_OR_INSERT(K, V, self, key, val, out_node) \
    tda_hmap_get_or_insert(TDA_HMAP_TYPED_(K, V, self), &(K[1]){ (key) }[0], &(V[1]){ (val) }[0], (out_node))

/// tda_hmap_remove from a key value rather than an address
/// @copydetails TDA_HMAP_CONTAINS
#define TDA_HMAP_REMOVE(K, self, key) \
    tda_hmap_remove(TDA_HMAP_KEY_TYPED_(K, self), &(K[1]){ (key) }[0])

/// tda_hmap_take from a key value rather than an address, with 'out_key' and 'out_val'
/// typechecked as K * and V *
/// @param K the key type; 'key' may be any K, a struct literal in parentheses too
/// @param V the value type
/// @param self the map
/// @param key the key to drop
/// @param[out] out_key the stored key, or nullptr
/// @param[out] out_val the stored value, or nullptr
/// @bigo{1} expected
#define TDA_HMAP_TAKE(K, V, self, key, out_key, out_val) \
    tda_hmap_take(TDA_HMAP_TYPED_(K, V, self), &(K[1]){ (key) }[0], (K *){ (out_key) }, (V *){ (out_val) })

/// @cond
// the body of both walks; the hidden names come in already unique, and 'self' is
// read once
#define TDA_HMAP_WALK_(SelfT, NodeT, node, self, first, next, hmap, once) \
    for (SelfT *hmap = (self), **once = &hmap; once; once = nullptr)      \
        for (NodeT *node = first(hmap); node; node = next(hmap, node))
/// @endcond

/// walks every entry, binding 'node' to each in turn, in unspecified (bucket) order
/// @param node the name of the loop variable, a const tda_HMapNode *
/// @param self the map
/// @warning the loop steps through 'node' after the body, so removing it inside the loop
///          uses a freed node. To remove while walking, take the next node first:
/// @code
/// for (tda_HMapNode *node = tda_hmap_first_node_mut(m), *next; node; node = next) {
///     next = tda_hmap_node_next_mut(m, node);
///     if (drop_it(node)) {
///         tda_hmap_remove_node(m, node);
///     }
/// }
/// @endcode
/// @warning inserting inside the loop is not allowed either; see tda_hmap_node_next
#define TDA_HMAP_FOR_EACH(node, self) \
    TDA_HMAP_WALK_(                   \
        const tda_HMap,               \
        const tda_HMapNode,           \
        node,                         \
        self,                         \
        tda_hmap_first_node,          \
        tda_hmap_node_next,           \
        TDA_LINE_NAME_(hmap_),        \
        TDA_LINE_NAME_(once_)         \
    )

/// the same walk over entries whose values may be written through
/// @copydetails TDA_HMAP_FOR_EACH
#define TDA_HMAP_FOR_EACH_MUT(node, self) \
    TDA_HMAP_WALK_(                       \
        tda_HMap,                         \
        tda_HMapNode,                     \
        node,                             \
        self,                             \
        tda_hmap_first_node_mut,          \
        tda_hmap_node_next_mut,           \
        TDA_LINE_NAME_(hmap_),            \
        TDA_LINE_NAME_(once_)             \
    )

/// tda_hmap_node_key as a const K *
/// @param K the key type
/// @param self the map 'node' is in
/// @param node the position
/// @bigo{1}
#define TDA_HMAP_NODE_KEY_AS(K, self, node) \
    ((const typeof(K) *) tda_hmap_node_key(TDA_HMAP_KEY_TYPED_(K, self), (node)))

/// tda_hmap_node_val as a const V *
/// @param V the value type
/// @param self the map
/// @param node the position
/// @bigo{1}
#define TDA_HMAP_NODE_VAL_AS(V, self, node) \
    ((const typeof(V) *) tda_hmap_node_val(TDA_HMAP_VAL_TYPED_(V, self), (node)))

/// tda_hmap_node_val_mut as a V *
/// @copydetails TDA_HMAP_NODE_VAL_AS
#define TDA_HMAP_NODE_VAL_MUT_AS(V, self, node) \
    ((V *) tda_hmap_node_val_mut(TDA_HMAP_VAL_TYPED_(V, self), (node)))

/// @}

/// @}
