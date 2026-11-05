# terse — conventions

The rules the library is written to, and why each is what it is. C23, one namespace
prefix: `tda_` / `TDA_`.

## Naming

**Core rule: one primary type per header, and the type's slug == the module name.** That
makes "name by type" and "name by module" the same thing, so nothing is decided per
function.

- **Types** are `tda_Pascal`: `tda_Arr`, `tda_Status`, `tda_Al`. Snake prefix plus
  PascalCase avoids the POSIX-reserved `_t` and still separates types from values.
- **Methods** on an owned object: `tda_<slug>_<verb>(<Type> *self, ...)`, receiver first,
  named `self`. `tda_Arr` (module `ds/arr`) → `tda_arr_init_len`, `tda_arr_deinit`,
  `tda_arr_len`, `tda_arr_get`.
- **Lifetime verbs** are `init` and `deinit`: `tda_Vec v = tda_vec_init(elem, al)` /
  `tda_vec_deinit(&v)`, the value being the caller's (see *Values* below). A type that
  cannot be built empty spells out what it needs — `tda_arr_init_len`, because an array's
  length is fixed at construction, and `tda_bitset_init_nbits` — so a bare `init` never
  fails. One that copies elems in says from what: `from_data`, `from_span`. One laid over
  memory the caller lends says so: `tda_arena_init_buf`. One that takes over a block the
  caller made through the same allocator is `init_raw`, and `into_raw` hands the block
  back out with no copy, leaving the container inert.
- **`to_` gives a view of the object as another type**, cheap and borrowed:
  `tda_vec_to_span(&v)`, `tda_arena_to_al(&arena)`. A bare `_al` on a container is its
  accessor, the allocator it allocates from: `tda_vec_al(&v)`. An allocator's own
  parent would be `parent`, never `al`, so the two never meet.
- Type suffix and method slug stay parallel: `tda_Arr` ↔ `arr_`. Rename the type to
  `tda_Array` and the methods become `tda_array_*`. Never let them drift.

**Exception — interfaces.** A type you *dispatch through* rather than *own* is named by
the operation verb; the instance is just the first argument, like `FILE *` in `fprintf`
and not `file_printf`.

- `tda_Al` is an interface → `tda_alloc`, `tda_alloc_zeroed`, `tda_resize`, `tda_remap`,
  `tda_realloc`, `tda_dealloc`.
- Functions about the allocator *object* keep the full slug: `tda_al_default`,
  `tda_al_eq`.

Rule of thumb: **owned object → `tda_<slug>_verb(self, ...)`; interface →
`tda_verb(iface, ...)`.**

**Function-pointer typedefs** follow the type rule: `tda_Cmp`, `tda_Eq`, `tda_Hasher`,
`tda_FPrint`, `tda_Pred`, `tda_Fold` and kin. No `Fn` or `_cb` suffix: these functions are
called synchronously, not registered for later. A typedef lives with its consumer:
`tda_Pred` in `algo/fn.h`, `tda_EntryPred` in `ds/hmap.h`.

**Ready-made callbacks** are `tda_<slug>_<T>`: `tda_cmp_i32`, `tda_cmp_desc_f64`,
`tda_eq_cstr`. To compare two values, pass their addresses to the comparator. When the
operation name already denotes the result, the callback is named for its agent:
`tda_Hash` is the value, `tda_Hasher` produces it.

**Every callback takes a trailing `void *ctx`**, and its caller passes the ctx immediately
after the callback: `tda_span_sort(s, cmp, ctx)`. This supports external ordering keys,
moduli and seeds without globals. Ready-made callbacks ignore it and receive `nullptr`.
A container storing callbacks stores their ctx too; a map's key and value printers each
have one.

**A key goes to the right of the elem**: `eq(elem, key, ctx)` or `cmp(elem, key, ctx)`.
The key may therefore have another type, as when records are searched by id. In hmap and
hset the probe must still be a key because the same hasher handles probes and stored keys.

A function pointer used only inside one interface, such as `tda_AlVtable`, stays an inline
member without a typedef. Its leading `ctx` is the receiver, like a method's `self`.

**The `mut` marker.** No public name carries `const` — a name either says `mut` or it does
not, and the unmarked form is the read-only one. Where the marker sits says what it
describes:

- **suffix `_mut`** — the *result* is mutable. `tda_arr_get_mut` returns `void *` where
  `tda_arr_get` returns `const void *`; `tda_vec_to_span_mut` returns `tda_SpanMut`.
