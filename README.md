# tda — terse data structures and algorithms in C23

[![CI](https://github.com/twist347/terse-dsa/actions/workflows/ci.yml/badge.svg)](https://github.com/twist347/terse-dsa/actions/workflows/ci.yml)

Classic containers and algorithms, written plainly. No dependencies.

Two rules shape the whole API:

- **Memory is explicit and swappable.** Nothing allocates on its own — every container is
  handed a `tda_Al *` and uses only that. Swapping in an arena, a pool or a logging
  allocator is a one-line change at the call site.
- **Errors cannot be dropped.** A fallible operation returns `tda_Status` and writes its
  result through a trailing `out`; `[[nodiscard]]` makes ignoring it a warning every
  compiler raises unasked, and a compile error under `-Werror`. Broken preconditions are
  `assert`, not status — those are bugs, not runtime states.

The rest of them — how a name is built, where the `mut` marker sits, what is an `assert`
and what a status — are written down in [conventions](docs/conventions.md), each with the
reason it is what it is.

## What it does not do

- **Elems are bytes.** A container copies `elem_size` bytes in and out, and drops them by
  releasing the block — it never calls anything of yours. A `tda_Vec` of `strdup`ed
  `char *` leaks unless the caller frees them first.
- **Nothing is thread-safe.** No container takes a lock; sharing one across threads is the
  caller's problem.

## Allocators, in three rules

A container is built on one `tda_Al *` and never touches another:

- **A copy is born on its source's allocator** — `tda_vec_copy_with` names another one.
- **An assignment keeps the target's.** On one allocator a move hands the block over and
  cannot fail; across two it costs `n` and may return `TDA_STATUS_ERR_NO_MEM`, leaving
  both sides as they were.
- **`swap` wants both sides on one allocator** — it is O(1) and returns nothing, so a
  mismatch is an `assert`, exactly as C++ leaves it undefined when
  `propagate_on_container_swap` is false. Across two, copy with `copy_with` and hand the
  results over with `move_assign`.

## Example

```c
tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
if (!arena) {
    return 1;
}

tda_Vec *vec = nullptr;
if (TDA_STATUS_IS_ERR(TDA_VEC_OF(int32_t, arena, &vec, 5, 3, 1, 4, 2))) {
    return 1;
}

tda_span_sort(tda_vec_to_span_mut(vec), tda_cmp_i32);

size_t idx;
if (tda_span_binary_search(tda_vec_to_span(vec), &(int32_t){4}, tda_cmp_i32, &idx)) {
    printf("4 is at %zu\n", idx); // 4 is at 3
}

tda_vec_drop(vec);
tda_al_arena_drop(arena);
```

## Layout

`tda/tda.h` includes every header below at once; naming the modules a file actually
uses stays the better habit.

**`core`** — the vocabulary the rest is written in.

| | |
|---|---|
| `status.h` | `tda_Status`, what every fallible operation returns |
| `span.h` | `tda_Span` / `tda_SpanMut`, a non-owning view over contiguous elems |
| `cmp.h` | `tda_Cmp` and `tda_Eq`, plus ready-made ones for the built-in types |
| `hash.h` | `tda_Hasher`, `tda_Hash`, hashers for the built-in types and `tda_hash_combine` |
| `rng.h` | `tda_Rng` — a seeded generator, and uniform ints, floats and bools drawn from one |
| `print.h` | `tda_FPrint`, the printer a container is handed to show itself, plus ready-made ones for the built-in types |
| `check.h` | `TDA_CHECK`, an assert that survives NDEBUG |
| `util.h` | `TDA_SWAP`, `TDA_UNUSED`, `TDA_STRINGIFY` |
| `version.h` | `TDA_VERSION_MAJOR/MINOR/PATCH`, `TDA_VERSION_STRING`, `TDA_VERSION_AT_LEAST` |
| `export.h` | `TDA_API` and the visibility it carries |

**`alloc`** — memory, explicit and swappable.

| | |
|---|---|
| `alloc.h` | the `tda_Al` interface and the `tda_alloc` / `tda_calloc` / `tda_realloc` / `tda_dealloc` wrappers |
| `default.h` | malloc and friends |
| `arena.h` | bump allocation, freed all at once |
| `pool.h` | fixed-size blocks off a free list |
| `aligned.h` | wraps another allocator and over-aligns every block it hands out |
| `log.h` | wraps another allocator and writes down what it is asked |

**`algo`** — operations over spans, customized only through function pointers.

| | |
|---|---|
| `fn.h` | `tda_Pred`, `tda_Fold`, `tda_Gen`, `tda_UnOp`, `tda_BinOp` |
| `search.h` | find and its kin, count, the all_of/any_of/none_of trio, min_elem and max_elem, and the binary family over a sorted span |
| `sort.h` | sort and sort_stable, insertion_sort, partial_sort, nth_elem and the is_sorted checks |
| `heap.h` | make_heap, push_heap, pop_heap, sort_heap and the is_heap checks |
| `permute.h` | reverse, rotate, swap_ranges, shuffle and shuffle_prefix, the partition family and stepping through permutations |
| `modify.h` | remove, remove_if and unique, which return the new length; replace and replace_if, which write in place |
| `copy.h` | copy, copy_if, copy_overlapping |
| `fill.h` | fill, fill_zero, generate |
| `fold.h` | fold, fold_back, partial_sum, adjacent_difference |
| `transform.h` | transform and zip_with |
| `compare.h` | cmp, eq, eq_by, mismatch |
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
| `hset.h` | `tda_HSet` — the same table with nothing on the value side |
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

Requires a C23 toolchain.

The reference, generated from the same headers:

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
    GIT_TAG main
)
FetchContent_MakeAvailable(terse-dsa)

target_link_libraries(app PRIVATE tda::tda)
```

As a submodule:

```sh
git submodule add https://github.com/twist347/terse-dsa.git \
    thirdparty/terse-dsa
```

```cmake
add_subdirectory(thirdparty/terse-dsa)

target_link_libraries(app PRIVATE tda::tda)
```

## License

MIT — see [LICENSE](https://github.com/twist347/terse-dsa/blob/main/LICENSE).

`thirdparty/Unity-2.7.0` is Unity, the test framework, vendored as is. It is third-party
code under its own MIT license; its copyright notice lives in
`thirdparty/Unity-2.7.0/LICENSE.txt` and is not covered by the notice above.

`thirdparty/ubench` is ubench.h, the benchmark harness, vendored the same way. There is no
copyright to carry over: it is released into the public domain under the Unlicense, whose
text sits at the top of `thirdparty/ubench/ubench.h`.
