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
- **Lifetime verbs** are `init` and `deinit`: `tda_vec_init(elem, al, &v)` /
  `tda_vec_deinit(&v)`, the value being the caller's (see *Values* below). A type that
  cannot be built empty spells out what it needs — `tda_arr_init_len`, because an
  array's length is fixed at construction, and `tda_bitset_init_len` — so a bare `init`
  never fails. One that copies elems in says from what: `from_data`, `from_span`. One
  laid over memory the caller lends says so: `tda_arena_init_buf`.
- Type suffix and method slug stay parallel: `tda_Arr` ↔ `arr_`. Rename the type to
  `tda_Array` and the methods become `tda_array_*`. Never let them drift.

**Exception — interfaces.** A type you *dispatch through* rather than *own* is named by
the operation verb; the instance is just the first argument, like `FILE *` in `fprintf`
and not `file_printf`.

- `tda_Al` is an interface → `tda_alloc`, `tda_alloc_zeroed`, `tda_resize`, `tda_remap`,
  `tda_realloc`, `tda_dealloc`.
- Functions about the allocator *object* keep the full slug: `tda_al_default`.

Rule of thumb: **owned object → `tda_<slug>_verb(self, ...)`; interface →
`tda_verb(iface, ...)`.**

**Function-pointer typedefs** are types, so they follow the type rule and nothing else:
`tda_Cmp`, `tda_Eq`, `tda_Hasher`, `tda_FPrint`, `tda_Pred`, `tda_EntryPred`, `tda_Fold`,
`tda_Gen`, `tda_UnOp`, `tda_BinOp`. No `Fn` suffix, no `_cb`: they are called
synchronously as part of the operation's own definition — a comparator is what gives
`sort` its meaning — not registered to fire later, and a suffix that distinguishes nothing
is noise. A typedef lives with what it serves: `tda_Pred` and kin in `algo/fn.h`,
`tda_EntryPred` in `ds/hmap.h`, the one place a test sees a key and a value at once.

**Ready-made values** of such a type are `tda_<slug>_<T>`, the slug being the interface
they implement: `tda_cmp_i32`, `tda_cmp_desc_f64`, `tda_eq_cstr`. There is deliberately no
second, value-taking form to tell them apart from: to compare two values, call the
comparator with their addresses.

Where the bare name is already a result type, the typedef is named for the agent rather
than the operation — `tda_Hash` is the value, so `tda_Hasher` produces one.

**Every callback takes a `void *ctx` last**, and every function that takes a callback takes
the ctx right after it: `tda_span_sort(s, cmp, ctx)`, `tda_span_find_if(s, pred, ctx, …)`.
So an order or a match can depend on more than its operands — keys to sort indices by, a
modulus, a seed — with no global. A ready-made callback ignores it and is passed
`nullptr`. A container that stores its callbacks stores the ctx with them, one for a
hasher and an equality that must agree. `tda_FPrint` is the exception: it prints one
value to one stream and has nothing to be told.

A function pointer that only ever lives inside one interface aggregate stays an inline
member with no typedef, as in `tda_Al`: a typedef would name a type never written twice.

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
- `obj` — the local a constructor builds before it writes `out`, until its invariant
  holds; `self` is only ever the one that arrived ready. Where it has a truer job it takes that name
  instead: `copy`, `clone`.
- `key` what is looked up or indexed by, `val` any other value passed by address, `data`
  raw elems to copy in, `ptr` a block from an allocator. No `x`: a value has a job, not a
  letter.
- `lhs`/`rhs` two elems being compared; `a`/`b` two of anything larger — two spans, two
  hashes.
- `idx` one index, `i`/`j` a pair of them, `at` a node position.
- `len` how many elems there are, `cap` how many fit, `count` how many an argument asks
  for, `nth` an ordinal.
- `layout` a block's size and alignment together, a `tda_Layout`; `old` the one a block
  was made with, when a new size comes beside it.

