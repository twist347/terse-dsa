#include "tda/alloc/slab.h"

#include "tda/alloc/alloc.h"
#include "tda/core/check.h"

#include "internal/ptr.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ========== internals ========== */

// steps of 1.5x and 2x, so the rounding wastes at most a third of a block
static constexpr size_t CLASS_SIZES[] = {16, 32, 48, 64, 96, 128, 192, 256, 384, 512};
static constexpr size_t CLASS_COUNT = sizeof(CLASS_SIZES) / sizeof(CLASS_SIZES[0]);
static constexpr size_t SMALL_MAX = 512;

// every class is a multiple of 16, so blocks carved from an aligned chunk stay aligned
static_assert(alignof(max_align_t) <= 16);

static constexpr size_t CHUNK_BYTES = 4096;

typedef struct Chunk Chunk;

// the front of every chunk, linking it for the drop
struct Chunk {
    Chunk *next;
};

typedef struct FreeBlock FreeBlock;

struct FreeBlock {
    FreeBlock *next;
};

// a pool of one class: freed blocks first, then the uncarved rest of the newest chunk
typedef struct {
    FreeBlock *free_head;
    unsigned char *carve;
    unsigned char *carve_end;
} SizeClass;

typedef struct {
    tda_Al *parent;
    Chunk *chunks;
    size_t chunk_count;
    size_t small_blocks;
    size_t large_blocks;
    SizeClass classes[CLASS_COUNT];
} SlabCtx;

[[nodiscard]]
static void *slab_alloc(void *ctx, size_t size);

[[nodiscard]]
static void *slab_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size);

static void slab_dealloc(void *ctx, void *ptr, size_t size);

/// the class serving a small 'size'; the one place a size meets the class table
[[nodiscard]]
static size_t class_of(size_t size);

/// where the blocks of a chunk start: past its header, on the alignment grid
[[nodiscard]]
static size_t chunk_head_bytes();

/// a new chunk from the parent, made the carving ground of 'cls'; false when refused
[[nodiscard]]
static bool add_chunk(SlabCtx *slab_ctx, SizeClass *cls);

[[nodiscard]] [[maybe_unused]]
static bool slab_owns(const SlabCtx *slab_ctx, const void *ptr);

#define ASSERT_SLAB(al)                 \
    (assert(al),                        \
     assert((al)->alloc == slab_alloc), \
     assert((al)->ctx))

/* ========== lifetime ========== */

tda_Al *tda_al_slab_new(tda_Al *parent) {
    assert(parent);

    SlabCtx *slab_ctx = tda_alloc(parent, sizeof(SlabCtx));
    if (!slab_ctx) {
        return nullptr;
    }

    tda_Al *obj = tda_alloc(parent, sizeof(tda_Al));
    if (!obj) {
        tda_dealloc(parent, slab_ctx, sizeof(SlabCtx));
        return nullptr;
    }

    *slab_ctx = (SlabCtx){.parent = parent};

    obj->ctx = slab_ctx;
    obj->alloc = slab_alloc;
    obj->calloc = nullptr;
    obj->realloc = slab_realloc;
    obj->dealloc = slab_dealloc;

    return obj;
}

void tda_al_slab_drop(tda_Al *self) {
    if (!self) {
        return;
    }

    ASSERT_SLAB(self);

    SlabCtx *slab_ctx = self->ctx;
    tda_Al *parent = slab_ctx->parent;

    for (Chunk *chunk = slab_ctx->chunks, *next; chunk; chunk = next) {
        next = chunk->next;
        tda_dealloc(parent, chunk, CHUNK_BYTES);
    }

    tda_dealloc(parent, slab_ctx, sizeof(SlabCtx));
    tda_dealloc(parent, self, sizeof(tda_Al));
}

/* ========== stats ========== */

tda_AlSlabStats tda_al_slab_stats(const tda_Al *self) {
    ASSERT_SLAB(self);

    const SlabCtx *slab_ctx = self->ctx;

    return (tda_AlSlabStats){
        .chunks = slab_ctx->chunk_count,
        .chunk_bytes = slab_ctx->chunk_count * CHUNK_BYTES,
        .small_blocks = slab_ctx->small_blocks,
        .large_blocks = slab_ctx->large_blocks,
    };
}

/* ========== internals ========== */