- **after the type slug** — the *argument* is the mutable type, the result is not:
  `tda_span_mut_to_span(tda_SpanMut) -> tda_Span`, `tda_span_mut_fprint(tda_SpanMut, …)`.
  Only `tda_Span`/`tda_SpanMut` need it, being the one pair that are two distinct types;
  elsewhere mutability rides on the constness of the receiver pointer.
- **no marker** — there is one version, so nothing has to be told apart. Every `algo`
  function over a `tda_SpanMut` is like this: a constant sort does not exist.

Rule of thumb: **the marker separates two versions of one operation. Differ by result →
suffix; differ only by argument → after the slug; one version → nothing.**

**Parameter names go by role.**

- `self` — the receiver of a method on an owned object. A `tda_Span` is `self` in its own
  ops and `s` in `algo`, which are free functions over a span, not methods on it.
- `obj` — the local a constructor builds before it returns it or writes `out`, until its
  invariant holds; `self` is only ever the one that arrived ready. Where it has a truer
  job it takes that name instead: `copy`, `clone`.
- `key` what is looked up or indexed by, `val` any other value passed by address, `data`
  raw elems to copy in, `ptr` a block from an allocator. No `x`: a value has a job, not a
  letter.
- `lhs`/`rhs` two elems being compared; `a`/`b` two of anything larger — two spans, two
  hashes.
- `idx` one index, `i`/`j` a pair of them, `at` a node position.
- `len` how many elems there are, `cap` how many fit, `count` how many an argument asks
  for, `nth` an ordinal, `nbits` a bitset's universe, where `len` would read as its
  members or its words, and `from` the index a bitset search starts at.
- `other` the second object of the receiver's own type, the one copied, moved, swapped,
  compared or combined with; `src`/`dst` what elems are read from and written to, when
  they pass between a span and a container or two spans; `elems` a span of elems a
  container takes over as its block.
- `layout` a block's size and alignment together, a `tda_Layout`; `old` the one a block
  was made with, when a new size comes beside it.

**Macros** are `TDA_UPPER`, but the prefix is for what leaves the translation unit. A
`static` constant or a macro that lives and dies inside one `.c` shares no namespace with
anyone and goes unprefixed: `VEC_GROWTH_BASE`, `INSERTION_THRESHOLD`, `FNV_PRIME_64`.
Names in `src/internal/*.h` keep it, since several `.c` see them at once, and so do the
type-generic wrappers over functions: `TDA_ARR_INIT_LEN(T, …)`, `TDA_ALLOC(T, …)`.

**A rename inside a major keeps the old name.** The new one is declared beside it, and the
old one stays a function that calls it, marked `[[deprecated("use …")]]` and `@deprecated`
with the major that removes it. A name never leaves the headers within a major; only the
next one deletes it.

**A minor never changes what an existing name does.** New behavior comes under a new
name, and the old one keeps doing what its docs say.

**The promise is the API, not the ABI.** tda is built from source with what uses it, so a
minor may add a field to a struct, a hook to the end of `tda_AlVtable` or a field to a
stats struct, and a program is rebuilt against the headers it links. The shared library's
`SOVERSION` is `MAJOR.MINOR`, so one built against another minor refuses to load instead
of reading a struct of the wrong size. Windows puts no `SOVERSION` in a DLL's name, so
there nothing refuses. A hook is only ever added at the end and optional, and a table is
written with designated initializers, so a table written for an older minor still
compiles.

## Values

**Every container and allocator is a caller-held value**, not a library-allocated handle.
The object itself needs no allocation, so an empty container's `init` returns it directly
and cannot fail.

- **The public struct defines the value's size.** Field names end in `_` and are accessed
  through functions. Size-only stand-ins live under `@cond` and have a trailing `_`;
  the source checks them against the real type with `static_assert`.
- **Constructors follow the return rule.** Infallible ones return a value; fallible ones
  return a status and write `out`, as in `TDA_VEC_OF(int32_t, al, &v, 1, 2)`.
- **`= {}` is inert.** `deinit` leaves this state and accepts it again. `move_assign`
  accepts an inert target; every other operation asserts on an inert object.
- **Copying the struct creates a second owner.** Use `move_assign`, or `into_*` for an
  adapter's inner container.
- **A container moves as bytes.** Nothing points back at its struct, so it may itself be
  stored in a growing vec.
- **Elements are bytes.** Containers never call destructors or clone hooks. The caller
  releases resources owned by elements; `pop_to`, `take` and node walks allow extraction
  before removal or cleanup.
