#include "tda/alloc/pool.h"

#include "tda/core/check.h"
#include "tda/core/util.h"

#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <stdint.h>

/* ========== internals ========== */

typedef struct PoolNode PoolNode;

struct PoolNode {
    PoolNode *next;
};

typedef struct {
    tda_Al *parent_al; // null when the pool lives in a caller's buffer
    unsigned char *data;
    PoolNode *free_head;
    size_t block_size;
    size_t block_align;
    size_t block_count;
    size_t used;
} PoolCtx;

// what tda_al_pool_from_buf lays at the front of the buffer
typedef struct {
    tda_Al al;
    PoolCtx ctx;
} PoolHead;

[[nodiscard]]
static void *pool_alloc(void *ctx, tda_Layout layout);

/// a block already has the pool's one size, so a new size that fits stays where it is;
/// one that does not cannot be had from this pool at all
[[nodiscard]]
static bool pool_resize(void *ctx, void *ptr, tda_Layout old, size_t new_size);

static void pool_dealloc(void *ctx, void *ptr, tda_Layout layout);

/// 'block' raised to hold and align a free-list pointer, its size then rounded up to its
/// alignment so the blocks tile; true when the rounding overflows
[[nodiscard]]
static bool pool_ckd_block(tda_Layout *out, tda_Layout block);

static void pool_init(
    tda_Al *obj,
    PoolCtx *pool_ctx,
    tda_Al *parent,
    void *data,
    tda_Layout block,
    size_t block_count
);

static void pool_build_free_list(PoolCtx *ctx);

/// the block array, whose size fits: the constructor checked the product
[[nodiscard]]
static tda_Layout pool_data_layout(const PoolCtx *ctx);

[[nodiscard]] [[maybe_unused]]
static bool pool_owns(const PoolCtx *ctx, const void *ptr);

#define ASSERT_POOL(al)                 \
    (assert(al),                        \
     assert((al)->alloc == pool_alloc), \
     assert((al)->ctx))

/* ========== lifetime ========== */

tda_Al *tda_al_pool_new(tda_Al *parent, tda_Layout block, size_t block_count) {
    assert(parent);
    assert(block.size > 0);
    assert(tda_layout_is_align(block.align));
    assert(block_count > 0);

    tda_Layout data_layout;
    if (pool_ckd_block(&block, block) || tda_layout_ckd_array(&data_layout, block, block_count)) {
        return nullptr;
    }

    PoolCtx *pool_ctx = TDA_ALLOC(PoolCtx, parent, 1);
    if (!pool_ctx) {
        return nullptr;
    }

    unsigned char *data = tda_alloc(parent, data_layout);
    if (!data) {
        TDA_DEALLOC(PoolCtx, parent, pool_ctx, 1);
        return nullptr;
    }

    tda_Al *obj = TDA_ALLOC(tda_Al, parent, 1);
    if (!obj) {
        tda_dealloc(parent, data, data_layout);
        TDA_DEALLOC(PoolCtx, parent, pool_ctx, 1);
        return nullptr;
    }

    pool_init(obj, pool_ctx, parent, data, block, block_count);

    return obj;
}

tda_Al *tda_al_pool_from_buf(void *buf, size_t size, tda_Layout block) {
    assert(buf);
    assert(size > 0);
    assert(block.size > 0);
    assert(tda_layout_is_align(block.align));

    if (pool_ckd_block(&block, block)) {
        return nullptr;
    }

    void *head_ptr, *rest_ptr;
    size_t rest;
    if (!tda_buf_split_head(buf, size, sizeof(PoolHead), &head_ptr, &rest_ptr, &rest)) {
        return nullptr;
    }

    // the blocks start on their own alignment, which may be past the header's
    const size_t pad = tda_ptr_align_pad(rest_ptr, block.align);
    if (pad > rest || rest - pad < block.size) {
        return nullptr;
    }

    PoolHead *head = head_ptr;
    void *data = tda_byte_offset_mut(rest_ptr, 1, pad);
    pool_init(&head->al, &head->ctx, nullptr, data, block, (rest - pad) / block.size);

    return &head->al;
}

void tda_al_pool_drop(tda_Al *self) {
    if (!self) {
        return;
    }

    ASSERT_POOL(self);

    PoolCtx *pool_ctx = self->ctx;
    tda_Al *parent_al = pool_ctx->parent_al;
    if (!parent_al) {
        // from_buf: header and blocks live in the caller's buffer
        return;
    }

    tda_dealloc(parent_al, pool_ctx->data, pool_data_layout(pool_ctx));
    TDA_DEALLOC(PoolCtx, parent_al, pool_ctx, 1);
    TDA_DEALLOC(tda_Al, parent_al, self, 1);
}

void tda_al_pool_reset(tda_Al *self) {
    ASSERT_POOL(self);

    PoolCtx *pool_ctx = self->ctx;
    pool_ctx->used = 0;
    pool_build_free_list(pool_ctx);
}