static void *slab_alloc(void *ctx, size_t size) {
    assert(ctx);
    assert(size > 0); // tda_alloc answers a request for nothing itself

    SlabCtx *slab_ctx = ctx;

    if (size > SMALL_MAX) {
        void *ptr = tda_alloc(slab_ctx->parent, size);
        if (ptr) {
            ++slab_ctx->large_blocks;
        }
        return ptr;
    }

    const size_t c = class_of(size);
    SizeClass *cls = &slab_ctx->classes[c];

    void *ptr;
    if (cls->free_head) {
        ptr = cls->free_head;
        cls->free_head = cls->free_head->next;
    } else {
        // a class not yet started has no chunk to carve, and null pointers do not
        // subtract; the tail of a chunk too short for one more block is left unused
        const bool room = cls->carve && (size_t) (cls->carve_end - cls->carve) >= CLASS_SIZES[c];
        if (!room && !add_chunk(slab_ctx, cls)) {
            return nullptr;
        }
        ptr = cls->carve;
        cls->carve += CLASS_SIZES[c];
    }

    ++slab_ctx->small_blocks;

    return ptr;
}

static void *slab_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size) {
    assert(ctx);
    assert(new_size > 0); // tda_realloc answers a request for nothing itself

    SlabCtx *slab_ctx = ctx;

    if (!ptr) {
        return slab_alloc(ctx, new_size);
    }

    const bool old_small = old_size <= SMALL_MAX;
    const bool new_small = new_size <= SMALL_MAX;

    // one class on both sides: the block already has the room
    if (old_small && new_small && class_of(old_size) == class_of(new_size)) {
        return ptr;
    }

    // large on both sides: the parent may resize in place
    if (!old_small && !new_small) {
        return tda_realloc(slab_ctx->parent, ptr, old_size, new_size);
    }

    void *moved = slab_alloc(ctx, new_size);
    if (!moved) {
        return nullptr;
    }

    memcpy(moved, ptr, old_size < new_size ? old_size : new_size);
    slab_dealloc(ctx, ptr, old_size);

    return moved;
}

static void slab_dealloc(void *ctx, void *ptr, size_t size) {
    assert(ctx);
    assert(ptr); // tda_dealloc takes null itself

    SlabCtx *slab_ctx = ctx;

    if (size > SMALL_MAX) {
        assert(slab_ctx->large_blocks > 0);
        --slab_ctx->large_blocks;
        tda_dealloc(slab_ctx->parent, ptr, size);
        return;
    }

    // a foreign block would be handed out again as this slab's own; the check walks the
    // chunks, so only debug builds make it
    assert(slab_owns(slab_ctx, ptr));
    assert(slab_ctx->small_blocks > 0);

    SizeClass *cls = &slab_ctx->classes[class_of(size)];
    FreeBlock *block = ptr;
    block->next = cls->free_head;
    cls->free_head = block;

    --slab_ctx->small_blocks;
}

static size_t class_of(size_t size) {
    assert(size > 0 && size <= SMALL_MAX);

    size_t c = 0;
    while (CLASS_SIZES[c] < size) {
        ++c;
    }
    return c;
}

static size_t chunk_head_bytes() {
    return tda_align_up(sizeof(Chunk), TDA_DEFAULT_ALIGNMENT);
}

static bool add_chunk(SlabCtx *slab_ctx, SizeClass *cls) {
    unsigned char *bytes = tda_alloc(slab_ctx->parent, CHUNK_BYTES);
    if (!bytes) {
        return false;
    }

    // blocks are carved on this grid from the start of the chunk; see the tda_Al contract
    TDA_EXPECT(tda_ptr_is_aligned(bytes, TDA_DEFAULT_ALIGNMENT));

    Chunk *chunk = (Chunk *) bytes;
    chunk->next = slab_ctx->chunks;
    slab_ctx->chunks = chunk;
    ++slab_ctx->chunk_count;

    cls->carve = bytes + chunk_head_bytes();
    cls->carve_end = bytes + CHUNK_BYTES;

    return true;
}

static bool slab_owns(const SlabCtx *slab_ctx, const void *ptr) {
    // addresses, not pointers: ordering a pointer from outside a chunk against the
    // chunk's own is undefined, and one from outside is what this is here to catch
    const uintptr_t addr = (uintptr_t) ptr;

    for (const Chunk *chunk = slab_ctx->chunks; chunk; chunk = chunk->next) {
        const uintptr_t begin = (uintptr_t) chunk + chunk_head_bytes();
        const uintptr_t end = (uintptr_t) chunk + CHUNK_BYTES;
        if (addr >= begin && addr < end) {
            return true;
        }
    }
    return false;
}
