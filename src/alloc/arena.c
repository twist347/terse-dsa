#include "tda/alloc/arena.h"

#include "tda/core/check.h"
#include "tda/core/util.h"

#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <stddef.h>
#include <string.h>

/* ========== internals ========== */

[[nodiscard]]
static void *arena_alloc(void *ctx, tda_Layout layout);

/// resizes the last block in place, since nothing lies past it, and shrinks any other;
/// there is no remap, since a block that has to move costs what the wrapper's copy costs
[[nodiscard]]
static bool arena_resize(void *ctx, void *ptr, tda_Layout old, size_t new_size);

static void arena_dealloc(void *ctx, void *ptr, tda_Layout layout);

typedef struct {
    tda_Al *parent_al; // null when the arena lives in a caller's buffer
    void *data;
    size_t cap;
    size_t offset;
    // offset of the latest live mark. A last block starting below it moves to grow, so
    // a rewind to the mark never cuts it short
    size_t floor;
} ArenaCtx;

// what tda_al_arena_from_buf lays at the front of the buffer
typedef struct {
    tda_Al al;
    ArenaCtx ctx;
} ArenaHead;

static void arena_init(tda_Al *obj, ArenaCtx *arena_ctx, tda_Al *parent, void *data, size_t cap);

/// whether the block at 'ptr', handed out as 'size' bytes, is the last one, with only
/// the free tail after it
[[nodiscard]]
static bool is_last_block(const ArenaCtx *arena_ctx, const void *ptr, size_t size);

/// how far into the block 'ptr' starts
[[nodiscard]]
static size_t block_start(const ArenaCtx *arena_ctx, const void *ptr);

/// where a block of 'size' starting at 'start' would end; false when it would pass the
/// capacity
[[nodiscard]]
static bool block_end(const ArenaCtx *arena_ctx, size_t start, size_t size, size_t *out_end);

/// the arena's whole block, as asked of the parent
[[nodiscard]]
static tda_Layout data_layout(size_t cap);

#define ASSERT_ARENA(al)                 \
    (assert(al),                         \
     assert((al)->alloc == arena_alloc), \
     assert((al)->ctx))

/* ========== lifetime ========== */

tda_Al *tda_al_arena_new(tda_Al *parent, size_t cap) {
    assert(parent);
    assert(cap > 0);

    // each block is aligned by its own address, so the whole asks for none: any parent
    // serves it, a pool built for small alignments too
    void *data = tda_alloc(parent, data_layout(cap));
    if (!data) {
        return nullptr;
    }

    ArenaCtx *arena_ctx = TDA_ALLOC(ArenaCtx, parent, 1);
    if (!arena_ctx) {
        tda_dealloc(parent, data, data_layout(cap));
        return nullptr;
    }

    tda_Al *obj = TDA_ALLOC(tda_Al, parent, 1);
    if (!obj) {
        TDA_DEALLOC(ArenaCtx, parent, arena_ctx, 1);
        tda_dealloc(parent, data, data_layout(cap));
        return nullptr;
    }

    arena_init(obj, arena_ctx, parent, data, cap);

    return obj;
}

tda_Al *tda_al_arena_from_buf(void *buf, size_t size) {
    assert(buf);
    assert(size > 0);

    void *head_ptr, *data;
    size_t rest;
    if (!tda_buf_split_head(buf, size, sizeof(ArenaHead), &head_ptr, &data, &rest) || rest == 0) {
        return nullptr;
    }

    ArenaHead *head = head_ptr;
    arena_init(&head->al, &head->ctx, nullptr, data, rest);

    return &head->al;
}

void tda_al_arena_drop(tda_Al *self) {
    if (!self) {
        return;
    }

    ASSERT_ARENA(self);

    ArenaCtx *arena_ctx = self->ctx;
    tda_Al *parent_al = arena_ctx->parent_al;
    if (!parent_al) {
        // from_buf: header and block live in the caller's buffer
        return;
    }

    tda_dealloc(parent_al, arena_ctx->data, data_layout(arena_ctx->cap));
    TDA_DEALLOC(ArenaCtx, parent_al, arena_ctx, 1);
    TDA_DEALLOC(tda_Al, parent_al, self, 1);
}

/* ========== mods ========== */

void tda_al_arena_reset(tda_Al *self) {
    ASSERT_ARENA(self);

    ArenaCtx *arena_ctx = self->ctx;
    arena_ctx->offset = 0;
    arena_ctx->floor = 0;
}

