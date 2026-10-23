# Changelog

## 2.0.0 — unreleased

Three changes break the API, each for a fault that 1.x could not fix without breaking
it: an allocator could not tell "not in place" from "out of memory", an elem aligned
past `max_align_t` sat wherever malloc put it, and a comparator could depend on nothing
but its operands. See "Migrating from 1.x" below.

### Changed

- **`tda_Al` takes Zig's hooks and a layout in every call.** The table is `alloc`,
  `alloc_zeroed`, `resize`, `remap` and `dealloc`, each given a `tda_Layout`, the size and
  the alignment the block was made with. `resize` grows or shrinks a block only in place
  and answers `bool`; `remap` may move it and answers null when it has no way cheaper
  than a copy. Before, one null from `realloc` meant both "not in place" and "out of
  memory". Only `alloc` and `dealloc` are required: a missing hook means "never", and
  the wrappers make do. Null from `alloc` now means out of memory and nothing else; an
  alignment an allocator was not built to give asserts.
- The wrappers follow: `tda_alloc(al, layout)`, `tda_alloc_zeroed`, `tda_resize`,
  `tda_remap`, `tda_realloc(al, ptr, old, new_size)` and `tda_dealloc(al, ptr, layout)`.
  `tda_realloc` tries `resize`, then `remap`, then alloc, copy and dealloc. The typed
  macros take the layout from the type: `TDA_ALLOC`, `TDA_ALLOC_ZEROED`, `TDA_REALLOC`,
  `TDA_DEALLOC`.
- **Every container is built on the layout of its elem**, not its size:
  `tda_vec_new(TDA_LAYOUT_OF(T), al, &v)`, and the typed macros pass it themselves.
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
- `alloc/pool` is built on a block layout, `tda_al_pool_new(parent, block, count)` and
  `tda_al_pool_from_buf(buf, size, block)`, so it holds one type at that type's
  alignment; `tda_AlPoolStats` reports `block_align`. A block given back with a layout
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
- `tda_list_block_layout`, the block a list needs from an allocator whose blocks are all
  one layout, such as `alloc/pool`: the larger size and alignment of the list struct and
  one node. Before, the struct was opaque and the node size unwritten, so the block was
  found by trial.
- `tda_pqueue_cmp_ctx`, `tda_hmap_key_ctx` and `tda_hset_key_ctx`.

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
