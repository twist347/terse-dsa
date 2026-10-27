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
  length is fixed at construction, and `tda_bitset_init_len` — so a bare `init` never
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

**Every callback takes a `void *ctx` last**, and every function that takes a callback
takes the ctx right after it: `tda_span_sort(s, cmp, ctx)`,
`tda_span_find_if(s, pred, ctx, …)`. So an order or a match can depend on more than its
operands — keys to sort indices by, a modulus, a seed — with no global. A ready-made
callback ignores it and is passed `nullptr`. A container that stores its callbacks stores
the ctx with them, one for a hasher and an equality that must agree; a map's two printers
take one each. There is no exception: even a printer is told things, a width, a table of
names, the printer of the elems of a container it prints.

**A key goes to the right of the elem.** Every op that takes a key calls
`eq(elem, key, ctx)` or `cmp(elem, key, ctx)`, never the other way, so the key need not
be an elem: a span of records sorted by id is searched with an id. hmap and hset are the
exception the hasher makes: it hashes the stored keys and the probe alike, so a probe has
to be a key.

A function pointer that only ever lives inside one interface aggregate stays an inline
member with no typedef, as in `tda_AlVtable`: a typedef would name a type never written
twice. Its `ctx` comes first, since it is the receiver, the allocator's state, as `self`
is a method's.

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

**Every container and every allocator is a value the caller holds** — on the stack, in a
struct, in another container — not a handle the library allocates. Making one allocates
nothing for the object itself, an empty container nothing at all, so its `init` cannot
fail and returns the value.

- **The struct is in the header, so it has a size; its fields are the type's.** Each
  ends in `_` (`len_`, `al_`) and is read and changed only through the functions. Types
  that only give such a struct its size live there too, under `@cond`, with a trailing
  `_` and a `static_assert` in the source against the real one.
- **A constructor returns like any other function** (see *Return rule* below): one that
  cannot fail returns the value, `tda_Vec v = tda_vec_init(elem, al)`, and one that can
  returns a status and writes through `out`, `TDA_VEC_OF(int32_t, al, &v, 1, 2)`.
- **`= {}` is the inert state.** `deinit` leaves it, and takes it again as a no-op, so an
  exit path can release everything whether or not it was ever built. `move_assign` takes
  it as its target, so a field zeroed with its struct can be handed a container later.
  Every other op asserts on it.
- **A copy of the struct is a second owner.** Nothing in C stops `w = v`; each type says
  so in a `@warning`, and a value changes hands through `move_assign`, or `into_*` for an
  adapter's inner container.
- **A container moves as bytes.** Nothing inside points at the struct — no sentinel node,
  no buffer of its own fields — so it can sit in a vec that grows. Any later design has
  to keep that.
- **An elem is bytes, for good.** A container copies an elem in and out and drops it by
  releasing its block; it calls nothing of the caller's, no destructor and no clone. One
  that owns memory is the caller's to release, and every op that removes an elem lets
  the caller read it first: `pop_to`, `take`, a walk before `clear` or `deinit`, a `find`
  before an `insert` that overwrites. No container grows a hook for it.
- **An allocator is a state and a view of it.** The state is a value like a container,
  and holds no pointer to itself. `tda_<slug>_to_al(&state)` hands out a `tda_Al`, the
  state's address and a static table of the kind's hooks, passed by value; containers
  keep it by value. So a state moves freely until a view is out, and must stay where it
  is while one is: the rule of a span, and like a span's, checked by nothing. Two views
  are of one allocator when `tda_al_eq` says so, the same state and the same hooks; a
  container decides "relink or copy" by it. The library's own tables are `static const`
  inside their `tda_<slug>_to_al` and name all five hooks, `nullptr` for one the kind
  does not have, so a missing hook reads as a decision. A hook runs the kind's
  `ASSERT_<KIND>` on the `self` it recovers from `ctx`, which checks the state is live, a
  field that only deinit and `= {}` leave empty.
- **No elem has size 0.** A layout is a type's (`tda_layout_is_type`): a power-of-two
  alignment and a size above zero, a multiple of it.
- **Nothing locks.** A container or an allocator is guarded by its caller, and an
  allocator under containers on several threads needs it even when the containers are
  never shared, since every growth reaches it. `tda_al_default` alone may be shared: it
  holds no state, and libc guards its own.
- **The allocator is part of a container's value.** `move_assign` and `swap` carry it
  along with the elems, as Rust's `Vec<T, A>` does, so neither copies an elem and neither
  can fail on any two allocators; `copy_assign` keeps the target's, and `copy_with` puts
  a copy on the one asked for.

## API contracts