**Macros** are `TDA_UPPER`, but the prefix is for what leaves the translation unit. A
`static` constant or a macro that lives and dies inside one `.c` shares no namespace with
anyone and goes unprefixed: `VEC_GROWTH_BASE`, `INSERTION_THRESHOLD`, `FNV_PRIME_64`.
Names in `src/internal/*.h` keep it, since several `.c` see them at once, and so do the
type-generic wrappers over functions: `TDA_ARR_INIT_LEN(T, …)`, `TDA_ALLOC(T, …)`.

**A rename inside a major keeps the old name.** The new one is declared beside it, and the
old one stays an exported function that calls it, marked `[[deprecated("use …")]]` and
`@deprecated` with the major that removes it. A symbol never leaves the shared object
within a major; only the next one deletes it.

## Values

**Every container and every allocator is a value the caller holds** — on the stack, in a
struct, in another container — not a handle the library allocates. Making one allocates
nothing for the object itself, an empty container nothing at all, so its `init` cannot
fail and returns `void`.

- **The struct is in the header, so it has a size; its fields are the type's.** Each
  ends in `_` (`len_`, `al_`) and is read and changed only through the functions. Types
  that only give such a struct its size live there too, under `@cond`, with a trailing
  `_` and a `static_assert` in the source against the real one.
- **Every constructor writes through `out`**, the empty `init` included, so there is one
  shape to learn: `tda_vec_init(elem, al, &v)`, `TDA_VEC_OF(int32_t, al, &v, 1, 2)`.
- **`= {}` is the inert state.** `deinit` leaves it, and takes it again as a no-op, so an
  exit path can release everything whether or not it was ever built. Every other op
  asserts on it.
- **A copy of the struct is a second owner.** Nothing in C stops `w = v`; each type says
  so in a `@warning`, and a value changes hands through `move_assign`, or `into_*` for an
  adapter's inner container.
- **A container moves as bytes.** Nothing inside points at the struct — no sentinel node,
  no buffer of its own fields — so it can sit in a vec that grows. Any later design has
  to keep that.
- **An allocator does not move once it is built.** Its `tda_Al` is a field whose `ctx`
  points back at it, and every container on it keeps that pointer. `TDA_AL(&arena)`
  hands the interface out, one macro for the five kinds, since no function takes all
  five types. Each of its own functions `TDA_EXPECT`s that `ctx` is still itself, the
  one sign a moved or copied allocator leaves; a request through the `tda_Al` cannot
  check, since all it gets is the `ctx`.
- **No elem has size 0.** A layout is a type's (`tda_layout_is_type`): a power-of-two
  alignment and a size above zero, a multiple of it.
- **Nothing locks.** A container or an allocator is guarded by its caller, and an
  allocator under containers on several threads needs it even when the containers are
  never shared, since every growth reaches it.
- **The allocator is part of a container's value.** `move_assign` and `swap` carry it
  along with the elems, as Rust's `Vec<T, A>` does, so both are O(1) and cannot fail on
  any two allocators; `copy_assign` keeps the target's, and `copy_with` puts a copy on
  the one asked for.

## API contracts

**Error model: uniform status-return.** A fallible operation *returns* `tda_Status` and
writes its result through a trailing `out`. Strictness over ergonomics — the point is that
an error cannot be silently dropped.

- **Fallible op:** `[[nodiscard]] tda_Status foo(args…, T *out);`. `[[nodiscard]]` turns an
  ignored error into a diagnostic the compiler raises without being asked, and into a hard
  error under `-Werror`; `out` is written **only on `TDA_STATUS_OK`**, left untouched
  otherwise. Propagate by hand — `tda_Status st = foo(…); if (TDA_STATUS_IS_ERR(st))
  return st;` — or `goto fail` while resources are held, since C has no `defer` and a bare
  early return would leak them.
- **Never the inverse** (`T foo(args, tda_Status *st)`): a status out-param is silently
  ignorable, which is the "errors are optional" model this library rejects.
- **Can't-fail ops return their value directly**, with no status: pure accessors such as
  `tda_arr_len` and `tda_arr_elem_layout`. Uniform means uniform among *fallible* ops.
- **Allocator wrappers are the value-return exception, and a principled one.**
  `tda_alloc`, `tda_alloc_zeroed` and `tda_realloc` return the pointer, `nullptr` meaning
  failure: the value and the single error cause share one channel, so `[[nodiscard]]`
  already flags a dropped check. `tda_Status f(…, void **out)` would buy nothing and
  fight the `malloc` idiom.
