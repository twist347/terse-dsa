# Changelog

## 3.1.0 — 2026-10-26

Additions only, and one change of placement that nothing promised.

### Added

- **Typed macros over algo.** `TDA_SPAN_FIND`, `TDA_SPAN_FIND_LAST`, `TDA_SPAN_FIND_RUN`,
  `TDA_SPAN_CONTAINS`, `TDA_SPAN_COUNT`, `TDA_SPAN_LOWER_BOUND`, `TDA_SPAN_UPPER_BOUND`,
  `TDA_SPAN_EQUAL_RANGE`, `TDA_SPAN_BINARY_SEARCH` and `TDA_SPAN_REMOVE` take the key by
  value, `TDA_SPAN_FILL` and `TDA_SPAN_REPLACE_IF` the value, and `TDA_SPAN_REPLACE`
  both, where the functions want addresses. The first argument is the key's type, which
  need not be the elem's: a span of records is searched with a bare id.
- **`tda_list_pop_front_to` and `tda_list_pop_back_to`**, with their `*_TO` macros: the
  list was the one container that could drop an end but not hand it out.
- **`extend` on `tda_List`, `tda_Stack`, `tda_Queue` and `tda_PQueue`**, with their
  `*_EXTEND` macros, as vec and deque have it. Each is all or nothing: the adapters grow
  once, and the list makes its nodes apart and links them in one go. A pqueue extended
  from empty heapifies once, in O(n).
- **`tda_bitset_from_span` and `TDA_BITSET_OF`** build a bitset from the indices it
  holds: `TDA_BITSET_OF(64, al, &b, 1, 5, 7)`.
- **`into_raw` and `init_raw` on vec and arr** move a vec's block out and back in with no
  copy: into_raw hands out the elems and the capacity and leaves the vec inert, init_raw
  takes over a block the caller allocated through the same allocator. `tda_arr_into_raw`
  and `tda_arr_init_raw` do the same for an arr, whose block is its length, so into_raw
  returns the span; after `tda_vec_shrink_to_fit`, a vec's block becomes an arr's with no
  copy. `from_data` stays the way to start from a buffer that is not such a block.

### Changed

- **hmap and hset mix the hash before they pick a bucket.** The bucket is the top bits of
  the hash times 2^64/phi, where it was the low bits, so a hasher only has to tell keys
  apart: an identity hasher on keys that differ only in high bits no longer puts them all
  in one chain. The cost is a multiply per lookup: with the hashers of core/hash, which
  were fine either way, a lookup measured the same under clang and about 6% slower under
  gcc, an insert the same under both. Which bucket a key lands in, and so the walk order,
  changes, which the Compatibility section of the README never promised.

## 3.0.0 — 2026-10-26

2.0 made containers and allocators values, but an allocator kept its interface inside
itself, pointing back at it, so it could never move, and every constructor wrote through
`out` only because an allocator could not be returned. 3.0 takes the interface out of
the allocator and settles what 2.0 left to accident: one rule for what a function
returns, the order a key reaches a comparator in, and printers that can be told things.

### Changed

- **`tda_Al` is a view, passed by value.** It is two pointers, the allocator's state and a
  `const tda_AlVtable *` of its hooks, one static table for each kind of allocator.
  `tda_arena_to_al(&arena)`, `tda_pool_to_al`, `tda_slab_to_al`, `tda_debug_to_al` and
  `tda_log_to_al` hand it out, named as `to_span` is: the object as another type;
  containers keep it by value, and every function that took a `tda_Al *` takes a `tda_Al`.
  An allocator holds no pointer to itself any more, so it moves and copies as any value
  does until a view is out, and must stay where it is while one is, the rule of a span. As
  with a span, nothing checks it: the check of 2.0 that a moved allocator's own functions
  caught is gone, and so is the address an arena mark kept, so a mark from another arena
  is no longer caught. `tda_al_default()` returns its view, and its hooks can no longer be
  overwritten through the pointer it used to return. A container's `tda_<x>_al` accessor
  returns the view it was built with, and `tda_span_inplace_merge` takes `(tda_Al){}` for
  no buffer, where it took `nullptr`.
- **An allocation through a view costs one more load**, the table's address: a loop of
  nothing but allocs and frees on a pool or an arena runs about a tenth slower, and
  nothing that does work between them notices.
