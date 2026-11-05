# tda — terse data structures and algorithms in C23

[![linux](https://github.com/twist347/terse-dsa/actions/workflows/linux.yml/badge.svg)](https://github.com/twist347/terse-dsa/actions/workflows/linux.yml)
[![macos](https://github.com/twist347/terse-dsa/actions/workflows/macos.yml/badge.svg)](https://github.com/twist347/terse-dsa/actions/workflows/macos.yml)
[![windows](https://github.com/twist347/terse-dsa/actions/workflows/windows.yml/badge.svg)](https://github.com/twist347/terse-dsa/actions/workflows/windows.yml)

Containers and span-based algorithms for C23, with explicit memory control and no external
runtime dependencies.

[API reference](https://twist347.github.io/terse-dsa/) · [Examples](https://github.com/twist347/terse-dsa/tree/main/example) · [Conventions](docs/conventions.md)

## Highlights

- **Composable allocation.** Containers accept a `tda_Al` allocator view. Use the default
  allocator, an arena, pool or slab, or wrap one with logging and debugging.
- **Alignment is preserved.** Every allocation carries a `tda_Layout` with size and
  alignment, including alignments beyond malloc's guarantee.
- **Generic without code generation.** Type-erased containers store arbitrary element
  layouts; typed macros provide checked, convenient call sites.
- **One algorithm layer.** Algorithms operate on `tda_Span` and `tda_SpanMut`, so the same
  search, sort, transform and set operations work with arrays and contiguous containers.
- **Explicit ownership.** Containers and allocators are caller-held values. Copy, move,
  swap and conversion operations make ownership and allocation costs visible.
- **Failures are visible.** Fallible operations return `[[nodiscard]]` statuses. Debug,
  release and hardened builds offer distinct precondition-checking policies.
- **Diagnostics included.** The debug allocator detects leaks, invalid deallocation and
  out-of-bounds writes, and supports deterministic allocation failure injection.

## Example

A vector allocated entirely in a caller-provided buffer, then searched through the same
span API used for plain arrays:

```c
#include "tda/tda.h"

#include <stdio.h>

int main() {
    unsigned char mem[1024];
    tda_Arena arena = tda_arena_init_buf(mem, sizeof(mem));

    tda_Vec vec;
    if (TDA_STATUS_IS_ERR(TDA_VEC_OF(int32_t, tda_arena_to_al(&arena), &vec, 5, 3, 1, 4, 2))) {
        return 1;
    }

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

Most modules have a complete program in [example/](https://github.com/twist347/terse-dsa/tree/main/example).
Every example is built and run with the tests and quoted in the API reference.

## Build and test

Requires CMake 3.22 and a C23 toolchain.

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

With AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all"
cmake --build build-asan && ctest --test-dir build-asan --output-on-failure
```

With precondition checks retained in release builds:

```sh
cmake -S . -B build-hardened -DCMAKE_BUILD_TYPE=Release -DTDA_HARDENED=ON
cmake --build build-hardened && ctest --test-dir build-hardened --output-on-failure
```

Benchmarks are optional:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DTDA_BUILD_BENCH=ON
cmake --build build-bench && ./build-bench/bin/bench_algo_sort
```

CI tests Linux with GCC 15 and Clang 22, macOS with Apple Clang, and Windows with Clang
from MSYS2 against UCRT, including shared-library builds. MSVC is unsupported because it
lacks required C23 features, including `constexpr`, `_BitInt` and `<stdckdint.h>`.

To build the API reference locally:

```sh
doxygen docs/Doxyfile   # -> build-docs/html/index.html
```

## Use it in a project

tda is consumed as a source dependency; there is no install step. Both methods below
provide the `tda::tda` target, which carries the include path and C23 requirement.

With `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(terse-dsa
    GIT_REPOSITORY https://github.com/twist347/terse-dsa.git
    GIT_TAG v3.3.0
)
FetchContent_MakeAvailable(terse-dsa)

target_link_libraries(app PRIVATE tda::tda)
```

As a submodule:

```sh
git submodule add https://github.com/twist347/terse-dsa.git \
    thirdparty/terse-dsa
git -C thirdparty/terse-dsa checkout v3.3.0
```

```cmake
add_subdirectory(thirdparty/terse-dsa)

target_link_libraries(app PRIVATE tda::tda)
```

Set options before adding tda:

| Option | Default | What it does |
|---|---|---|
| `TDA_BUILD_SHARED` | on at top level, off as a dependency | a shared library rather than a static one |
| `TDA_BUILD_TEST` | on at top level, off as a dependency | the unit tests, run by `ctest` |
| `TDA_BUILD_EXAMPLE` | on at top level, off as a dependency | the examples the reference quotes |
| `TDA_BUILD_BENCH` | off | the benchmarks |
| `TDA_HARDENED` | off | keeps the bounds, emptiness and range checks in release |

## Diagnostics

`TDA_HARDENED=ON` retains bounds, emptiness and range checks in release builds: an invalid
index aborts instead of reading out of bounds. [example_check.c](https://github.com/twist347/terse-dsa/blob/main/example/core/example_check.c)
compares debug, release and hardened behavior.

The debug allocator wraps another allocator and checks block layouts, double frees and
writes outside live blocks. It can also report leaks, stop at a chosen allocation and
refuse allocations deterministically to exercise failure paths.

The test suite covers boundary conditions, overlapping memory, alignment, ownership,
allocation failures and algorithm complexity. Examples double as executable documentation.

## Modules

`tda/tda.h` includes every header below; including only the modules used is preferable.

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
| `alloc.h` | the `tda_Al` view, `tda_AlVtable`, allocation wrappers and typed forms |
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
| `copy.h` | copy, copy_if, sample |
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

## Ownership

An allocator view contains the state's address and a static hook table. Once a view exists,
the state must stay at its address; containers retain the view, and this borrowing rule is
unchecked.

The allocator is part of a container's value:

- **Copying uses the source's allocator**; `copy_with` selects another.
- **Moving transfers the block and allocator** without copying elements. The source stays
  empty and reusable. Only releasing nodes already in a list, hmap or hset target is not
  O(1). `copy_assign` keeps the target's allocator.
- **Swapping exchanges allocators too**, so every block returns to its original allocator.

[conventions](docs/conventions.md) defines naming, ownership and API contracts in detail.

## Compatibility

Within a major, names, signatures and documented behavior stay stable; renamed APIs keep
a deprecated alias. The promise covers source compatibility, not ABI: a minor may add
struct fields or optional trailing `tda_AlVtable` hooks. Rebuild consumers against the
matching headers. A shared library carries `MAJOR.MINOR` in its soname.

Not promised, even within a major:

- **Exact capacities.** Growth is geometric, but reserve amounts may change.
- **Hash values and walk order.** Do not persist hashes or rely on unspecified order.
- **Derived random streams.** `tda_rng_u64` is stable within a major; `shuffle` and
  `sample` only within a minor.
- **Human-readable output.** Printers and allocator diagnostics may change.
- **Transitive includes.** Include what you use, or `tda/tda.h` for everything.
- **The toolchain floor.** A minor may require a newer C23 toolchain. C++ is unsupported.

Test statuses with `TDA_STATUS_IS_ERR`, not a specific enumerator: a minor may add causes.

## What it does not do

- **Elements are bytes.** Containers copy and release storage but run no destructors.
  A `tda_Vec` of `strdup`ed `char *` leaks unless the caller frees the strings first.
- **No zero-sized elements.** Every element has a valid type layout with nonzero size.
- **Ready-made hashers are unseeded.** For untrusted keys, supply a seeded hasher and
  its state through `ctx`.
- **Printing requires `FILE *`.** There is no custom sink or string output.
- **Nothing is thread-safe.** Callers synchronize containers and stateful allocators;
  only the stateless `tda_al_default` may be shared directly.

## License

MIT — see [LICENSE](https://github.com/twist347/terse-dsa/blob/main/LICENSE).

`thirdparty/Unity-2.7.0` is Unity, the test framework, vendored as is. It is third-party
code under its own MIT license; its copyright notice lives in
`thirdparty/Unity-2.7.0/LICENSE.txt` and is not covered by the notice above.

`thirdparty/ubench` is ubench.h, the benchmark harness, vendored the same way. There is no
copyright to carry over: it is released into the public domain under the Unlicense, whose
text sits at the top of `thirdparty/ubench/ubench.h`.