- **A type known at compile time is allocated through `TDA_ALLOC(T, …)`**, inside the
  library as outside it: a bitset's words, an hmap's buckets. A layout known only at run
  time — elem buffers, nodes — goes through `tda_alloc` with that layout.
- **Everything else keeps the status**, though OOM is nearly its only cause, because the
  status is a type-level marker that an op *can* fail rather than a carrier of causes. The
  `malloc` idiom does not generalize: most fallible ops have nothing to hand back, and
  `bool` is spoken for — it returns *found* from `contains`, `is_empty` and
  `binary_search`, while `hmap` and `hset` `insert` need both meanings at once. A second
  cause is then one more enumerator, not a new signature everywhere.
- **assert vs status.** `assert` is for programmer errors — broken preconditions like
  an elem layout that is no type's, `al != nullptr`, `self != nullptr`,
  `out != nullptr`. Those are bugs, not runtime states. `tda_Status` is for
  data-dependent failures reachable from correct code: OOM, and `len * elem.size`
  overflow folded into `tda_layout_ckd_array` → `nullptr` → `TDA_STATUS_ERR_NO_MEM`. Never turn one of those into UB or an assert.
- **`TDA_EXPECT` vs `assert`.** A precondition of a public function whose breach would
  touch memory out of bounds — an index, emptiness, a range, a matching elem layout — is a
  `TDA_EXPECT`, which `TDA_HARDENED` keeps in release, and so is an allocator's block
  that misses the alignment the contract promises. Invariants, anything above O(1) and a
  breach that only gives a wrong answer, such as an output overlapping an input in algo,
  stay `assert`.
- **Typed macros check their type's layout.** A `*_AS`, `SET`, `PUSH` and kin passes its
  container pointer through `tda_<slug>_typed_`, which `TDA_EXPECT`s `TDA_LAYOUT_OF(T)`
  against the elem layout and hands the pointer back — one evaluation, constness kept by a `_Generic`
  choice between it and its mutable twin `tda_<slug>_typed_mut_`, so no cast drops it. A container stores
  no type, so this catches a wrong width or alignment only: `float` passes for
  `int32_t`. The node macros (`TDA_LIST_NODE_ELEM_AS`, `TDA_HMAP_NODE_KEY_AS`,
  `TDA_HSET_NODE_KEY_AS`) take the container a node is in and check through it, since
  the container knows where in the node the elem lies. A walk macro reads its container
  pointer once too, and names its hidden variables with `TDA_LINE_NAME_`, so walks nest without
  `-Wshadow`.
- **Layouts are passed, never guessed.** Whatever allocates for elems — a container, an
  algo's scratch buffer — gets their whole layout from its caller: a constructor takes a
  `tda_Layout`, a span carries one. Nothing infers an alignment from a size. Two spans,
  or a span and a container, meet only on one layout, alignment included.
- Public symbols carry `TDA_API` (see `core/export.h`); everything else stays hidden under
  the library's default-hidden visibility.
- **An accessor that only reads a field or indexes is `static inline` in the header**:
  `len`, `cap`, `al`, the layouts, `data`, the contiguous containers' `get`, `front`
  and `back`, `to_span`, a list's end nodes, a bit's
  `test` and `set`, and an adapter's forwarding of them. A call across the shared object
  cost more than the read itself; inline, `TDA_VEC_GET_AS` in a loop is five times
  faster. Anything that allocates, moves elems or walks nodes stays behind `TDA_API`, and
  so does the whole allocator layer, the wrappers in `alloc.h` included: an allocation
  costs far more than the call that reaches it. The struct is public anyway, so inlining
  adds nothing to the ABI, and `TDA_HARDENED` is a public definition, so an inlined
  `TDA_EXPECT` checks in the caller as it would in the library.

## Code layout

- **A function body is paragraphs**, a blank line between them: the precondition checks,
  each step of the work, the result. The final `return` is a paragraph of its own, after
  a loop's `}` and after the line that computed its value alike.
- **A body of one check and a `return`** — the `_typed_` helpers — is one paragraph and
  takes no blank line.