- **A function returns its result**; one that can fail returns a `tda_Status` and writes
  through a trailing `out`, as before. Constructors are no longer the exception:
  `tda_Vec v = tda_vec_init(elem, al)`, as for list, deque, hmap, hset, stack, queue and
  pqueue, and their `TDA_*_INIT` macros take no `out`; `tda_stack_into_vec`,
  `tda_queue_into_deque` and `tda_pqueue_into_vec` return what they hand over, and are
  `[[nodiscard]]`, as is every `tda_<x>_to_al`: a dropped `into_*` result leaks. The
  allocators follow: `tda_Arena a = tda_arena_init_buf(buf, cap)`,
  `tda_Slab s = tda_slab_init(parent)`, `tda_Debug d = tda_debug_init(parent)` and
  `tda_Log l = tda_log_init(stream, parent)`, which took the parent first.
- **A bare `init` never fails**, now for allocators too: `tda_arena_init` is
  `tda_arena_init_cap(cap, parent, &out)` and `tda_pool_init` is
  `tda_pool_init_cap(block_count, block, parent, &out)`. The parent comes last in every
  allocator's constructor, where a container takes its allocator.
- **An op that takes a key passes it to `cmp` or `eq` on the right**, the elem on the
  left, so the key need not be an elem: a span of users sorted by id is searched with the
  id. `tda_span_upper_bound` and `tda_span_binary_search` used to ask with the key on the
  left, to keep a comparator that only answers "less" working; that is no longer
  promised, and a comparator answers all three ways, as `tda_Cmp` says.
- **A printer is told things.** `tda_FPrint` is `int (*)(FILE *, const void *val,
  void *ctx)` and returns what `fprintf` does; every `fprint` and `print` takes the ctx
  after the printer, a map's one for each of its two, and returns negative if a write
  failed. A container's or a span's `fprint` writes no newline, so it can print an elem
  of another container; `print` ends the line. A bitset has no printer to hand one, and
  its `fprint` and `print` only return the `int`. The span printers moved from
  `core/span` to `core/print`. `tda_debug_fprint_leaks` returns the same `int` instead
  of the number of leaks, which `tda_debug_stats` gives, and `alloc/log` no longer
  prints the address of its parent, which a view no longer has.
- **A typed macro takes a struct.** `TDA_VEC_PUSH(Pair, &v, p)` and
  `TDA_HMAP_INSERT(Pair, Pair, &m, k, ((Pair){1, 2}), nullptr)` work, as does every
  macro that takes one value or key: the value goes in as `&(T[1]){ (val) }[0]`, where
  `&(T){ (val) }` poured a struct into its first field and did not compile. A scalar
  passed for a struct, which filled its first field silently, now draws
  `-Wmissing-braces`.
- `tda_span_replace_if(s, val, pred, ctx)` takes the value before the predicate, as
  every other op takes its data before the callback and its ctx.
- `tda_cmp_char` orders as `unsigned char` on every target, as `strcmp` orders the
  chars of a string and `tda_hash_char` reads them. Before, a byte past 0x7f sorted
  first on x86 and last on ARM.
- `tda_HSetNode` is a type of its own, no longer a name for `tda_HMapNode`: a set node
  does not reach a map function, nor a map node a set one.
- `tda_debug_fail_after` takes a `uint64_t`, as `tda_debug_break_at` does, and
  `UINT64_MAX` turns refusing off.
- A shared build's soname carries `MAJOR.MINOR`: the promise is the API, so a minor may
  add a struct field, and a program built against another minor refuses to load rather
  than read a struct of the wrong size.

### Removed

- `TDA_AL(&x)`: each allocator has its own `tda_<slug>_to_al`.
- The check that a moved or copied allocator is caught, and its test.

### Added

- `TDA_HSET_OF`, a set from the keys written out, as every other container has its
  `TDA_*_OF`.
- `tda_AlVtable`, the hooks of one kind of allocator, and `tda_al_eq`, whether two views
  are of one allocator. A list decides by it whether a splice relinks or copies.
- `core/align`: `tda_align_up`, `tda_align_ckd_up`, `tda_align_pad` and
  `tda_align_meets`, the arithmetic an allocator of your own needs.
- `tda_span_overlap` and `tda_span_apart_or_same`.

### Documentation

- README: a Compatibility section, what a major keeps and what it does not: exact
  capacities, hash values and walk order, the draws built on a seed past a minor, text
  meant for people, what a header pulls in, and the toolchain floor.
