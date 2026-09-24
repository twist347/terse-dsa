# Changelog

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