tda_AlArenaMark tda_al_arena_mark(tda_Al *self) {
    ASSERT_ARENA(self);

    ArenaCtx *arena_ctx = self->ctx;
    assert(arena_ctx->floor <= arena_ctx->offset);

    const tda_AlArenaMark mark = {
        .arena = arena_ctx,
        .offset = arena_ctx->offset,
        .floor = arena_ctx->floor,
    };
    arena_ctx->floor = arena_ctx->offset;

    return mark;
}

void tda_al_arena_rewind(tda_Al *self, tda_AlArenaMark mark) {
    ASSERT_ARENA(self);

    ArenaCtx *arena_ctx = self->ctx;
    TDA_EXPECT(mark.arena == arena_ctx);
    // only a rewind or a reset moves the offset below a live mark: a block from under
    // the floor never shrinks it, and one from above stays above. So a mark past the
    // offset has already been rewound past
    TDA_EXPECT(mark.offset <= arena_ctx->offset);
    assert(mark.floor <= mark.offset);

    arena_ctx->offset = mark.offset;
    arena_ctx->floor = mark.floor;
}

/* ========== stats ========== */

tda_AlArenaStats tda_al_arena_stats(const tda_Al *self) {
    ASSERT_ARENA(self);

    const ArenaCtx *arena_ctx = self->ctx;

    return (tda_AlArenaStats){
        .cap = arena_ctx->cap,
        .used = arena_ctx->offset,
        .available = arena_ctx->cap - arena_ctx->offset,
    };
}

/* ========== internals ========== */

static void *arena_alloc(void *ctx, tda_Layout layout) {
    assert(ctx);
    assert(layout.size > 0); // tda_alloc answers a request for nothing itself

    ArenaCtx *arena_ctx = ctx;

    // the padding is a count, checked against the capacity before an address is formed
    const void *cursor = tda_byte_offset(arena_ctx->data, 1, arena_ctx->offset);
    size_t start;
    size_t end;
    if (ckd_add(&start, arena_ctx->offset, tda_ptr_align_pad(cursor, layout.align))
        || !block_end(arena_ctx, start, layout.size, &end)) {
        return nullptr;
    }

    arena_ctx->offset = end;

    return tda_byte_offset_mut(arena_ctx->data, 1, start);
}

static bool arena_resize(void *ctx, void *ptr, tda_Layout old, size_t new_size) {
    assert(ctx);

    ArenaCtx *arena_ctx = ctx;

    // the last block resizes in place unless it starts under the floor: grown in place
    // it would reach past a mark, and the rewind would hand its tail out again
    const size_t start = block_start(arena_ctx, ptr);
    if (is_last_block(arena_ctx, ptr, old.size) && start >= arena_ctx->floor) {
        size_t end;
        if (!block_end(arena_ctx, start, new_size, &end)) {
            return false;
        }
        arena_ctx->offset = end;
        return true;
    }

    // any other block shrinks in place: the arena frees nothing per block, so the tail
    // is lost either way, and a move would cost more and could fail
    return new_size <= old.size;
}

static void arena_init(tda_Al *obj, ArenaCtx *arena_ctx, tda_Al *parent, void *data, size_t cap) {
    arena_ctx->parent_al = parent;
    arena_ctx->data = data;
    arena_ctx->cap = cap;
    arena_ctx->offset = 0;
    arena_ctx->floor = 0;

    obj->ctx = arena_ctx;
    obj->alloc = arena_alloc;
    obj->alloc_zeroed = nullptr;
    obj->resize = arena_resize;
    obj->remap = nullptr;
    obj->dealloc = arena_dealloc;
}

static bool is_last_block(const ArenaCtx *arena_ctx, const void *ptr, size_t size) {
    return block_start(arena_ctx, ptr) + size == arena_ctx->offset;
}

static size_t block_start(const ArenaCtx *arena_ctx, const void *ptr) {
    return (size_t) tda_byte_diff(ptr, arena_ctx->data);
}

static bool block_end(const ArenaCtx *arena_ctx, size_t start, size_t size, size_t *out_end) {
    return !ckd_add(out_end, start, size) && *out_end <= arena_ctx->cap;
}

static void arena_dealloc(void *ctx, void *ptr, tda_Layout layout) {
    // the arena frees nothing per block

    TDA_UNUSED(ctx);
    TDA_UNUSED(ptr);
    TDA_UNUSED(layout);
}

static tda_Layout data_layout(size_t cap) {
    return (tda_Layout){.size = cap, .align = 1};
}