- conventions: the return rule and the shapes that only look like exceptions to it; an
  elem is bytes for good; a status is tested with `TDA_STATUS_IS_ERR`, and
  `TDA_STATUS_ERR_NO_MEM` stands for an overflowing size as well; every callback takes a
  ctx, a printer included.

## 2.0.0 — 2026-10-23

Four changes break the API, each for a fault that 1.x could not fix without breaking
it: every container and allocator was a handle the library allocated, so even an empty
one could fail to be made and none could live inside a struct of the caller's; an
allocator could not tell "not in place" from "out of memory"; an elem aligned past
`max_align_t` sat wherever malloc put it; and a comparator could depend on nothing but
its operands.

### Changed

- **Containers and allocators are values.** `tda_Vec`, `tda_HMap`, `tda_Arena` and the
  rest are structs in their headers that the caller holds — on the stack, in a struct,
  in another container — built in place by `init` and released by `deinit`, which leaves
  `= {}`, the inert state, and takes it again as a no-op. Nothing is allocated for the
  object itself: an empty container allocates nothing and its `init` returns `void`, an
  adapter holds its inner container in place, and a list on a pool needs blocks of one
  node, no longer of the list struct too. Every constructor still writes through `out`.
  The fields end in `_` and belong to the type. A copy of the struct is a second owner,
  which each type warns of. With the struct public, the accessors that only read or
  index — `len`, `get`, `front`, `to_span` and kin — are `static inline` in the header,
  so `TDA_VEC_GET_AS` in a loop runs about five times faster.
- **The allocator is part of a container's value.** `move_assign` and `swap` carry it
  along with the elems, as Rust's `Vec<T, A>` does, so both are O(1) and cannot fail
  whatever the two allocators: `move_assign` returns `void`, and `swap` no longer asserts
  that the allocators match. `copy_assign` keeps the target's.
- **An allocator is a type of its own, built in place, and hands out its interface**:
  `tda_arena_init(parent, cap, &arena)`, then `TDA_AL(&arena)` for the `tda_Al *`. The
  names lose their `al_`: `tda_Arena`, `tda_Pool`, `tda_Slab`, `tda_Debug` and
  `tda_Log`, `tda_arena_*` where it was `tda_al_arena_*`, and `tda_ArenaStats` where it
  was `tda_AlArenaStats`. Its ops take it by its own type —
  `tda_arena_mark(&arena)`, `tda_pool_stats(&pool)`, `tda_debug_fail_after(&debug, n)`
  — so the compiler checks what used to be checked at run time. It must not move once built: its `tda_Al`
  points back at it. `from_buf` is `init_buf`, since it lends memory rather than copying
  anything in, and gives the whole buffer to the blocks, with no header carved from
  its front.

- **`tda_Al` takes Zig's hooks and a layout in every call.** The table is `alloc`,
  `alloc_zeroed`, `resize`, `remap` and `dealloc`, each given a `tda_Layout`, the size and
  the alignment the block was made with. `resize` grows or shrinks a block only in place
  and answers `bool`; `remap` may move it and answers null when it has no way cheaper
  than a copy. Before, one null from `realloc` meant both "not in place" and "out of
  memory". Only `alloc` and `dealloc` are required: a missing hook means "never", and
  the wrappers make do. Null from `alloc` now means the request cannot be met — out of
  memory, or a size past a fixed-size allocator's block — and nothing else; an alignment
  an allocator was not built to give asserts.
- The wrappers follow: `tda_alloc(al, layout)`, `tda_alloc_zeroed`, `tda_resize`,
  `tda_remap`, `tda_realloc(al, ptr, old, new_size)` and `tda_dealloc(al, ptr, layout)`.
  `tda_realloc` tries `resize`, then `remap`, then alloc, copy and dealloc. The typed
  macros take the layout from the type: `TDA_ALLOC`, `TDA_ALLOC_ZEROED`, `TDA_REALLOC`,
  `TDA_DEALLOC`.
- **Every container is built on the layout of its elem**, not its size:
  `tda_vec_init(TDA_LAYOUT_OF(T), al, &v)`, and the typed macros pass it themselves.
  Buffers and nodes are aligned to it, so an elem with `alignas(64)` lands on a multiple
  of 64 in a vec, a deque, a list or an hmap, on any allocator. Each container has an
  `elem_layout` accessor (`key_layout` and `val_layout` for hmap, `key_layout` for hset),
  and the typed accessors check the alignment as well as the size.
