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
static void *arena_alloc(void *ctx, size_t size);

/// resizes the last block in place, since nothing lies past it; any other block moves,
/// and its old slot stays used like every block the arena has lent
[[nodiscard]]
static void *arena_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size);

static void arena_dealloc(void *ctx, void *ptr, size_t size);

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

#define ASSERT_ARENA(al)                 \
    (assert(al),                         \
     assert((al)->alloc == arena_alloc), \
     assert((al)->ctx))

/* ========== lifetime ========== */

tda_Al *tda_al_arena_new(tda_Al *parent, size_t cap) {
    assert(parent);
    assert(cap > 0);

    // every request is rounded up to the alignment, so a tail past the last multiple of
    // it could never be handed out
    size_t block_size;
    if (tda_ckd_align_up(&block_size, cap, TDA_DEFAULT_ALIGNMENT)) {
        return nullptr;
    }

    void *data = tda_alloc(parent, block_size);
    if (!data) {
        return nullptr;
    }

    ArenaCtx *arena_ctx = tda_alloc(parent, sizeof(ArenaCtx));
    if (!arena_ctx) {
        tda_dealloc(parent, data, block_size);
        return nullptr;
    }

    tda_Al *obj = tda_alloc(parent, sizeof(tda_Al));
    if (!obj) {
        tda_dealloc(parent, arena_ctx, sizeof(ArenaCtx));
        tda_dealloc(parent, data, block_size);
        return nullptr;
    }

    // blocks are carved on this grid from the start of data; see the tda_Al contract
    TDA_EXPECT(tda_ptr_is_aligned(data, TDA_DEFAULT_ALIGNMENT));

    arena_init(obj, arena_ctx, parent, data, block_size);

    return obj;
}

tda_Al *tda_al_arena_from_buf(void *buf, size_t size) {
    assert(buf);
    assert(size > 0);

    void *head_ptr, *data;
    size_t rest;
    if (!tda_buf_split_head(buf, size, sizeof(ArenaHead), &head_ptr, &data, &rest) || rest < TDA_DEFAULT_ALIGNMENT) {
        return nullptr;
    }

    // cut down to whole slots, as tda_al_arena_new rounds up; a partial tail is unusable
    ArenaHead *head = head_ptr;
    arena_init(&head->al, &head->ctx, nullptr, data, tda_align_down(rest, TDA_DEFAULT_ALIGNMENT));

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

    tda_dealloc(parent_al, arena_ctx->data, arena_ctx->cap);
    tda_dealloc(parent_al, arena_ctx, sizeof(ArenaCtx));
    tda_dealloc(parent_al, self, sizeof(tda_Al));
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

static void *arena_alloc(void *ctx, size_t size) {
    assert(ctx);

    ArenaCtx *arena_ctx = ctx;

    if (size == 0) {
        return nullptr;
    }

    size_t aligned_size;
    size_t end;
    if (tda_ckd_align_up(&aligned_size, size, TDA_DEFAULT_ALIGNMENT)
        || ckd_add(&end, arena_ctx->offset, aligned_size)
        || end > arena_ctx->cap) {
        return nullptr;
    }

    void *ptr = tda_byte_offset_mut(arena_ctx->data, 1, arena_ctx->offset);
    arena_ctx->offset += aligned_size;

    return ptr;
}

static void *arena_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size) {
    assert(ctx);
    assert(new_size > 0); // tda_realloc answers a request for nothing itself

    ArenaCtx *arena_ctx = ctx;

    if (!ptr) {
        return arena_alloc(ctx, new_size);
    }

    // the last block resizes in place unless it starts under the floor: grown in place
    // it would reach past a mark, and the rewind would hand its tail out again
    if (is_last_block(arena_ctx, ptr, old_size) && block_start(arena_ctx, ptr) >= arena_ctx->floor) {
        const size_t start = arena_ctx->offset - tda_align_up(old_size, TDA_DEFAULT_ALIGNMENT);
        size_t aligned_size;
        size_t end;
        if (tda_ckd_align_up(&aligned_size, new_size, TDA_DEFAULT_ALIGNMENT) ||
            ckd_add(&end, start, aligned_size) ||
            end > arena_ctx->cap
        ) {
            return nullptr;
        }
        arena_ctx->offset = end;
        return ptr;
    }

    // any other block shrinks in place: the arena frees nothing per block, so the tail
    // is lost either way, and a move would cost more and could fail
    if (new_size <= old_size) {
        return ptr;
    }

    void *new_ptr = arena_alloc(ctx, new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
    }
    return new_ptr;
}

static void arena_init(tda_Al *obj, ArenaCtx *arena_ctx, tda_Al *parent, void *data, size_t cap) {
    arena_ctx->parent_al = parent;
    arena_ctx->data = data;
    arena_ctx->cap = cap;
    arena_ctx->offset = 0;
    arena_ctx->floor = 0;

    obj->ctx = arena_ctx;
    obj->alloc = arena_alloc;
    obj->calloc = nullptr;
    obj->realloc = arena_realloc;
    obj->dealloc = arena_dealloc;
}

static bool is_last_block(const ArenaCtx *arena_ctx, const void *ptr, size_t size) {
    return block_start(arena_ctx, ptr) + tda_align_up(size, TDA_DEFAULT_ALIGNMENT) == arena_ctx->offset;
}

static size_t block_start(const ArenaCtx *arena_ctx, const void *ptr) {
    return (size_t) tda_byte_diff(ptr, arena_ctx->data);
}

static void arena_dealloc(void *ctx, void *ptr, size_t size) {
    // the arena frees nothing per block

    TDA_UNUSED(ctx);
    TDA_UNUSED(ptr);
    TDA_UNUSED(size);
}