tda_AlPoolStats tda_al_pool_stats(const tda_Al *self) {
    ASSERT_POOL(self);

    const PoolCtx *pool_ctx = self->ctx;

    return (tda_AlPoolStats){
        .block_size = pool_ctx->block_size,
        .block_align = pool_ctx->block_align,
        .block_count = pool_ctx->block_count,
        .used = pool_ctx->used,
        .free = pool_ctx->block_count - pool_ctx->used,
    };
}

/* ========== internals ========== */

static void *pool_alloc(void *ctx, tda_Layout layout) {
    assert(ctx);
    assert(layout.size > 0); // tda_alloc answers a request for nothing itself

    PoolCtx *pool_ctx = ctx;

    // an alignment past the blocks' is how the pool was built, not how full it is; a
    // size past them is a request it cannot serve, as when it runs out
    TDA_EXPECT(layout.align <= pool_ctx->block_align);
    if (layout.size > pool_ctx->block_size) {
        return nullptr;
    }

    PoolNode *node = pool_ctx->free_head;
    if (!node) {
        return nullptr;
    }

    pool_ctx->free_head = node->next;
    ++pool_ctx->used;

    return node;
}

static void pool_dealloc(void *ctx, void *ptr, tda_Layout layout) {
    assert(ctx);
    assert(ptr); // tda_dealloc takes null itself

    PoolCtx *pool_ctx = ctx;
    TDA_UNUSED(layout); // only the check reads it, and a release build drops that

    // the layout is what alloc was asked: it fits one block, or the block is not ours
    TDA_EXPECT(layout.size <= pool_ctx->block_size && layout.align <= pool_ctx->block_align);

    // a foreign block would be threaded into the free list and handed out as the pool's
    // own, so a hardened build keeps these. A block freed twice is caught only when it is
    // still the head of the list or would take 'used' below zero; one freed twice with
    // other frees in between needs a walk of the list, which is not O(1)
    TDA_EXPECT(pool_owns(pool_ctx, ptr));
    TDA_EXPECT(pool_ctx->used > 0);
    TDA_EXPECT(ptr != pool_ctx->free_head);

    // push onto free list
    PoolNode *node = ptr;
    node->next = pool_ctx->free_head;
    pool_ctx->free_head = node;
    --pool_ctx->used;
}

static bool pool_resize(void *ctx, void *ptr, tda_Layout old, size_t new_size) {
    assert(ctx);
    TDA_UNUSED(ptr); // only the ownership check reads it, and a release build drops that
    TDA_UNUSED(old);

    PoolCtx *pool_ctx = ctx;
    TDA_EXPECT(pool_owns(pool_ctx, ptr));

    return new_size <= pool_ctx->block_size;
}

static bool pool_ckd_block(tda_Layout *out, tda_Layout block) {
    // a free block holds the list's pointer, so it has that pointer's size and alignment
    // at least
    const size_t align = block.align > alignof(PoolNode) ? block.align : alignof(PoolNode);
    const size_t size = block.size > sizeof(PoolNode) ? block.size : sizeof(PoolNode);

    out->align = align;

    return tda_ckd_align_up(&out->size, size, align);
}

static void pool_init(
    tda_Al *obj,
    PoolCtx *pool_ctx,
    tda_Al *parent,
    void *data,
    tda_Layout block,
    size_t block_count
) {
    assert(tda_layout_is_type(block));
    assert(tda_ptr_is_aligned(data, block.align));

    pool_ctx->parent_al = parent;
    pool_ctx->data = data;
    pool_ctx->block_size = block.size;
    pool_ctx->block_align = block.align;
    pool_ctx->block_count = block_count;
    pool_ctx->used = 0;

    pool_build_free_list(pool_ctx);

    obj->ctx = pool_ctx;
    obj->alloc = pool_alloc;
    obj->alloc_zeroed = nullptr;
    obj->resize = pool_resize;
    obj->remap = nullptr;
    obj->dealloc = pool_dealloc;
}

static void pool_build_free_list(PoolCtx *ctx) {
    assert(ctx);
    ctx->free_head = nullptr;

    // build list in reverse so that first alloc returns the first block
    for (size_t i = ctx->block_count; i > 0; --i) {
        PoolNode *node = (PoolNode *) tda_byte_offset_mut(ctx->data, ctx->block_size, i - 1);
        node->next = ctx->free_head;
        ctx->free_head = node;
    }
}

static tda_Layout pool_data_layout(const PoolCtx *ctx) {
    return (tda_Layout){.size = ctx->block_size * ctx->block_count, .align = ctx->block_align};
}

static bool pool_owns(const PoolCtx *ctx, const void *ptr) {
    // addresses, not pointers: ordering a pointer from outside the pool against the
    // pool's own is undefined, and one from outside is exactly what this is here to catch
    const uintptr_t addr = (uintptr_t) ptr;
    const uintptr_t begin = (uintptr_t) ctx->data;
    const uintptr_t end = begin + pool_data_layout(ctx).size;

    return addr >= begin && addr < end && (addr - begin) % ctx->block_size == 0;
}