- **A span carries the layout of its elems**: `tda_Span` is `{data, len, elem}`, and
  `tda_span_from_data(data, len, TDA_LAYOUT_OF(T))` builds one; the typed macros pass
  it. So nothing guesses an alignment: `from_span` and `hmap_from_spans` take the layout
  off the span, and an algo that copies elems aside, as `sort_stable` does, asks for
  exactly theirs. Two spans or a span and a container meet only on the same layout, and
  a span's data has to sit on its elem's alignment, which its constructors assert.
- **A node lays its elem out as a struct would.** list and hmap put the elem, or the key
  and the value, after the links at the first offset their alignment allows, instead of
  always at 16. An hmap node of `int32_t` keys and values is 24 bytes, not 32. The
  offset is the container's, so `tda_list_node_elem(self, node)`,
  `tda_hmap_node_key(self, node)` and `tda_hset_node_key(self, node)` take it, and the
  `NODE_ELEM_AS` and `NODE_KEY_AS` macros take it and check the type with it.
- **Every comparator, equality and hasher takes a `void *ctx`**, and every function that
  takes one takes the ctx right after it: `tda_span_sort(s, cmp, ctx)`. An order can
  depend on more than its operands, as keys to sort indices by, with no global. The
  ready-made `tda_cmp_*`, `tda_eq_*` and `tda_hash_*` ignore it and are passed `nullptr`;
  they no longer fit `qsort`. pqueue stores its ctx with its comparator, hmap and hset
  one ctx with their hasher and equality, and copies, moves and swaps carry it along.
- `alloc/default` serves any alignment: up to malloc's own through the plain calls, a
  block shorter than its alignment asking for a whole `max_align_t`, which C23 owes that
  alignment; past it through `aligned_alloc`, or `_aligned_malloc` on Windows. Each
  block goes back the way it was made.
- `alloc/arena` aligns each block to what it asks for and rounds nothing up, so small
  blocks sit back to back; the cap is taken as given, and the whole block asks its
  parent for no alignment, so any parent serves it.
- `alloc/pool` is built on a block layout, `tda_pool_init(parent, block, count, &pool)`
  and `tda_pool_init_buf(buf, size, block, &pool)`, so it holds one type at that type's
  alignment; `tda_PoolStats` reports `block_align`. A block given back with a layout
  past the pool's blocks fails `TDA_EXPECT`.
- `alloc/slab` serves an alignment from the classes it divides, and passes one past
  512 to the parent.
- `alloc/debug` records the alignment too and stops on a block given back with another,
  and checks the old layout of a realloc before anything is copied.
- `alloc/log` writes a line per hook: `alloc`, `alloc_zeroed`, `resize` (in place or
  refused), `remap` and `dealloc`, each with its alignment. A log read by a script needs
  the new spelling.
- `tda_hmap_eq`, `tda_hset_eq` and the set ops take containers whose key ctx differ, a
  seed each, as long as their equalities agree.
- **A few names follow the new rules.** `tda_bitset_new` is `tda_bitset_init_len`, as
  `tda_arr_init_len`, so a bare `init` never fails. The `elem_size`, `key_size` and
  `val_size` accessors are gone: the layout accessors give the size with the alignment,
  `tda_vec_elem_layout(&v).size`. `tda_hset_intersect` and `tda_bitset_intersect` are
  `intersection`, as `tda_span_set_intersection` is.
- `tda_list_remove` is `tda_list_remove_node`, as `insert_before` and `splice_node` name
  the node they take. `tda_ArenaStats.available` is `free` and `tda_PoolStats.block_count`
  is `blocks`, matching the `used` and `free` beside them. The state of `tda_Rng` and
  the fields of `tda_ArenaMark` end in `_`, as every field nobody reads directly does.
- `tda_bitset_clear` and `tda_bitset_clear_all` are now `tda_bitset_reset` and
  `tda_bitset_reset_all`. Everywhere else in the library clear empties a container;
  a bit is reset, as in `std::bitset`.

### Removed

- `alloc/aligned`: every allocator takes the alignment per call now, so a wrapper that
  forces one on every block has nothing left to do.
- `tda_calloc` and `TDA_CALLOC`, replaced by `tda_alloc_zeroed` and `TDA_ALLOC_ZEROED`.
- `tda_span_find_sub_last`, deprecated since 1.3.0; `tda_span_find_last_sub` replaces it.

### Added

