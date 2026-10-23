#include "tda/alloc/pool.h"

#include "tda/core/check.h"
#include "tda/core/util.h"

#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <stdint.h>

/* ========== internals ========== */

// a free block, holding the link to the next free one
typedef struct tda_PoolFree_ FreeBlock;

struct tda_PoolFree_ {
    FreeBlock *next;
};

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
static bool ckd_block(tda_Layout *out, tda_Layout block);

/// fills in a pool over 'data', its interface pointing back at it
static void fill(tda_Pool *obj, tda_Al *parent, void *data, tda_Layout block, size_t block_count);

static void build_free_list(tda_Pool *self);

/// the block array, whose size fits: the constructor checked the product
[[nodiscard]]
static tda_Layout array_layout(const tda_Pool *self);

[[nodiscard]] [[maybe_unused]]
static bool owns_block(const tda_Pool *self, const void *ptr);

// the ctx check is how a moved or copied allocator gets caught, so it stays under
// TDA_HARDENED
#define ASSERT_POOL(p)                     \
    (assert(p),                            \
     assert((p)->al_.alloc == pool_alloc), \
     TDA_EXPECT((p)->al_.ctx == (p)))

/* ========== lifetime ========== */

tda_Status tda_pool_init(tda_Al *parent, tda_Layout block, size_t block_count, tda_Pool *out) {
    assert(parent);
    assert(block.size > 0);
    assert(tda_layout_is_align(block.align));
    assert(block_count > 0);
    assert(out);

    tda_Layout data_layout;
    if (ckd_block(&block, block) || tda_layout_ckd_array(&data_layout, block, block_count)) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    unsigned char *data = tda_alloc(parent, data_layout);
    if (!data) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    fill(out, parent, data, block, block_count);

    return TDA_STATUS_OK;
}

tda_Status tda_pool_init_buf(void *buf, size_t cap, tda_Layout block, tda_Pool *out) {
    assert(buf);
    assert(cap > 0);
    assert(block.size > 0);
    assert(tda_layout_is_align(block.align));
    assert(out);

    if (ckd_block(&block, block)) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    // the blocks start on their own alignment, which may be past the buffer's
    const size_t pad = tda_ptr_align_pad(buf, block.align);
    if (pad > cap || cap - pad < block.size) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    void *data = tda_byte_offset_mut(buf, 1, pad);
    fill(out, nullptr, data, block, (cap - pad) / block.size);

    return TDA_STATUS_OK;
}

void tda_pool_deinit(tda_Pool *self) {
    assert(self);

    // the inert state, never filled in or already released
    if (!self->al_.alloc) {
        return;
    }

    ASSERT_POOL(self);

    // init_buf: the blocks are the caller's
    if (self->parent_) {
        tda_dealloc(self->parent_, self->data_, array_layout(self));
    }

    *self = (tda_Pool){};
}

void tda_pool_reset(tda_Pool *self) {
    ASSERT_POOL(self);

    self->used_ = 0;
    build_free_list(self);
}

/* ========== stats ========== */

tda_PoolStats tda_pool_stats(const tda_Pool *self) {
    ASSERT_POOL(self);

    return (tda_PoolStats){
        .block_size = self->block_size_,
        .block_align = self->block_align_,
        .blocks = self->block_count_,
        .used = self->used_,
        .free = self->block_count_ - self->used_,
    };
}

/* ========== internals ========== */

static void *pool_alloc(void *ctx, tda_Layout layout) {
    assert(ctx);
    assert(layout.size > 0); // tda_alloc answers a request for nothing itself

    tda_Pool *self = ctx;

    // an alignment past the blocks' is how the pool was built, not how full it is; a
    // size past them is a request it cannot serve, as when it runs out
    TDA_EXPECT(layout.align <= self->block_align_);
    if (layout.size > self->block_size_) {
        return nullptr;
    }

    FreeBlock *node = self->free_head_;
    if (!node) {
        return nullptr;
    }

    self->free_head_ = node->next;
    ++self->used_;

    return node;
}

static void pool_dealloc(void *ctx, void *ptr, tda_Layout layout) {
    assert(ctx);
    assert(ptr); // tda_dealloc takes null itself

    tda_Pool *self = ctx;
    TDA_UNUSED(layout); // only the check reads it, and a release build drops that

    // the layout is what alloc was asked: it fits one block, or the block is not ours
    TDA_EXPECT(layout.size <= self->block_size_ && layout.align <= self->block_align_);

    // a foreign block would be threaded into the free list and handed out as the pool's
    // own, so a hardened build keeps these. A block freed twice is caught only when it is
    // still the head of the list or would take 'used' below zero; one freed twice with
    // other frees in between needs a walk of the list, which is not O(1)
    TDA_EXPECT(owns_block(self, ptr));
    TDA_EXPECT(self->used_ > 0);
    TDA_EXPECT(ptr != self->free_head_);

    // push onto free list
    FreeBlock *node = ptr;
    node->next = self->free_head_;
    self->free_head_ = node;
    --self->used_;
}

static bool pool_resize(void *ctx, void *ptr, tda_Layout old, size_t new_size) {
    assert(ctx);
    TDA_UNUSED(ptr); // only the ownership check reads it, and a release build drops that
    TDA_UNUSED(old);

    tda_Pool *self = ctx;
    TDA_EXPECT(owns_block(self, ptr));

    return new_size <= self->block_size_;
}

static bool ckd_block(tda_Layout *out, tda_Layout block) {
    // a free block holds the list's pointer, so it has that pointer's size and alignment
    // at least
    const size_t align = block.align > alignof(FreeBlock) ? block.align : alignof(FreeBlock);
    const size_t size = block.size > sizeof(FreeBlock) ? block.size : sizeof(FreeBlock);

    out->align = align;

    return tda_ckd_align_up(&out->size, size, align);
}

static void fill(tda_Pool *obj, tda_Al *parent, void *data, tda_Layout block, size_t block_count) {
    assert(tda_layout_is_type(block));
    assert(tda_ptr_is_aligned(data, block.align));

    *obj = (tda_Pool){
        .al_ = {
            .ctx = obj,
            .alloc = pool_alloc,
            .resize = pool_resize,
            .dealloc = pool_dealloc,
        },
        .parent_ = parent,
        .data_ = data,
        .block_size_ = block.size,
        .block_align_ = block.align,
        .block_count_ = block_count,
    };

    build_free_list(obj);
}

static void build_free_list(tda_Pool *self) {
    assert(self);
    self->free_head_ = nullptr;

    // build list in reverse so that first alloc returns the first block
    for (size_t i = self->block_count_; i > 0; --i) {
        FreeBlock *node = (FreeBlock *) tda_byte_offset_mut(self->data_, self->block_size_, i - 1);
        node->next = self->free_head_;
        self->free_head_ = node;
    }
}

static tda_Layout array_layout(const tda_Pool *self) {
    return (tda_Layout){.size = self->block_size_ * self->block_count_, .align = self->block_align_};
}

static bool owns_block(const tda_Pool *self, const void *ptr) {
    // addresses, not pointers: ordering a pointer from outside the pool against the
    // pool's own is undefined, and one from outside is exactly what this is here to catch
    const uintptr_t addr = (uintptr_t) ptr;
    const uintptr_t begin = (uintptr_t) self->data_;
    const uintptr_t end = begin + array_layout(self).size;

    return addr >= begin && addr < end && (addr - begin) % self->block_size_ == 0;
}