- **An allocator is a state plus borrowed views.** `tda_<slug>_to_al(&state)` returns its
  address and a static hook table by value. Once viewed, the state must stay at its
  address. `tda_al_eq` compares both state and hooks. Library tables name all five hooks,
  using `nullptr` for unsupported ones; hooks assert that their recovered state is live.
- **No element has size 0.** A type layout has power-of-two alignment and positive size
  divisible by it.
- **Nothing locks.** Callers guard containers and stateful allocators, even when separate
  containers share one allocator across threads. Only stateless `tda_al_default` is
  directly shareable.
- **The allocator belongs to the container's value.** `move_assign` and `swap` carry it
  with the elements without copying and cannot fail because the allocators differ;
  `copy_assign` keeps the target's allocator, while `copy_with` selects another.

## API contracts

**Return rule.** A fallible function returns `tda_Status` and writes its result through
a trailing `out`; `[[nodiscard]]` makes an ignored status visible. Other return shapes
depend on the result, not on the kind of function:

- **One named value is one result**: `tda_MinMax`, `tda_Range`, a stats struct.
- **Several results go through `out_*`**, and the function returns `void`:
  `tda_span_split_at(…, out_head, out_tail)`, `tda_deque_to_spans`.
- **`bool` with an `out_*` means "found"**: the find family, `tda_bitset_find_next`,
  `tda_hmap_take`. Without one it is a predicate's answer, `contains`, `is_sorted`, `eq`,
  or whether an op did anything, `tda_hmap_remove`, `tda_span_next_permutation`.
- **A pointer that may be null means "found" too**, where the result is a place:
  `tda_hmap_get`, `tda_hmap_find`, `tda_list_find`.
- **An elem of run-time size goes through `out`**, since C cannot return it: `pop_to`.
- **An `out` documented as nullable may be null**: `out_is_new`.
- **`*_ckd_*` take the shape of C23's `ckd_add`**: `out` first, `true` on overflow, `out`
  unspecified then.
- **A variadic typed macro puts `out` before the elems**, which have to come last:
  `TDA_VEC_OF(T, al, out, …)`.
- **A printer returns an `int`, as stdio does**: negative when a write failed. A printer
  is a thin layer over `fprintf`, and its result passes straight through, for the reason
  the allocator wrappers keep `malloc`'s pointer.

- **Fallible op:** `[[nodiscard]] tda_Status foo(args…, T *out);`. `[[nodiscard]]` turns
  an ignored error into a diagnostic the compiler raises without being asked, and into a
  hard error under `-Werror`; `out` is written **only on `TDA_STATUS_OK`**, left
  untouched otherwise. Propagate by hand —
  `tda_Status st = foo(…); if (TDA_STATUS_IS_ERR(st)) return st;` — or `goto fail` while
  resources are held, since C has no `defer` and a bare early return would leak them.
- **Never the inverse** (`T foo(args, tda_Status *st)`): a status out-param is silently
  ignorable, which is the "errors are optional" model this library rejects.
- **Can't-fail ops return their value directly**, with no status: accessors such as
  `tda_arr_len`, constructors such as `tda_vec_init`, views such as `tda_arena_to_al`.
- **Allocator wrappers are the value-return exception, and a principled one.** Those
  that hand out a block, `tda_alloc`, `tda_alloc_zeroed`, `tda_remap` and `tda_realloc`,
  return the pointer, `nullptr` meaning
  failure: the value and the single error cause share one channel, so `[[nodiscard]]`
  already flags a dropped check. `tda_Status f(…, void **out)` would buy nothing and
  fight the `malloc` idiom.
- **A type known at compile time is allocated through `TDA_ALLOC(T, …)`**, inside the
  library as outside it: a bitset's words, an hmap's buckets. A layout known only at run
  time — elem buffers, nodes — goes through `tda_alloc` with that layout.
- **Everything else keeps the status**, though OOM is nearly its only cause, because the
  status is a type-level marker that an op *can* fail rather than a carrier of causes.
  The `malloc` idiom does not generalize: most fallible ops have nothing to hand back,
  and `bool` is spoken for — it returns *found* from `contains` and `binary_search` and a
  predicate's answer from `is_sorted`, while `hmap` and `hset` `insert` need both
  meanings at once. A second cause is then one more enumerator, not a new signature
  everywhere.
- **Test a status with `TDA_STATUS_IS_ERR`**, never against one cause. A minor may add a
  cause, and `TDA_STATUS_ERR_NO_MEM` already stands for an overflowing size as well as an
  exhausted allocator, for good: a size that overflows is a size no allocator gives.