- `core/layout`: `tda_Layout`, `TDA_LAYOUT_OF(T)`, the predicates `tda_layout_is_align`,
  `tda_layout_is_type` and `tda_layout_eq`, and checked arithmetic for building one:
  `tda_layout_ckd_array`, `tda_layout_ckd_extend` and `tda_layout_ckd_pad`, which lay
  out an array or a struct's members as C does.
- `tda_list_node_layout`, the block a list needs from an allocator whose blocks are all
  one layout, such as `alloc/pool`: the layout of one node. Before, the node size was
  unwritten, so the block was found by trial.
- `tda_vec_push_assume_cap`, `tda_vec_add_one` and `tda_vec_add_one_assume_cap`, as
  Zig's `ArrayList` has them: after one `tda_vec_reserve`, a push into the reserved room
  cannot fail and returns nothing to check, and `add_one` hands out the new elem's slot
  to fill in place rather than copy a value in. `TDA_VEC_PUSH_ASSUME_CAP` and
  `TDA_VEC_ADD_ONE_ASSUME_CAP_AS` are their typed forms.
- `tda_span_split_at` and `tda_span_split_at_mut`, Rust's `split_at`: a view cut in two
  at an index, in O(1).
- `tda_pqueue_cmp_ctx`, `tda_hmap_key_ctx` and `tda_hset_key_ctx`.
- `tda_debug_flush_quarantine`, to give the quarantined blocks back before the parent is
  reset or rewound: an arena's reset would otherwise hand them out again while the
  quarantine still watches them, and report the new owner's writes as misuse.
- `TDA_AL(&x)`, the `tda_Al *` of a `tda_Arena`, `tda_Pool`, `tda_Slab`, `tda_Debug` or
  `tda_Log`; any other type does not compile.
- A moved or copied allocator is caught under `TDA_HARDENED` as well as under assert:
  each of its own functions, `deinit` included, checks that its `tda_Al` still points
  back at it.

### Fixed

- `tda_span_upper_bound`, `tda_span_equal_range` and `tda_span_binary_search` asked the
  comparator for "equal" as well as "less", so one that never answers "greater", as a
  C++ less-than, put the upper bound past larger elems and found keys that were not
  there. So did `tda_span_inplace_merge` without a buffer, which bisects through them.
  The bisecting ops now ask only "less", and `binary_search` reports the first of
  equal elems.
- `tda_vec_insert` and `tda_deque_insert` asserted that 'val' sat on the elem's
  alignment, which push and set do not ask; a value read from a packed buffer now
  inserts as it pushes.

### Documentation

- `algo/sort` states what it needs from a comparator, a strict weak ordering, and what
  happens under one that is not: the order is unspecified, but no elem is lost or
  duplicated and nothing outside the span is read. A test holds it to that with a
  comparator that answers at random. Its example sorts indices by keys in the ctx.
- The allocator wrappers give their complexity.

## 1.6.0 — 2026-10-22

### Added

- **`alloc/slab`**, an allocator that keeps small blocks in pools by size class, from
  16 to 512 bytes, and passes larger ones to its parent. Every container runs on it with
  no tuning: the struct and the nodes of a list or an hmap go to pools, the buffer of a
  grown vec to the parent. Pools take 4 KiB chunks as they need them, a freed block goes
  back to its own pool, and no block carries a header. Over the default allocator it
  runs a list about twice as fast and an hmap about a third faster; the memory of a
  peak stays held until the drop.

## 1.5.0 — 2026-10-22

### Added

- **`alloc/debug`**, an allocator that checks how another one is used, for builds
  without sanitizers. It stops the program with a report on a wrong size in a dealloc or
  realloc, a double free, a pointer it never handed out, a write past the end of a block
  and a write after a free, which a quarantine of freed blocks keeps checkable. New
  blocks are filled with 0xCD and freed ones with 0xDD, and a realloc always moves, so a
  pointer kept across a growth always reads freed memory.
  `tda_al_debug_fprint_leaks` lists the live blocks by serial, `tda_al_debug_break_at`
  stops at a serial so a debugger shows where a block was made, and
  `tda_al_debug_fail_after` refuses requests to walk the out-of-memory paths.

### Changed

- `tda_list_new` refuses an elem size whose node would not fit in `size_t` with
  `TDA_STATUS_ERR_NO_MEM`, as `tda_hmap_new` and `tda_hset_new` do; the first push used
  to be the one to fail.

## 1.4.1 — 2026-10-22

### Fixed

- `tda_arr_set` takes a value that is the very elem it overwrites, as `tda_vec_set` and
  `tda_deque_set` already did; that was a `memcpy` onto itself.
