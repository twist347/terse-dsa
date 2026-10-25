#include "tda/alloc/default.h"

#include "tda/core/util.h"

#include "internal/ptr.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <malloc.h>
#endif

/* ========== internals ========== */

/// malloc's own alignment, alignof(max_align_t): what the plain libc calls are owed for a
/// block at least that long
static constexpr size_t PLAIN_ALIGN = alignof(max_align_t);

// libc came before alignment did: malloc, calloc and realloc know only their own, and
// C11 added aligned_alloc alone. So each hook takes the plain call up to that alignment
// and aligned_alloc past it, which also keeps clear of the small alignments some
// aligned_alloc refuse: macOS takes nothing under the size of a pointer

/// whether the plain calls serve 'align': it is within malloc's own
[[nodiscard]]
static bool is_plain(size_t align);

/// what to ask the plain calls for so the block is owed 'align'. C23 promises malloc(n)
/// only the alignment of types of size n or less, so a block shorter than its alignment
/// asks for a whole max_align_t, which is owed every plain alignment
[[nodiscard]]
static size_t plain_size(size_t size, size_t align);

[[nodiscard]]
static void *default_alloc(void *ctx, tda_Layout layout);

[[nodiscard]]
static void *default_alloc_zeroed(void *ctx, tda_Layout layout);

[[nodiscard]]
static void *default_remap(void *ctx, void *ptr, tda_Layout old, size_t new_size);

static void default_dealloc(void *ctx, void *ptr, tda_Layout layout);

/* ========== lifetime ========== */

tda_Al *tda_al_default() {
    // no resize: libc has no call that grows a block only where it lies
    static tda_Al static_al = {
        .ctx = nullptr,
        .alloc = default_alloc,
        .alloc_zeroed = default_alloc_zeroed,
        .resize = nullptr,
        .remap = default_remap,
        .dealloc = default_dealloc,
    };

    return &static_al;
}

/* ========== internals ========== */

static bool is_plain(size_t align) {
    return align <= PLAIN_ALIGN;
}

static size_t plain_size(size_t size, size_t align) {
    assert(is_plain(align));

    return size >= align ? size : sizeof(max_align_t);
}

static void *default_alloc(void *, tda_Layout layout) {
    assert(layout.size > 0); // tda_alloc answers a request for nothing itself

    if (is_plain(layout.align)) {
        return malloc(plain_size(layout.size, layout.align));
    }

#ifdef _WIN32
    // UCRT has no aligned_alloc, and its aligned blocks go back through _aligned_free
    return _aligned_malloc(layout.size, layout.align);
#else
    // aligned_alloc takes a size that is a multiple of the alignment
    size_t size;
    if (tda_align_ckd_up(&size, layout.size, layout.align)) {
        return nullptr;
    }

    return aligned_alloc(layout.align, size);
#endif
}

static void *default_alloc_zeroed(void *ctx, tda_Layout layout) {
    assert(layout.size > 0); // tda_alloc_zeroed answers a request for nothing itself

    // calloc's large blocks come from the system already zeroed, and it skips the memset
    if (is_plain(layout.align)) {
        return calloc(1, plain_size(layout.size, layout.align));
    }

    void *ptr = default_alloc(ctx, layout);
    if (ptr) {
        memset(ptr, 0, layout.size);
    }

    return ptr;
}

static void *default_remap(void *, void *ptr, tda_Layout old, size_t new_size) {
    assert(ptr);
    assert(old.size > 0);
    assert(new_size > 0);

    // realloc may move a large block by remapping its pages instead of copying them, but
    // it keeps only malloc's own alignment, and only what the new size is owed
    if (!is_plain(old.align)) {
        return nullptr;
    }

    return realloc(ptr, plain_size(new_size, old.align));
}

static void default_dealloc(void *, void *ptr, tda_Layout layout) {
    assert(ptr); // tda_dealloc takes null itself

#ifdef _WIN32
    if (!is_plain(layout.align)) {
        _aligned_free(ptr);
        return;
    }
#else
    TDA_UNUSED(layout);
#endif

    free(ptr);
}