**Return rule.** A function returns its result. One that can fail returns `tda_Status`
and writes its result through a trailing `out`. Strictness over ergonomics — the point is
that an error cannot be silently dropped. The shapes that look like exceptions follow
from what is returned, not from which function it is:

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
- **`TDA_EXPECT` vs `assert`.** A precondition of a public function whose breach would
  touch memory out of bounds — an index, emptiness, a range, a matching elem layout — is a
  `TDA_EXPECT`, which `TDA_HARDENED` keeps in release, and so is an allocator's block
  that misses the alignment the contract promises, and an output in algo overlapping an
  input it is copied from with `memcpy`, where the overlap is UB. Invariants, anything
  above O(1) and a breach that only gives a wrong answer, such as an overlap that an op
  reads and writes through, stay `assert`. The debug allocator is the one exception:
  what it catches it reports and aborts on in every build, since catching it is its
  whole job.
- **One shape for the checks.** A public function opens with them as one paragraph, in
  this order: `ASSERT_<TYPE>` on each container argument, `TDA_SPAN_ASSERT` on each span,
  `assert` on each pointer and callback that must not be null, the `TDA_EXPECT`s, and last
  the plain asserts that cost more than O(1) or only guard a wrong answer (an overlap an
  op reads and writes through, `owns_node` in the ops that relink, two containers agreeing
  on an equality). An O(1) accessor called on every step of a walk takes no such check: in
  debug it would make the walk quadratic. Within a group the parameters keep their order.
  `ctx` and a pointer documented as nullable are never asserted.
- **A function checks all its own arguments**, even when it forwards to another that
  checks them again: what a function promises to catch should not depend on how it is
  written today. A hardened build pays the second check, which is a branch.
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
- **A typed macro takes one value as `&(T[1]){ (val) }[0]`**, not `&(T){ (val) }`: an
  array of one elem takes an expression of the elem's type whole, so a struct variable or
  a struct literal in parentheses goes in as a scalar does, where `(T){ (val) }` would
  pour it into the first field. A missing or an extra argument stays an arity error. A
  bare brace list does not go in; only the list macros, `*_OF` and `*_EXTEND`, take their
  elems variadic.
- **Typed macros check their type's layout.** A `*_AS`, `SET`, `PUSH` and kin passes its
  container pointer through `tda_<slug>_typed_`, which `TDA_EXPECT`s `TDA_LAYOUT_OF(T)`
  against the elem layout and hands the pointer back — one evaluation, constness kept by
  a `_Generic` choice between it and its mutable twin `tda_<slug>_typed_mut_`, so no cast
  drops it. A container stores no type, so this catches a wrong width or alignment only:
  `float` passes for `int32_t`. An algo macro that takes a key names the key's type `K`
  first, and checks nothing against it, since a key need not be an elem (see *A key goes
  to the right*); the value it writes, `TDA_SPAN_FILL`'s or `TDA_SPAN_REPLACE`'s, is
  checked as an elem. The node macros (`TDA_LIST_NODE_ELEM_AS`, `TDA_HMAP_NODE_KEY_AS`,
  `TDA_HSET_NODE_KEY_AS`) take the container a node is in and check through it, since the
  container knows where in the node the elem lies. A walk macro reads its container
  pointer once too, and names its hidden variables with `TDA_LINE_NAME_`, so walks nest
  without `-Wshadow` as long as each starts on a line of its own: the names come from
  `__LINE__`, and two walks on one line would declare the same ones.
- **Layouts are passed, never guessed.** Whatever allocates for elems — a container, an
  algo's scratch buffer — gets their whole layout from its caller: a constructor takes a
  `tda_Layout`, a span carries one. Nothing infers an alignment from a size. Two spans,
  or a span and a container, meet only on one layout, alignment included.
- Public symbols carry `TDA_API` (see `core/export.h`); everything else stays hidden under
  the library's default-hidden visibility.
- **An accessor that only reads a field or indexes is `static inline` in the header**:
  `len`, `cap`, `al`, the layouts, `data`, the contiguous containers' `get`, `front` and
  `back`, `to_span`, a list's end nodes, a bit's `test` and `set`, and an adapter's
  forwarding of them. So is a write as small: a vec's `pop` and `clear`, a push into room
  already reserved, a bit's `reset` and `flip`. core/span has no source file, so all of it
  is inline, its printing too. A call across the shared object cost more than the read
  itself; inline, `TDA_VEC_GET_AS` in a loop runs several times faster. Anything that
  allocates, moves elems or walks nodes stays behind `TDA_API`, and so does the whole
  allocator layer, the wrappers in `alloc.h` included: an allocation costs far more than
  the call that reaches it. The struct is public anyway, so inlining adds nothing to the
  ABI, and `TDA_HARDENED` is a public definition, so an inlined `TDA_EXPECT` checks in the
  caller as it would in the library.

## Code layout

- **A function body is paragraphs**, a blank line between them: the precondition checks,
  each step of the work, the result. The final `return` is a paragraph of its own, after
  a loop's `}` and after the line that computed its value alike.
- **A body of one check and a `return`** — the `_typed_` helpers — is one paragraph and
  takes no blank line.
- **A parameter the body never reads has no name**, as C23 allows: a hook or a callback
  that ignores one of its arguments says so in the signature. `TDA_UNUSED` is for a name
  only a check reads, which a release build drops, or only one `#ifdef` branch reads.