- The walk macros read their handle once. `TDA_DEQUE_FOR_EACH_AS`, `TDA_HMAP_FOR_EACH`,
  `TDA_HSET_FOR_EACH`, `TDA_BITSET_FOR_EACH` and their `_MUT` forms used to evaluate it
  on every step, so `ds[k++]` walked a mix of containers. Their hidden variables, and
  those of `TDA_SPAN_FOR_EACH_AS`, are named per line, so a walk nested in another no
  longer trips `-Wshadow`.
- `TDA_SWAP` rejects a const operand at compile time; it used to warn and then write
  through it.
- The tda_Al contract asks for blocks aligned to `alignof(max_align_t)`, and list, hmap,
  arena and pool check it with `TDA_EXPECT`, so hardened builds stop on a parent that
  aligns less. In release such a list used to crash later on a misaligned access.
  `tda_al_aligned_new(parent, alignof(max_align_t))` adapts one.
- Debug builds assert that an output of `tda_span_merge`, the set ops, `tda_span_copy`,
  `tda_span_sample`, `tda_span_swap_ranges`, `tda_span_partial_sum` and
  `tda_span_adjacent_difference` does not overlap an input in a way the op cannot take.
  Those used to give a wrong result in silence.
- Debug builds assert on a zeroed `tda_Rng`, which drew zeros forever and hung the bounded
  draws. A platform without C23 `_BitInt` fails the build with a message that says so.

### Documentation

- `alloc/aligned` promises over-alignment to the containers that keep one buffer;
  list, hmap and hset do not get it. `alloc/pool` says a list is the one container that
  runs on it, and the README no longer offers a pool as a drop-in.
- The arena warns that a container made before a mark must not grow before the rewind.
- list: merge, splice and splice_node relink only on one allocator and copy across two;
  `src` may not be `self`; debug builds walk the list to check a node.
- hmap and hset: no inserts during a walk; a `const char *` key keeps the pointer, not
  the text; the bucket comes from the low bits of the hash.
- Complexity: move_assign of list, hmap and hset frees the old nodes one by one; the set
  ops and `tda_hset_eq` walk the buckets as well as the keys.
- bitset: the assigns allocate too. `TDA_SPAN_FROM_DATA` lets a `void *` through; the
  `*_OF` macros need at least one elem. conventions.md says typed macros check the size
  of `T`, not the type.

## 1.4.0 — 2026-10-13

### Added

- **Building from spans.** `tda_hmap_from_spans` and `tda_hset_from_span` make a
  container from what spans view, taking the key and value sizes from them;
  `tda_hmap_extend`, `tda_hset_extend` and `TDA_HSET_EXTEND` insert a run into one that
  exists. A repeated key keeps its last value. On a refused allocation the pairs before
  the failing one stay inserted.
- **`retain` on every container with a predicate to filter by.** `tda_list_retain`,
  `tda_hmap_retain` and `tda_hset_retain` join those of `tda_Vec` and `tda_Deque`: the
  entries that fail are dropped in one pass, and the kept nodes do not move, so pointers
  into them stay valid.
- `tda_deque_to_spans` and `tda_deque_to_spans_mut` view a deque as the two runs it lies
  in, so the algo layer reaches it without a copy: a find or a fold runs on each half.
  The back run is empty unless the ring wraps.
- `tda_span_sample` copies a random selection into `dst`, none twice and in their order
  in `src`, in one pass that leaves `src` unchanged. Every pick of that size is equally
  likely.
- `tda_span_starts_with` and `tda_span_ends_with`, under an equality.

## 1.3.0 — 2026-10-08

### Added

- **Taking an elem out.** `pop_to` on `tda_Vec`, `tda_Stack`, `tda_Queue` and
  `tda_PQueue`, and `pop_front_to` / `pop_back_to` on `tda_Deque`, copy the elem out
  before dropping it, where a top + pop had to read it first. Each has a `*_POP_TO` macro
  that typechecks where it goes.
- `tda_vec_swap_remove` drops an elem in O(1), filling the gap with the back one.
- `tda_vec_truncate` and `tda_deque_truncate`: `resize` that only shrinks, and so cannot
  fail — no error branch that never runs after a `remove_if` or `unique`.
- `tda_vec_retain` and `tda_deque_retain` keep the elems a predicate passes, in order, in
  one pass. A deque has no span to hand to `remove_if`, so this is its only way to filter.