- **assert vs status.** `assert` is for programmer errors — broken preconditions like an
  elem layout that is no type's, an allocator view with no hooks, `self != nullptr`,
  `out != nullptr`. Those are bugs, not runtime states. `tda_Status` is for
  data-dependent failures reachable from correct code: OOM, and `len * elem.size`
  overflow folded into `tda_layout_ckd_array` → `nullptr` → `TDA_STATUS_ERR_NO_MEM`.
  Never turn one of those into UB or an assert.
- **`TDA_EXPECT` vs `assert`.** Use `TDA_EXPECT` for public preconditions whose breach
  causes out-of-bounds access or UB: indices, ranges, layouts, allocator alignment and
  forbidden `memcpy` overlap. `TDA_HARDENED` retains them in release. Invariants,
  expensive checks and wrong-answer-only conditions stay plain `assert`. The debug
  allocator reports its checks and aborts in every build.
- **One shape for the checks.** A public function starts with `ASSERT_<TYPE>` for
  containers, `TDA_SPAN_ASSERT` for spans, assertions for required pointers and callbacks,
  then `TDA_EXPECT`, then expensive or wrong-answer-only assertions. Preserve parameter
  order within each group. Nullable pointers and `ctx` are never asserted. Per-step
  accessors omit checks that would make a debug walk quadratic.
- **A function checks its own arguments**, even when it forwards to another checked
  function; its contract must not depend on today's implementation.
- **Two containers of one type meet through `EXPECT_SAME_<TYPE>(a, b)`**, a macro next
  to `ASSERT_<TYPE>` holding the `TDA_EXPECT`s on their layouts. Two hmaps or hsets that
  must also agree on an equality say so in a separate `assert`, where the
  answer depends on it. A container and a span meet through
  `TDA_EXPECT(tda_layout_eq(<container>, <span>.elem))`, the container first.
- **A mutator that changes the structure checks it on the way out**: one that changes a
  length, a capacity, a head, a block, links or tail bits ends with `ASSERT_<TYPE>` on
  every container it changed, on every path that changed it. One that only rewrites
  elem bytes does not.
- **An inline accessor in a header checks lightly.** It cannot see the private
  `ASSERT_<TYPE>`, so it asserts `self` and the field that marks the inert state, then
  its own `TDA_EXPECT`s. An adapter's accessor asserts the inner container's.
- **A typed macro takes one value as `&(T[1]){ (val) }[0]`**, so struct variables and
  parenthesized literals work like scalars; `(T){ (val) }` would initialize the first
  field instead. Bare brace lists are unsupported; only `*_OF` and `*_EXTEND` are variadic.
  Wrong argument counts remain compile errors.
- **Typed macros check the type's layout.** `tda_<slug>_typed_` compares `TDA_LAYOUT_OF(T)`
  with the element layout, evaluates the container once and preserves constness through
  `_Generic`. This detects only size and alignment: `float` can pass for `int32_t`.
  Algo key types `K` need not match elements, but values written as elements are checked.
  Node macros check through the container that owns the node.
- **Walk macros evaluate the container once** and generate hidden names with
  `TDA_LINE_NAME_`. Nested walks need separate lines because the names use `__LINE__`.
- **Layouts are passed, never guessed.** Constructors and spans carry the complete element
  layout; alignment is never inferred from size. Combined storage requires matching
  layouts, except when the second span supplies comparison-only keys.
- Public symbols carry `TDA_API`; everything else stays hidden by default visibility.
- **Small accessors and mutations are `static inline` in headers**: field access, indexing,
  views, reserved-capacity pushes and bit operations. This avoids a shared-library call
  per element. Allocation, element movement and node walks stay behind `TDA_API`, as does
  the allocator layer. Public structs make this ABI-neutral, and public `TDA_HARDENED`
  keeps inline precondition checks consistent between caller and library.

## Code layout

- **A function body is paragraphs**, a blank line between them: the precondition checks,
  each step of the work, the result. The final `return` is a paragraph of its own, after
  a loop's `}` and after the line that computed its value alike.
- **A body of one check and a `return`** — the `_typed_` helpers — is one paragraph and
  takes no blank line.
- **A parameter the body never reads has no name**, as C23 allows: a hook or a callback
  that ignores one of its arguments says so in the signature. `TDA_UNUSED` is for a name
  only a check reads, which a release build drops, or only one `#ifdef` branch reads.
