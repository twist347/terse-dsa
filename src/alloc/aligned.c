#include "tda/alloc/aligned.h"

#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>

/* ========== internals ========== */

[[nodiscard]]
static void *aligned_alloc_block(void *ctx, size_t size);

static void aligned_dealloc_block(void *ctx, void *ptr, size_t size);

/// what a request for 'size' costs the parent: room to slide up to the alignment, plus
/// the word that keeps the parent's address. True when that does not fit in size_t
[[nodiscard]]
static bool ckd_parent_size(size_t *out, size_t size, size_t alignment);

/// ckd_parent_size for a block already handed out, which therefore fits
[[nodiscard]]
static size_t parent_size(size_t size, size_t alignment);

typedef struct {
    tda_Al *parent_al;
    size_t alignment;
} AlignedCtx;

#define ASSERT_ALIGNED(al)                       \
    (assert(al),                                 \
     assert((al)->alloc == aligned_alloc_block), \
     assert((al)->ctx))

/* ========== lifetime ========== */

tda_Al *tda_al_aligned_new(tda_Al *parent, size_t alignment) {
    assert(parent);
    assert(alignment > 0);
    assert((alignment & (alignment - 1)) == 0);
    assert(alignment >= TDA_DEFAULT_ALIGNMENT);

    AlignedCtx *aligned_ctx = tda_alloc(parent, sizeof(AlignedCtx));
    if (!aligned_ctx) {
        return nullptr;
    }

    tda_Al *obj = tda_alloc(parent, sizeof(tda_Al));
    if (!obj) {
        tda_dealloc(parent, aligned_ctx, sizeof(AlignedCtx));
        return nullptr;
    }

    aligned_ctx->parent_al = parent;
    aligned_ctx->alignment = alignment;

    obj->ctx = aligned_ctx;
    obj->alloc = aligned_alloc_block;
    obj->calloc = nullptr;
    obj->realloc = nullptr;
    obj->dealloc = aligned_dealloc_block;

    return obj;
}

void tda_al_aligned_drop(tda_Al *self) {
    if (!self) {
        return;
    }

    ASSERT_ALIGNED(self);

    AlignedCtx *aligned_ctx = self->ctx;
    tda_Al *parent_al = aligned_ctx->parent_al;
    assert(parent_al);

    tda_dealloc(parent_al, aligned_ctx, sizeof(AlignedCtx));
    tda_dealloc(parent_al, self, sizeof(tda_Al));
}

/* ========== internals ========== */

static void *aligned_alloc_block(void *ctx, size_t size) {
    assert(ctx);

    AlignedCtx *aligned_ctx = ctx;
    const size_t alignment = aligned_ctx->alignment;

    if (size == 0) {
        return nullptr;
    }

    size_t bytes;
    if (ckd_parent_size(&bytes, size, alignment)) {
        return nullptr;
    }

    unsigned char *base = tda_alloc(aligned_ctx->parent_al, bytes);
    if (!base) {
        return nullptr;
    }

    unsigned char *ptr = tda_ptr_align_up(base + sizeof(void *), alignment);

    ((void **) ptr)[-1] = base;

    assert(tda_ptr_is_aligned(ptr, alignment));

    return ptr;
}

static void aligned_dealloc_block(void *ctx, void *ptr, size_t size) {
    assert(ctx);
    assert(ptr);

    AlignedCtx *aligned_ctx = ctx;

    void *base = ((void **) ptr)[-1];
    assert((unsigned char *) base <= (unsigned char *) ptr);

    tda_dealloc(aligned_ctx->parent_al, base, parent_size(size, aligned_ctx->alignment));
}

static bool ckd_parent_size(size_t *out, size_t size, size_t alignment) {
    return ckd_add(out, size, alignment - 1) || ckd_add(out, *out, sizeof(void *));
}

static size_t parent_size(size_t size, size_t alignment) {
    size_t bytes;
    [[maybe_unused]] const bool overflow = ckd_parent_size(&bytes, size, alignment);
    assert(!overflow);

    return bytes;
}