- `tda_deque_extend`, `tda_deque_insert_span` and `tda_deque_remove_range`, as `tda_Vec`
  has them: the room is taken once, and the shorter side moves once for the whole run.
- `tda_pqueue_replace_top` writes over the greatest elem and sifts once, where a pop and a
  push sift twice, and allocates nothing: the step of a top-k.
- `tda_hmap_take` and `tda_hset_take` drop an entry and hand out its stored key and value,
  for a container that owns what its entries point to and has to free it.
- **Set ops on `tda_HSet`:** `union`, `intersect`, `difference`, `symmetric_difference`,
  `is_subset` and `intersects`, the set `tda_BitSet` has. The two sets must agree on key
  size and equality. A refused allocation leaves `union` and `symmetric_difference` with
  a valid set, but not the result.
- `tda_bitset_find_prev` and `tda_bitset_find_prev_clear`, the scans down; a start past
  the universe begins at its last index.
- **Scratch memory in an arena.** `tda_al_arena_mark` and `tda_al_arena_rewind` take back
  what was handed out since the mark; marks nest. A block from before a mark that grows
  moves rather than growing in place past it, so a rewind never cuts it short.
- `tda_span_is_permutation`, std::is_permutation: the same elems in any order, each as
  many times, under an equality, with nothing allocated.
- `tda_span_find_last` and `tda_span_find_last_if`, the finds from the back.

### Deprecated

- `tda_span_find_sub_last` is now `tda_span_find_last_sub`, so that the direction comes
  first, as in the new finds and in C++23. The old name stays, exported and meaning the
  same, until 2.0 removes it; a call to it warns, which `-Werror` turns into an error.

### Fixed

- The typed macros' helpers no longer cast away `const`, so the headers are quiet under
  `-Wcast-qual`; 1.2.4 gave nine warnings there, an error under `-Werror`.

### Changed

- `tda_stack_pop`, `tda_queue_pop` and `tda_pqueue_pop` point to their `pop_to` for
  keeping the elem, where they told the caller to read it with `top` first.

## 1.2.4 — 2026-09-29

### Fixed

- A `break` inside `TDA_DEQUE_FOR_EACH_AS` and `TDA_DEQUE_FOR_EACH_MUT_AS` ends the walk;
  it used to act as a `continue`.
- Typed macros check their type. Every `*_AS`, `SET`, `PUSH`, `INSERT` and kin compares
  `sizeof(T)` with the elem size — an assert, kept in hardened builds — and still reads
  its handle once; a wrong `T` used to read and write past the elem. The node macros
  (`TDA_LIST_NODE_ELEM_AS`, `TDA_HMAP_NODE_KEY_AS`, `TDA_HSET_NODE_KEY_AS`) have no
  container to ask, and say so.
- `tda_span_next_permutation` and `tda_span_prev_permutation` work with a comparator that
  never answers "greater" (`a < b ? -1 : 0`, as carried over from a C++ less-than); the
  search for the elem to swap used to run off the front of the span.
- An arena hands out all of its capacity. `tda_al_arena_new` rounds `cap` up to the
  alignment, and `tda_al_arena_from_buf` cuts the block down to whole slots and refuses a
  buffer without room for one; the stats report the result. A tail short of a slot used to
  count as available and never be handed out.
- `tda_span_copy_if` needs room only for the elems that pass, as documented, not for all
  of the source.
- `TDA_ALLOC` and `TDA_REALLOC` read each argument once. A count with a side effect used
  to be read twice, and the size later handed to `TDA_DEALLOC` did not match the block.
- Hardened builds catch a pool block freed twice in a row, not only when no other block is
  taken. One freed twice with other frees in between still passes, and `pool.h` says so.
- `tda_vec_set`, `tda_deque_set` and `tda_hmap_insert` over an existing key take a value
  that is the very elem they overwrite; that was a `memcpy` onto itself.
- `TDA_SPAN_FOR_EACH_AS` over an empty view no longer adds 0 to a null pointer.
- The log allocator no longer reads a pointer `tda_realloc` has just freed.
- Docs: removing the current node inside `TDA_HMAP_FOR_EACH`, `TDA_HSET_FOR_EACH` or
  `TDA_LIST_FOR_EACH` steps through freed memory, and the loop that does it safely is
  shown; a node goes with a clear, an assign or a drop, not only its own removal; the ops
  that move the elems of vec, stack, queue and pqueue are listed in full; a walk or a
  clear of hmap and hset pays for every bucket; `tda_span_transform` in place wants
  destination elems no wider than the source's; `tda_pqueue_pop` sifts down;
  `tda_fprint_char` and `tda_fprint_cstr` escape less than a reader splitting on
  separators would need.

