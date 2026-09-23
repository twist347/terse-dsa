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
  named `self`. `tda_Arr` (module `ds/arr`) → `tda_arr_new_len`, `tda_arr_drop`,
  `tda_arr_len`, `tda_arr_get`.
- **Lifetime verbs** are `new` and `drop`: `tda_vec_new` / `tda_vec_drop`. A type that
  cannot be built empty spells out what it needs — `tda_arr_new_len`, because an array's
  length is fixed at construction.
- Type suffix and method slug stay parallel: `tda_Arr` ↔ `arr_`. Rename the type to
  `tda_Array` and the methods become `tda_array_*`. Never let them drift.

**Exception — interfaces.** A type you *dispatch through* rather than *own* is named by
the operation verb; the instance is just the first argument, like `FILE *` in `fprintf`
and not `file_printf`.

- `tda_Al` is an interface → `tda_alloc`, `tda_calloc`, `tda_realloc`, `tda_dealloc`.
- Functions about the allocator *object* keep the full slug: `tda_al_default`.

Rule of thumb: **owned object → `tda_<slug>_verb(self, ...)`; interface →
`tda_verb(iface, ...)`.**

**Function-pointer typedefs** are types, so they follow the type rule and nothing else:
`tda_Cmp`, `tda_Eq`, `tda_Pred`, `tda_Fold`, `tda_Gen`, `tda_UnOp`, `tda_BinOp`. No `Fn`
suffix, no `_cb`: they are called synchronously as part of the operation's own definition
— a comparator is what gives `sort` its meaning — not registered to fire later, and a
suffix that distinguishes nothing is noise.

**Ready-made values** of such a type are `tda_<slug>_<T>`, the slug being the interface
they implement: `tda_cmp_i32`, `tda_cmp_desc_f64`, `tda_eq_cstr`. There is deliberately no
second, value-taking form to tell them apart from: to compare two values, call the
comparator with their addresses.

Where the bare name is already a result type, the typedef is named for the agent rather
than the operation — `tda_Hash` is the value, so `tda_Hasher` produces one.

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
- `obj` — the object under construction, from allocation until its invariant holds; `self`
  is only ever the one that arrived ready. Where it has a truer job it takes that name
  instead: `copy`, `clone`.
- `key` what is looked up or indexed by, `val` any other value passed by address, `data`
  raw elems to copy in, `ptr` a block from an allocator. No `x`: a value has a job, not a
  letter.
- `lhs`/`rhs` two elems being compared; `a`/`b` two of anything larger — two spans, two
  hashes.
- `idx` one index, `i`/`j` a pair of them, `at` a node position.
- `len` how many elems there are, `cap` how many fit, `count` how many an argument asks
  for, `nth` an ordinal. `num` only in `tda_calloc`, where `malloc`'s spelling wins.

**Macros** are `TDA_UPPER`, but the prefix is for what leaves the translation unit. A
`static` constant or a macro that lives and dies inside one `.c` shares no namespace with
anyone and goes unprefixed: `VEC_GROWTH_BASE`, `INSERTION_THRESHOLD`, `FNV_PRIME_64`.
Names in `src/internal/*.h` keep it, since several `.c` see them at once, and so do the
type-generic wrappers over functions: `TDA_ARR_NEW_LEN(T, …)`, `TDA_ALLOC(T, …)`.

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
  `tda_arr_len` and `tda_arr_elem_size`. Uniform means uniform among *fallible* ops.
- **Allocator wrappers are the value-return exception, and a principled one.** `tda_alloc`,
  `tda_calloc` and `tda_realloc` return the pointer, `nullptr` meaning failure: the value
  and the single error cause share one channel, so `[[nodiscard]]` already flags a dropped
  check. `tda_Status f(…, void **out)` would buy nothing and fight the `malloc` idiom.
- **Everything else keeps the status**, though OOM is nearly its only cause, because the
  status is a type-level marker that an op *can* fail rather than a carrier of causes. The
  `malloc` idiom does not generalize: most fallible ops have nothing to hand back, and
  `bool` is spoken for — it returns *found* from `contains`, `is_empty` and
  `binary_search`, while `hmap` and `hset` `insert` need both meanings at once. A second
  cause is then one more enumerator, not a new signature everywhere.
- **assert vs status.** `assert` is for programmer errors — broken preconditions like
  `elem_size > 0`, `al != nullptr`, `self != nullptr`, `out != nullptr`. Those are bugs,
  not runtime states. `tda_Status` is for data-dependent failures reachable from correct
  code: OOM, and `len * elem_size` overflow folded into `tda_calloc`'s `ckd_mul` →
  `nullptr` → `TDA_STATUS_ERR_NO_MEM`. Never turn one of those into UB or an assert.
- **`TDA_EXPECT` vs `assert`.** A precondition of a public function whose breach would
  touch memory out of bounds — an index, emptiness, a range, matching `elem_size` — is a
  `TDA_EXPECT`, which `TDA_HARDENED` keeps in release. Invariants and anything above O(1)
  stay `assert`.
- Public symbols carry `TDA_API` (see `core/export.h`); everything else stays hidden under
  the library's default-hidden visibility.
