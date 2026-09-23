# Changelog

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
