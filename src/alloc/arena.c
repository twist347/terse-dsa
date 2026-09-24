#include "tda/alloc/arena.h"

#include "tda/core/util.h"

#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <stddef.h>
#include <string.h>

/* ========== internals ========== */

[[nodiscard]]
static void *arena_alloc(void *ctx, size_t size);

/// grows or shrinks the last block where it stands, since nothing lies past it; any
/// other block moves, and the old one stays behind like every block the arena has lent
[[nodiscard]]
static void *arena_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size);

static void arena_dealloc(void *ctx, void *ptr, size_t size);

typedef struct {
    tda_Al *parent_al; // null when the arena lives in a buffer of the caller's
    void *data;
    size_t cap;
    size_t offset;
} ArenaCtx;

// what tda_al_arena_from_buf lays at the front of the buffer
typedef struct {
    tda_Al al;
    ArenaCtx ctx;
} ArenaHead;

static void arena_init(tda_Al *obj, ArenaCtx *arena_ctx, tda_Al *parent, void *data, size_t cap);

/// whether the block at 'ptr', 'size' bytes when it was handed out, is the last one: the
/// only block with nothing but the free tail after it
[[nodiscard]]
static bool is_last_block(const ArenaCtx *arena_ctx, const void *ptr, size_t size);

#define ASSERT_ARENA(al)                 \
    (assert(al),                         \
     assert((al)->alloc == arena_alloc), \
     assert((al)->ctx))

/* ========== lifetime ========== */

tda_Al *tda_al_arena_new(tda_Al *parent, size_t cap) {
    assert(parent);
    assert(cap > 0);

    void *data = tda_alloc(parent, cap);
    if (!data) {
        return nullptr;
    }

    ArenaCtx *arena_ctx = tda_alloc(parent, sizeof(ArenaCtx));
    if (!arena_ctx) {
        tda_dealloc(parent, data, cap);
        return nullptr;
    }

    tda_Al *obj = tda_alloc(parent, sizeof(tda_Al));
    if (!obj) {
        tda_dealloc(parent, arena_ctx, sizeof(ArenaCtx));
        tda_dealloc(parent, data, cap);
        return nullptr;
    }

    assert(tda_ptr_is_aligned(data, TDA_DEFAULT_ALIGNMENT));

    arena_init(obj, arena_ctx, parent, data, cap);

    return obj;
}

tda_Al *tda_al_arena_from_buf(void *buf, size_t size) {
    assert(buf);
    assert(size > 0);

    // the header goes first and the block right after it, both aligned; offsets and not
    // pointers until they are known to fit, so no address is formed past the buffer
    const size_t head_offset = tda_ptr_align_pad(buf, TDA_DEFAULT_ALIGNMENT);
    size_t data_offset;
    if (ckd_add(&data_offset, head_offset, tda_align_up(sizeof(ArenaHead), TDA_DEFAULT_ALIGNMENT))
        || data_offset >= size) {
        return nullptr;
    }

    ArenaHead *head = (void *) tda_byte_offset_mut(buf, 1, head_offset);
    arena_init(&head->al, &head->ctx, nullptr, tda_byte_offset_mut(buf, 1, data_offset), size - data_offset);

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
        // from_buf: header and block are the caller's buffer, and nothing was taken
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

    if (is_last_block(arena_ctx, ptr, old_size)) {
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

    // any other block shrinks where it stands: the arena takes nothing back per block, so
    // the tail is lost either way, and a move would only spend more and could fail
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

    obj->ctx = arena_ctx;
    obj->alloc = arena_alloc;
    obj->calloc = nullptr;
    obj->realloc = arena_realloc;
    obj->dealloc = arena_dealloc;
}

static bool is_last_block(const ArenaCtx *arena_ctx, const void *ptr, size_t size) {
    const size_t start = (size_t) tda_byte_diff(ptr, arena_ctx->data);
    return start + tda_align_up(size, TDA_DEFAULT_ALIGNMENT) == arena_ctx->offset;
}

static void arena_dealloc(void *ctx, void *ptr, size_t size) {
    // arena doesn't free individual allocations

    TDA_UNUSED(ctx);
    TDA_UNUSED(ptr);
    TDA_UNUSED(size);
}
