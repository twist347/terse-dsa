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

// the header sizes the slab's table of classes without seeing this one
static_assert(CLASS_COUNT == TDA_SLAB_CLASSES_);

static constexpr size_t CHUNK_BYTES = 4096;

// blocks are carved from the start of a chunk at a multiple of their class, so a block
// is aligned to the largest power of two dividing its class, as long as the chunk is
// aligned to at least that. 512 covers every class
static constexpr size_t CHUNK_ALIGN = 512;

typedef struct tda_SlabChunk_ Chunk;

// the back of every chunk, linking it for the deinit; at the back so the blocks start
// at the chunk's own alignment
struct tda_SlabChunk_ {
    Chunk *next;
};

// the bytes of a chunk left for blocks, and so where its link sits
static constexpr size_t CHUNK_ROOM = CHUNK_BYTES - sizeof(Chunk);

typedef struct tda_SlabFree_ FreeBlock;

struct tda_SlabFree_ {
    FreeBlock *next;
};

typedef tda_SlabClass_ SizeClass;

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

/// whether a block too large for any class stays so at 'new_size'
[[nodiscard]]
static bool stays_large(tda_Layout old, size_t new_size);

/// the chunk whose link is 'chunk', from its first byte
[[nodiscard]]
static unsigned char *chunk_start(Chunk *chunk);

[[nodiscard]]
static tda_Layout chunk_layout();

/// a new chunk from the parent, made the carving ground of 'cls'; false when refused
[[nodiscard]]
static bool add_chunk(tda_Slab *self, SizeClass *cls);

/// whether the chunk 'cls' carves from has room for one more block of 'size'
[[nodiscard]]
static bool can_carve(const SizeClass *cls, size_t size);

/// a fresh block of 'size' from the chunk of 'cls', starting a new chunk when that one
/// is used up; null when the parent refuses
[[nodiscard]]
static void *carve(tda_Slab *self, SizeClass *cls, size_t size);

[[nodiscard]] [[maybe_unused]]
static bool owns_block(const tda_Slab *self, const void *ptr);

// the slab is live while it has a parent: deinit and '= {}' leave none
#define ASSERT_SLAB(s) \
    (assert(s), assert((s)->parent_.vt))

/* ========== lifetime ========== */

tda_Slab tda_slab_init(tda_Al parent) {
    assert(parent.vt);

    return (tda_Slab){.parent_ = parent};
}

void tda_slab_deinit(tda_Slab *self) {
    assert(self);

    // the inert state, never made or already released
    if (!self->parent_.vt) {
        return;
    }

    ASSERT_SLAB(self);

    for (Chunk *chunk = self->chunks_, *next; chunk; chunk = next) {
        next = chunk->next;
        tda_dealloc(self->parent_, chunk_start(chunk), chunk_layout());
    }

    *self = (tda_Slab){};
}

/* ========== view ========== */

tda_Al tda_slab_to_al(tda_Slab *self) {
    ASSERT_SLAB(self);

    static const tda_AlVtable vtable = {
        .alloc = slab_alloc,
        .alloc_zeroed = nullptr,
        .resize = slab_resize,
        .remap = slab_remap,
        .dealloc = slab_dealloc,
    };

    return (tda_Al){.ctx = self, .vt = &vtable};
}

/* ========== stats ========== */

tda_SlabStats tda_slab_stats(const tda_Slab *self) {
    ASSERT_SLAB(self);

    return (tda_SlabStats){
        .chunks = self->chunk_count_,
        .chunk_bytes = self->chunk_count_ * CHUNK_BYTES,
        .small_blocks = self->small_blocks_,
        .large_blocks = self->large_blocks_,
    };
}

/* ========== internals ========== */

static void *slab_alloc(void *ctx, tda_Layout layout) {
    assert(ctx);
    tda_Slab *self = ctx;
    ASSERT_SLAB(self);
    assert(layout.size > 0); // tda_alloc answers a request for nothing itself

    const size_t idx = class_of(layout);
    if (idx == CLASS_COUNT) {
        void *ptr = tda_alloc(self->parent_, layout);
        if (ptr) {
            ++self->large_blocks_;
        }
        return ptr;
    }

    SizeClass *cls = &self->classes_[idx];

    void *ptr;
    if (cls->free_head) {
        ptr = cls->free_head;
        cls->free_head = cls->free_head->next;
    } else {
        ptr = carve(self, cls, CLASS_SIZES[idx]);
        if (!ptr) {
            return nullptr;
        }
    }

    ++self->small_blocks_;

    return ptr;
}

