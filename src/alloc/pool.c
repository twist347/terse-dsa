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
    tda_Al *parent_al; // null when the pool lives in a buffer of the caller's
    unsigned char *data;
    PoolNode *free_head;
    size_t block_size;
    size_t block_count;
    size_t used;
} PoolCtx;

// what tda_al_pool_from_buf lays at the front of the buffer
typedef struct {
    tda_Al al;
    PoolCtx ctx;
} PoolHead;

[[nodiscard]]
static void *pool_alloc(void *ctx, size_t size);

/// a block already has the pool's one size, so a new size that fits stays where it is;
/// one that does not cannot be had from this pool at all
[[nodiscard]]
static void *pool_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size);

static void pool_dealloc(void *ctx, void *ptr, size_t size);

/// 'block_size' raised to hold a free-list pointer and rounded up to the alignment; false
/// when the rounding overflows
[[nodiscard]]
static bool pool_round_block_size(size_t *block_size);

static void pool_init(
    tda_Al *obj,
    PoolCtx *pool_ctx,
    tda_Al *parent,
    void *data,
    size_t block_size,
    size_t block_count
);

static void pool_build_free_list(PoolCtx *ctx);

[[nodiscard]] [[maybe_unused]]
static bool pool_owns(const PoolCtx *ctx, const void *ptr);

#define ASSERT_POOL(al)                 \
    (assert(al),                        \
     assert((al)->alloc == pool_alloc), \
     assert((al)->ctx))

/* ========== lifetime ========== */

tda_Al *tda_al_pool_new(tda_Al *parent, size_t block_size, size_t block_count) {
    assert(parent);
    assert(block_size > 0);
    assert(block_count > 0);

    if (!pool_round_block_size(&block_size)) {
        return nullptr;
    }

    // allocate backing buffer
    size_t total_bytes;
    if (ckd_mul(&total_bytes, block_size, block_count)) {
        return nullptr;
    }

    // allocate context
    PoolCtx *pool_ctx = tda_alloc(parent, sizeof(PoolCtx));
    if (!pool_ctx) {
        return nullptr;
    }

    unsigned char *data = tda_alloc(parent, total_bytes);
    if (!data) {
        tda_dealloc(parent, pool_ctx, sizeof(PoolCtx));
        return nullptr;
    }

    assert(tda_ptr_is_aligned(data, TDA_DEFAULT_ALIGNMENT));

    // allocate the tda_Al itself
    tda_Al *obj = tda_alloc(parent, sizeof(tda_Al));
    if (!obj) {
        tda_dealloc(parent, data, total_bytes);
        tda_dealloc(parent, pool_ctx, sizeof(PoolCtx));
        return nullptr;
    }

    pool_init(obj, pool_ctx, parent, data, block_size, block_count);

    return obj;
}

tda_Al *tda_al_pool_from_buf(void *buf, size_t size, size_t block_size) {
    assert(buf);
    assert(size > 0);
    assert(block_size > 0);

    if (!pool_round_block_size(&block_size)) {
        return nullptr;
    }

    // the header goes first and the blocks right after it, both aligned; offsets and not
    // pointers until they are known to fit, so no address is formed past the buffer
    const size_t head_offset = tda_ptr_align_pad(buf, TDA_DEFAULT_ALIGNMENT);
    size_t data_offset;
    if (ckd_add(&data_offset, head_offset, tda_align_up(sizeof(PoolHead), TDA_DEFAULT_ALIGNMENT))
        || data_offset >= size) {
        return nullptr;
    }

    const size_t block_count = (size - data_offset) / block_size;
    if (block_count == 0) {
        return nullptr;
    }

    PoolHead *head = (void *) tda_byte_offset_mut(buf, 1, head_offset);
    pool_init(
        &head->al,
        &head->ctx,
        nullptr,
        tda_byte_offset_mut(buf, 1, data_offset),
        block_size,
        block_count
    );

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
        // from_buf: header and blocks are the caller's buffer, and nothing was taken
        return;
    }

    tda_dealloc(parent_al, pool_ctx->data, pool_ctx->block_size * pool_ctx->block_count);
    tda_dealloc(parent_al, pool_ctx, sizeof(PoolCtx));
    tda_dealloc(parent_al, self, sizeof(tda_Al));
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
        .block_count = pool_ctx->block_count,
        .used = pool_ctx->used,
        .free = pool_ctx->block_count - pool_ctx->used,
    };
}

/* ========== internals ========== */

static void *pool_alloc(void *ctx, size_t size) {
    assert(ctx);

    if (size == 0) {
        return nullptr;
    }

    PoolCtx *pool_ctx = ctx;
    if (size > pool_ctx->block_size) {
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

static void pool_dealloc(void *ctx, void *ptr, size_t size) {
    assert(ctx);
    TDA_UNUSED(size);

    if (!ptr) {
        return;
    }

    PoolCtx *pool_ctx = ctx;

    // a foreign block would be threaded into the free list and handed out as the pool's
    // own, so a hardened build keeps these; a block freed twice is caught only when it
    // would take 'used' below zero — anything more is not O(1)
    TDA_EXPECT(pool_owns(pool_ctx, ptr));
    TDA_EXPECT(pool_ctx->used > 0);

    // push onto free list
    PoolNode *node = ptr;
    node->next = pool_ctx->free_head;
    pool_ctx->free_head = node;
    --pool_ctx->used;
}

static void *pool_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size) {
    assert(ctx);
    assert(new_size > 0); // tda_realloc answers a request for nothing itself
    TDA_UNUSED(old_size);

    PoolCtx *pool_ctx = ctx;

    if (!ptr) {
        return pool_alloc(ctx, new_size);
    }

    TDA_EXPECT(pool_owns(pool_ctx, ptr));

    return new_size <= pool_ctx->block_size ? ptr : nullptr;
}

static bool pool_round_block_size(size_t *block_size) {
    // each block must hold at least a free-list pointer
    if (*block_size < sizeof(PoolNode)) {
        *block_size = sizeof(PoolNode);
    }

    return !tda_ckd_align_up(block_size, *block_size, TDA_DEFAULT_ALIGNMENT);
}

static void pool_init(
    tda_Al *obj,
    PoolCtx *pool_ctx,
    tda_Al *parent,
    void *data,
    size_t block_size,
    size_t block_count
) {
    pool_ctx->parent_al = parent;
    pool_ctx->data = data;
    pool_ctx->block_size = block_size;
    pool_ctx->block_count = block_count;
    pool_ctx->used = 0;

    pool_build_free_list(pool_ctx);

    obj->ctx = pool_ctx;
    obj->alloc = pool_alloc;
    obj->calloc = nullptr;
    obj->realloc = pool_realloc;
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

static bool pool_owns(const PoolCtx *ctx, const void *ptr) {
    // addresses, not pointers: ordering a pointer from outside the pool against the pool's
    // own is undefined, and one from outside is exactly what this is here to catch
    const uintptr_t addr = (uintptr_t) ptr;
    const uintptr_t begin = (uintptr_t) ctx->data;
    const uintptr_t end = begin + ctx->block_size * ctx->block_count;

    return addr >= begin && addr < end && (addr - begin) % ctx->block_size == 0;
}
