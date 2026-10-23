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

static constexpr size_t CHUNK_BYTES = 4096;

// blocks are carved from the start of a chunk at a multiple of their class, so a block
// is aligned to the largest power of two dividing its class, as long as the chunk is
// aligned to at least that. 512 covers every class
static constexpr size_t CHUNK_ALIGN = 512;

typedef struct Chunk Chunk;

// the back of every chunk, linking it for the drop; at the back so the blocks start at
// the chunk's own alignment
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
static void *slab_alloc(void *ctx, tda_Layout layout);

/// in place while the class stays the same; a large block asks the parent
[[nodiscard]]
static bool slab_resize(void *ctx, void *ptr, tda_Layout old, size_t new_size);

/// a large block that stays large is the parent's to move; anything else has no move
/// cheaper than the wrapper's copy
[[nodiscard]]
static void *slab_remap(void *ctx, void *ptr, tda_Layout old, size_t new_size);

static void slab_dealloc(void *ctx, void *ptr, tda_Layout layout);

/// the class serving 'layout', or CLASS_COUNT when it goes to the parent; the one place
/// a request meets the class table
[[nodiscard]]
static size_t class_of(tda_Layout layout);

/// the chunk whose link is 'chunk', from its first byte
[[nodiscard]]
static unsigned char *chunk_start(const Chunk *chunk);

[[nodiscard]]
static tda_Layout chunk_layout();

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

    SlabCtx *slab_ctx = TDA_ALLOC(SlabCtx, parent, 1);
    if (!slab_ctx) {
        return nullptr;
    }

    tda_Al *obj = TDA_ALLOC(tda_Al, parent, 1);
    if (!obj) {
        TDA_DEALLOC(SlabCtx, parent, slab_ctx, 1);
        return nullptr;
    }

    *slab_ctx = (SlabCtx){.parent = parent};

    *obj = (tda_Al){
        .ctx = slab_ctx,
        .alloc = slab_alloc,
        .alloc_zeroed = nullptr,
        .resize = slab_resize,
        .remap = slab_remap,
        .dealloc = slab_dealloc,
    };

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
        tda_dealloc(parent, chunk_start(chunk), chunk_layout());
    }

    TDA_DEALLOC(SlabCtx, parent, slab_ctx, 1);
    TDA_DEALLOC(tda_Al, parent, self, 1);
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

static void *slab_alloc(void *ctx, tda_Layout layout) {
    assert(ctx);
    assert(layout.size > 0); // tda_alloc answers a request for nothing itself

    SlabCtx *slab_ctx = ctx;

    const size_t c = class_of(layout);
    if (c == CLASS_COUNT) {
        void *ptr = tda_alloc(slab_ctx->parent, layout);
        if (ptr) {
            ++slab_ctx->large_blocks;
        }
        return ptr;
    }

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

static bool slab_resize(void *ctx, void *ptr, tda_Layout old, size_t new_size) {
    assert(ctx);

    SlabCtx *slab_ctx = ctx;

    const tda_Layout new_layout = {.size = new_size, .align = old.align};
    const size_t old_class = class_of(old);
    const size_t new_class = class_of(new_layout);

    if (old_class == CLASS_COUNT && new_class == CLASS_COUNT) {
        return tda_resize(slab_ctx->parent, ptr, old, new_size);
    }

    // one class on both sides: the block already has the room
    return old_class == new_class;
}

static void *slab_remap(void *ctx, void *ptr, tda_Layout old, size_t new_size) {
    assert(ctx);

    SlabCtx *slab_ctx = ctx;

    const tda_Layout new_layout = {.size = new_size, .align = old.align};
    if (class_of(old) == CLASS_COUNT && class_of(new_layout) == CLASS_COUNT) {
        return tda_remap(slab_ctx->parent, ptr, old, new_size);
    }

    return nullptr;
}

static void slab_dealloc(void *ctx, void *ptr, tda_Layout layout) {
    assert(ctx);
    assert(ptr); // tda_dealloc takes null itself

    SlabCtx *slab_ctx = ctx;

    const size_t c = class_of(layout);
    if (c == CLASS_COUNT) {
        assert(slab_ctx->large_blocks > 0);
        --slab_ctx->large_blocks;
        tda_dealloc(slab_ctx->parent, ptr, layout);
        return;
    }

    // a foreign block would be handed out again as this slab's own; the check walks the
    // chunks, so only debug builds make it
    assert(slab_owns(slab_ctx, ptr));
    assert(slab_ctx->small_blocks > 0);

    SizeClass *cls = &slab_ctx->classes[c];
    FreeBlock *block = ptr;
    block->next = cls->free_head;
    cls->free_head = block;

    --slab_ctx->small_blocks;
}

static size_t class_of(tda_Layout layout) {
    assert(layout.size > 0);

    // a block at a multiple of its class from an aligned chunk is aligned to the lowest
    // set bit of the class, so a class serves an alignment up to that bit. Every class
    // takes a pointer-aligned free-list link, which 16 already covers
    for (size_t c = 0; c < CLASS_COUNT; ++c) {
        const size_t cls = CLASS_SIZES[c];
        if (cls >= layout.size && (cls & -cls) >= layout.align) {
            return c;
        }
    }

    return CLASS_COUNT;
}

static unsigned char *chunk_start(const Chunk *chunk) {
    return (unsigned char *) chunk - (CHUNK_BYTES - sizeof(Chunk));
}

static tda_Layout chunk_layout() {
    return (tda_Layout){.size = CHUNK_BYTES, .align = CHUNK_ALIGN};
}

static bool add_chunk(SlabCtx *slab_ctx, SizeClass *cls) {
    unsigned char *bytes = tda_alloc(slab_ctx->parent, chunk_layout());
    if (!bytes) {
        return false;
    }

    Chunk *chunk = (Chunk *) (bytes + CHUNK_BYTES - sizeof(Chunk));
    chunk->next = slab_ctx->chunks;
    slab_ctx->chunks = chunk;
    ++slab_ctx->chunk_count;

    cls->carve = bytes;
    cls->carve_end = (unsigned char *) chunk;

    return true;
}

static bool slab_owns(const SlabCtx *slab_ctx, const void *ptr) {
    // addresses, not pointers: ordering a pointer from outside a chunk against the
    // chunk's own is undefined, and one from outside is what this is here to catch
    const uintptr_t addr = (uintptr_t) ptr;

    for (const Chunk *chunk = slab_ctx->chunks; chunk; chunk = chunk->next) {
        const uintptr_t begin = (uintptr_t) chunk_start(chunk);
        const uintptr_t end = (uintptr_t) chunk;
        if (addr >= begin && addr < end) {
            return true;
        }
    }

    return false;
}