static bool slab_resize(void *ctx, void *ptr, tda_Layout old, size_t new_size) {
    assert(ctx);
    const tda_Slab *self = ctx;
    ASSERT_SLAB(self);
    assert(ptr);
    assert(old.size > 0);
    assert(new_size > 0);
    // a large block is the parent's, which checks it; the walk keeps this to debug builds
    assert(class_of(old) == CLASS_COUNT || owns_block(self, ptr));

    if (stays_large(old, new_size)) {
        return tda_resize(self->parent_, ptr, old, new_size);
    }

    // one class on both sides: the block already has the room
    return class_of(old) == class_of((tda_Layout){.size = new_size, .align = old.align});
}

static void *slab_remap(void *ctx, void *ptr, tda_Layout old, size_t new_size) {
    assert(ctx);
    const tda_Slab *self = ctx;
    ASSERT_SLAB(self);
    assert(ptr);
    assert(old.size > 0);
    assert(new_size > 0);
    // a large block is the parent's, which checks it; the walk keeps this to debug builds
    assert(class_of(old) == CLASS_COUNT || owns_block(self, ptr));

    if (stays_large(old, new_size)) {
        return tda_remap(self->parent_, ptr, old, new_size);
    }

    return nullptr;
}

static void slab_dealloc(void *ctx, void *ptr, tda_Layout layout) {
    assert(ctx);
    tda_Slab *self = ctx;
    ASSERT_SLAB(self);
    assert(ptr); // tda_dealloc takes null itself

    const size_t idx = class_of(layout);
    if (idx == CLASS_COUNT) {
        TDA_EXPECT(self->large_blocks_ > 0);
        --self->large_blocks_;
        tda_dealloc(self->parent_, ptr, layout);
        return;
    }

    // a block freed twice is caught, as in the pool, only while it is still the head of
    // its class's list or would take the count below zero. A foreign block would be
    // handed out again as this slab's own; that check walks the chunks, so only debug
    // builds make it
    SizeClass *cls = &self->classes_[idx];
    TDA_EXPECT(self->small_blocks_ > 0);
    TDA_EXPECT(ptr != cls->free_head);
    assert(owns_block(self, ptr));

    FreeBlock *block = ptr;
    block->next = cls->free_head;
    cls->free_head = block;

    --self->small_blocks_;
}

static size_t class_of(tda_Layout layout) {
    assert(layout.size > 0);

    // a block at a multiple of its class from an aligned chunk is aligned to the lowest
    // set bit of the class, so a class serves an alignment up to that bit. Every class
    // takes a pointer-aligned free-list link, which 16 already covers
    for (size_t idx = 0; idx < CLASS_COUNT; ++idx) {
        const size_t size = CLASS_SIZES[idx];
        if (size >= layout.size && (size & -size) >= layout.align) {
            return idx;
        }
    }

    return CLASS_COUNT;
}

static bool stays_large(tda_Layout old, size_t new_size) {
    const tda_Layout new_layout = {.size = new_size, .align = old.align};

    return class_of(old) == CLASS_COUNT && class_of(new_layout) == CLASS_COUNT;
}

static unsigned char *chunk_start(Chunk *chunk) {
    return (unsigned char *) chunk - CHUNK_ROOM;
}

static tda_Layout chunk_layout() {
    return (tda_Layout){.size = CHUNK_BYTES, .align = CHUNK_ALIGN};
}

static bool add_chunk(tda_Slab *self, SizeClass *cls) {
    unsigned char *bytes = tda_alloc(self->parent_, chunk_layout());
    if (!bytes) {
        return false;
    }

    Chunk *chunk = (Chunk *) (bytes + CHUNK_ROOM);
    chunk->next = self->chunks_;
    self->chunks_ = chunk;
    ++self->chunk_count_;

    cls->carve = bytes;
    cls->carve_end = (unsigned char *) chunk;

    return true;
}

static bool can_carve(const SizeClass *cls, size_t size) {
    // a class not yet started has no chunk to carve, and null pointers do not subtract
    return cls->carve && (size_t) (cls->carve_end - cls->carve) >= size;
}

static void *carve(tda_Slab *self, SizeClass *cls, size_t size) {
    // the tail of a chunk too short for one more block is left unused
    if (!can_carve(cls, size) && !add_chunk(self, cls)) {
        return nullptr;
    }

    void *ptr = cls->carve;
    cls->carve += size;

    return ptr;
}

static bool owns_block(const tda_Slab *self, const void *ptr) {
    // addresses, not pointers: ordering a pointer from outside a chunk against the
    // chunk's own is undefined, and one from outside is what this is here to catch
    const uintptr_t addr = (uintptr_t) ptr;

    for (const Chunk *chunk = self->chunks_; chunk; chunk = chunk->next) {
        const uintptr_t end = (uintptr_t) chunk;
        const uintptr_t begin = end - CHUNK_ROOM;
        if (addr >= begin && addr < end) {
            return true;
        }
    }

    return false;
}
