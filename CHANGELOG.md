# Changelog

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
