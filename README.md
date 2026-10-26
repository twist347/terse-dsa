# tda — terse data structures and algorithms in C23

[![linux](https://github.com/twist347/terse-dsa/actions/workflows/linux.yml/badge.svg)](https://github.com/twist347/terse-dsa/actions/workflows/linux.yml)
[![macos](https://github.com/twist347/terse-dsa/actions/workflows/macos.yml/badge.svg)](https://github.com/twist347/terse-dsa/actions/workflows/macos.yml)
[![windows](https://github.com/twist347/terse-dsa/actions/workflows/windows.yml/badge.svg)](https://github.com/twist347/terse-dsa/actions/workflows/windows.yml)

Classic containers and algorithms, written plainly. No dependencies.

The reference lives at [twist347.github.io/terse-dsa](https://twist347.github.io/terse-dsa/).

## Example

```c
#include "tda/tda.h"

#include <stdio.h>

int main() {
    unsigned char mem[1024];

    // the arena and the vec are values on the stack; the vec's elems live in 'mem'
    tda_Arena arena = tda_arena_init_buf(mem, sizeof(mem));

    tda_Vec vec;
    if (TDA_STATUS_IS_ERR(TDA_VEC_OF(int32_t, tda_arena_to_al(&arena), &vec, 5, 3, 1, 4, 2))) {
        return 1; // 'vec' is written only on success, and the arena holds no heap
    }

    // every comparator takes a ctx beside it; the ready-made ones need none
    tda_span_sort(tda_vec_to_span_mut(&vec), tda_cmp_i32, nullptr);

    size_t idx;
    if (tda_span_binary_search(tda_vec_to_span(&vec), &(int32_t){4}, tda_cmp_i32, nullptr, &idx)) {
        printf("4 is at %zu\n", idx); // 4 is at 3
    }

    tda_vec_deinit(&vec);
    tda_arena_deinit(&arena);

    return 0;
}
```

Most modules have a program of their own in [example/](example/): each is built and run
with the tests, and the reference quotes it on that module's page.

## Design

Three rules shape the whole API:

- **Memory is explicit and swappable.** Nothing allocates on its own — every container is
  handed a `tda_Al`, a view of an allocator, and uses only that, so swapping in an arena, a slab or a logging
  allocator is a one-line change at the call site. Every request carries a size and an
  alignment, so an elem aligned past malloc's own lands aligned in any container, on any
  allocator.
- **Containers and allocators are values.** A `tda_Vec` or a `tda_Arena` is a struct
  the caller holds wherever it likes — on the stack, in a struct of its own — made by
  an `init` that returns it and released by `deinit`. Nothing is allocated for the object
  itself, so an empty container costs nothing and cannot fail to be made. A copy of the
  struct would be a second owner, so a value changes hands through `move_assign`.
- **Errors cannot be dropped.** A function returns its result; a fallible one returns
  `tda_Status` and writes its result through a trailing `out`; `[[nodiscard]]` makes ignoring it a warning every
  compiler raises unasked, and a compile error under `-Werror`. Broken preconditions are
  `assert`, not status — those are bugs, not runtime states.

Out of bounds is never silent in a build that asks: `-DTDA_HARDENED=ON` keeps every
bounds, emptiness and range check in release, so an index past the end aborts with its
place instead of reading past it. [example_check.c](example/core/example_check.c) shows
the same mistake in a debug, a release and a hardened build.

The rest of them — how a name is built, where the `mut` marker sits, what is an `assert`
and what a status — are written down in [conventions](docs/conventions.md), each with the
reason it is what it is.

## Allocators

An allocator is a value too — `tda_Arena`, `tda_Pool`, `tda_Slab`, `tda_Debug`, `tda_Log`
— built over a parent allocator or a buffer of your own, and `tda_arena_to_al(&arena)`
hands out a `tda_Al`, a view of it: a pointer to the arena and to a static table of its
hooks, passed by value. The arena can move like any value until a view is out, and must
stay where it is while one is, since the view and every container built on it point at
it. Nothing checks this, as nothing checks a span.

A pool hands out blocks of one layout, so of the containers only a list runs on it, with
blocks of `tda_list_node_layout`; a slab keeps a pool per size class, and any container
runs on it.

A container keeps the `tda_Al` it was built on, and the allocator is part of its value:

- **A copy is born on its source's allocator** — `tda_vec_copy_with` names another one.
- **A move carries the allocator along**, as Rust's `Vec<T, A>` does: the target takes
  the block and the allocator it came from, the source is left empty on that allocator,
  and nothing is copied, so it is O(1) and cannot fail on any two allocators.
  `copy_assign` keeps the target's allocator.
- **`swap` exchanges allocators too**, so each block still goes back to the one that made
  it, whatever the two sides were built on.

## Layout

`tda/tda.h` includes every header below at once; naming the modules a file actually
uses stays the better habit.

**`core`** — the vocabulary the rest is written in.

| | |
|---|---|
| `status.h` | `tda_Status`, what every fallible operation returns |
| `span.h` | `tda_Span` / `tda_SpanMut`, a non-owning view over contiguous elems |
| `layout.h` | `tda_Layout` — a block's size and alignment as one value, and `TDA_LAYOUT_OF(T)` |
| `align.h` | rounding sizes and addresses up to an alignment, for an allocator of one's own |
| `cmp.h` | `tda_Cmp` and `tda_Eq`, plus ready-made ones for the built-in types |
| `hash.h` | `tda_Hasher`, `tda_Hash`, hashers for the built-in types and `tda_hash_combine` |
| `rng.h` | `tda_Rng` — a seeded generator, and uniform ints, floats and bools drawn from one |
| `print.h` | `tda_FPrint`, the printer a container is handed to show itself, ready-made ones for the built-in types, and the span printers |
| `check.h` | `TDA_CHECK`, an assert that survives NDEBUG, and `TDA_EXPECT`, one that does under `TDA_HARDENED` |
| `util.h` | `TDA_SWAP`, `TDA_UNUSED`, `TDA_STRINGIFY` |
| `version.h` | `TDA_VERSION_MAJOR/MINOR/PATCH`, `TDA_VERSION_STRING`, `TDA_VERSION_AT_LEAST` |
| `export.h` | `TDA_API` and the visibility it carries |

**`alloc`** — memory, explicit and swappable.

| | |
|---|---|
| `alloc.h` | the `tda_Al` view and its `tda_AlVtable` (Zig's four hooks and Rust's `alloc_zeroed`), `tda_al_eq`, the `tda_alloc` / `tda_alloc_zeroed` / `tda_resize` / `tda_remap` / `tda_realloc` / `tda_dealloc` wrappers, and their typed forms `TDA_ALLOC(T, al, count)` and kin |
| `default.h` | malloc and friends, and `aligned_alloc` past malloc's own alignment |
| `arena.h` | bump allocation, freed all at once or back to a mark; over a parent or a buffer of your own |
| `pool.h` | blocks of one layout off a free list; over a parent or a buffer of your own |
| `slab.h` | small blocks from pools by size class, large ones from the parent; the one to put under a container |
| `log.h` | wraps another allocator and writes down what it is asked |
| `debug.h` | wraps another allocator and stops on misuse: wrong sizes or alignments, double frees, writes past the end or after a free; reports leaks and fails on demand |

**`algo`** — operations over spans, customized only through function pointers.

| | |
|---|---|
| `fn.h` | `tda_Pred`, `tda_Fold`, `tda_Gen`, `tda_UnOp`, `tda_BinOp` |
| `search.h` | find and its kin from either end, count, starts_with and ends_with, the all_of/any_of/none_of trio, min_elem and max_elem, and the binary family over a sorted span |
| `sort.h` | sort and sort_stable, insertion_sort, partial_sort, nth_elem and the is_sorted checks |
| `heap.h` | make_heap, push_heap, pop_heap, sort_heap and the is_heap checks |
| `permute.h` | reverse, rotate, swap_ranges, shuffle and shuffle_prefix, the partition family and stepping through permutations |
| `modify.h` | remove, remove_if and unique, which return the new length; replace and replace_if, which write in place |
| `copy.h` | copy, copy_if, copy_overlapping, sample |
| `fill.h` | fill, fill_zero, generate |
| `fold.h` | fold, fold_back, partial_sum, adjacent_difference |
| `transform.h` | transform and zip_with |
| `compare.h` | cmp, eq, eq_by, mismatch, is_permutation |
| `merge.h` | merge and inplace_merge |
| `set.h` | union, intersection, difference, symmetric difference and includes, over sorted spans |

**`ds`** — owning containers.

| | |
|---|---|
| `arr.h` | `tda_Arr` — a length fixed at construction |
| `bitset.h` | `tda_BitSet` — a set of indices, one bit each, over a fixed universe |
| `vec.h` | `tda_Vec` — growable, one contiguous block |
| `deque.h` | `tda_Deque` — a ring, both ends O(1) amortized |
| `list.h` | `tda_List` — doubly linked; a position stays valid |
| `hmap.h` | `tda_HMap` — separate chaining; an entry never moves |
| `hset.h` | `tda_HSet` — the same table with nothing on the value side, and the set ops |
| `stack.h` | `tda_Stack` — a vec through a narrower keyhole |
| `queue.h` | `tda_Queue` — a deque through a narrower keyhole |
| `pqueue.h` | `tda_PQueue` — a buffer kept under a heap discipline |

## Build

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build
```

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all"
cmake --build build-asan && ctest --test-dir build-asan
```

```sh
cmake -S . -B build-hardened -DCMAKE_BUILD_TYPE=Release -DTDA_HARDENED=ON
cmake --build build-hardened && ctest --test-dir build-hardened
```

The benchmarks are off by default:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DTDA_BUILD_BENCH=ON
cmake --build build-bench && ./build-bench/bin/bench_algo_sort
```

Requires a C23 toolchain. CI builds and runs the tests on Linux with gcc 15 and clang 22,
on macOS with Apple clang, and on Windows with clang from MSYS2 against the UCRT, the
shared library included. MSVC's `cl` does not build it: it lacks too much of C23
(`constexpr`, `_BitInt`, `<stdckdint.h>`).

The reference is generated from the same headers and published with each release; to
build it locally:

```sh
doxygen docs/Doxyfile   # -> build-docs/html/index.html
```

## Use it in a project

tda is consumed as a source dependency; there is no `install` step and none is planned.
Either way the target to link is the alias `tda::tda`, which carries
the include path and the C23 requirement with it.

With `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(terse-dsa
    GIT_REPOSITORY https://github.com/twist347/terse-dsa.git
    GIT_TAG v3.0.0
)
FetchContent_MakeAvailable(terse-dsa)

target_link_libraries(app PRIVATE tda::tda)
```

As a submodule:

```sh
git submodule add https://github.com/twist347/terse-dsa.git \
    thirdparty/terse-dsa
git -C thirdparty/terse-dsa checkout v3.0.0
```

```cmake
add_subdirectory(thirdparty/terse-dsa)

target_link_libraries(app PRIVATE tda::tda)
```

## Compatibility

A major keeps the names, the signatures and what the docs say they do; only the next one
changes them, and a rename inside a major keeps the old name, deprecated. That is the
API: tda is built from source together with what uses it, so a minor may add fields to
a struct or hooks to the end of `tda_AlVtable`, and you rebuild against the new headers.
A shared build of tda carries `MAJOR.MINOR` in its soname for that reason.

Not promised, even within a major:

- **The exact capacities.** Growth is geometric; how much a vec or a map reserves when
  it grows may change.
- **Hash values and walk order.** What `tda_hash_*` returns, and the order an hmap or an
  hset is walked in. Do not store a hash, and do not rely on an order the docs call
  unspecified.
- **The stream behind a seed.** `tda_rng_u64` gives the same numbers for a seed within a
  major; the draws built on it, `shuffle` and `sample` within a minor.
- **Text meant for people.** The output of the printers, of `alloc/log` and of
  `alloc/debug`'s reports.
- **What a header pulls in.** Include what you use, or `tda/tda.h` for everything.
- **The toolchain floor.** C23 with `nullptr`, `constexpr`, `typeof` and
  `<stdckdint.h>`: the CI runs clang 22 and gcc 15, and a minor may need a newer one.
  It is C only; nothing is wrapped in `extern "C"`.

A status is tested with `TDA_STATUS_IS_ERR`, not against one cause: a minor may add one.

## What it does not do

- **Elems are bytes.** A container copies `elem.size` bytes in and out, and drops them by
  releasing the block — it never calls anything of yours. A `tda_Vec` of `strdup`ed
  `char *` leaks unless the caller frees them first.
- **No elem has size 0.** Every layout a container or a span takes is a type's, with a
  size above zero; Rust's zero-sized types have no C counterpart, and a set of nothing
  is a counter.
- **The hashers are not seeded.** The ready-made ones are fixed functions, fine for keys
  you control. For keys from outside, write a hasher that takes a seed through its `ctx`,
  so no one can aim their keys at a single bucket.
- **Printing goes to a `FILE *`.** The printers are for looking at data while debugging,
  through stdio; there is no sink of your own to print into a string.
- **Nothing is thread-safe.** No container takes a lock; sharing one across threads is the
  caller's problem. So is an allocator under containers on several threads: each growth
  reaches it, even when the containers themselves are never shared.

## License

MIT — see [LICENSE](https://github.com/twist347/terse-dsa/blob/main/LICENSE).

`thirdparty/Unity-2.7.0` is Unity, the test framework, vendored as is. It is third-party
code under its own MIT license; its copyright notice lives in
`thirdparty/Unity-2.7.0/LICENSE.txt` and is not covered by the notice above.

`thirdparty/ubench` is ubench.h, the benchmark harness, vendored the same way. There is no
copyright to carry over: it is released into the public domain under the Unlicense, whose
text sits at the top of `thirdparty/ubench/ubench.h`.