### Changed

- `tda_deque_copy_assign` no longer copies the target's old elems into a grown block only
  to overwrite them.
- `alloc/alloc.h` no longer includes `<stdint.h>`, which it had stopped using. Code that
  took `uint32_t` and kin from it includes `<stdint.h>` itself.

## 1.2.3 — 2026-09-29

### Fixed

- Builds on macOS and on Windows. `<stdbit.h>` ships with the C library, and neither
  Apple's nor the UCRT has it; it is used where it exists and compiler builtins stand in
  elsewhere.
- Tests and examples find the DLL on Windows: every exe now builds into `build/bin`, on
  every platform.

### Changed

- `tda_fprint_f32` and `tda_fprint_f64` spell `inf`, `-inf` and `nan` out instead of
  leaving them to `%g`, so every C library prints the same. A NaN prints without its sign,
  which `0.0 / 0.0` sets on x86 and not on ARM.
- CI builds and runs the tests on macOS with Apple clang and on Windows with clang from
  MSYS2 as well, with a badge per platform.

## 1.2.2 — 2026-09-24

### Fixed

- `tda_span_merge` checks its destination's length in hardened builds; a short one used to
  be written past its end.
- `tda_hmap_move_assign` on one allocator no longer leaves the emptied source with the
  target's old hasher and equality. `tda_hset_move_assign` inherits the fix.
- `tda_hmap_remove_node` and `tda_hset_remove_node` check in hardened builds that the node
  is the map's own.
- An arena shrinks a block that is not its last one where it stands, instead of moving it
  and failing when full.
- `tda_span_partial_sum` in place no longer calls `memcpy` onto itself.
- Docs: `tda_hmap_eq`, `tda_hmap_eq_by` and `tda_hset_eq` require one key equality on both
  sides, checked in hardened builds; `tda_span_remove` and `tda_span_replace` forbid a key
  inside the span; `remove_node` is O(1) expected; the pool's own realloc; which of
  `max_elem` and `std::minmax_element` breaks a tie differently.
- README: the example no longer leaks the arena on failure, FetchContent and the submodule
  pin a release tag instead of `main`, and the C library requirement is stated.
- The rng example no longer depends on the order function arguments are evaluated in.

## 1.2.1 — 2026-09-23

### Fixed

- gcc builds under `-Werror` again: an expression in `internal/ptr.h` had lost the
  parentheses that `-Wparentheses` asks for, and the casts that keep an address mask
  whole where `uintptr_t` is wider than `size_t`.

## 1.2.0 — 2026-09-23

### Added

- **Allocators over your own memory.** `tda_al_arena_from_buf` and `tda_al_pool_from_buf`
  build an arena or a pool inside a buffer the caller already has — an array on the
  stack, a static one, a block from anywhere — with no parent and no heap at all.
- Hardened builds check `tda_al_pool` deallocs: a block from elsewhere, or one more free
  than there were allocs, aborts instead of corrupting the free list.

### Changed

- `tda_al_pool` has a realloc of its own: a new size that fits the block keeps it where it
  is, with no copy and no second block.

## 1.1.0 — 2026-09-23

### Added

- **Hardened builds.** `-DTDA_HARDENED=ON` keeps the bounds, emptiness, range and
  `elem_size` checks of every public function in a release build: a breach aborts with
  its place instead of running on past the end. Off by default, and a build without it is
  what it was.
- `TDA_EXPECT` in `core/check`: `TDA_CHECK` under `TDA_HARDENED`, `assert` otherwise.

## 1.0.0 — 2026-09-23

The first stable release. Until 2.0 the public API only grows: nothing declared under
`include/tda/` is renamed, removed or changes meaning.

### Contents

- **core** — `span`, `status`, `cmp`, `hash`, `print`, `rng`, `check`, `version`
- **alloc** — `default`, `arena`, `pool`, `log`, `aligned`
- **ds** — `arr`, `vec`, `deque`, `list`, `stack`, `queue`, `pqueue`, `hmap`, `hset`,
  `bitset`
- **algo** — `compare`, `copy`, `fill`, `fold`, `heap`, `merge`, `modify`, `permute`,
  `search`, `set`, `sort`, `transform`
