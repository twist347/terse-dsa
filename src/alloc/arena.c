#include "tda/alloc/arena.h"

#include "tda/core/check.h"
#include "tda/core/util.h"

#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ========== internals ========== */

[[nodiscard]]
static void *arena_alloc(void *ctx, tda_Layout layout);

/// resizes the last block in place, since nothing lies past it, and shrinks any other;
/// there is no remap, since a block that has to move costs what the wrapper's copy costs
[[nodiscard]]
static bool arena_resize(void *ctx, void *ptr, tda_Layout old, size_t new_size);

static void arena_dealloc(void *ctx, void *ptr, tda_Layout layout);

/// an arena over 'data'
[[nodiscard]]
static tda_Arena fill(tda_Al parent, void *data, size_t cap);

/// whether the block at 'ptr', handed out as 'size' bytes, is the last one, with only
/// the free tail after it
[[nodiscard]]
static bool is_last_block(const tda_Arena *self, const void *ptr, size_t size);

/// whether 'size' bytes at 'ptr' lie inside what the arena has handed out
[[nodiscard]] [[maybe_unused]]
static bool owns_block(const tda_Arena *self, const void *ptr, size_t size);

/// how far into the block 'ptr' starts
[[nodiscard]]
static size_t block_start(const tda_Arena *self, const void *ptr);

/// where a block of 'size' starting at 'start' would end; false when it would pass the
/// capacity
[[nodiscard]]
static bool block_end(const tda_Arena *self, size_t start, size_t size, size_t *out_end);

/// the arena's whole block, as asked of the parent
[[nodiscard]]
static tda_Layout block_layout(size_t cap);

// the arena is live while it has a block: deinit and '= {}' leave none
#define ASSERT_ARENA(a)                   \
    (assert(a),                           \
     assert((a)->data_),                  \
     assert((a)->cap_ > 0),               \
     assert((a)->floor_ <= (a)->offset_), \
     assert((a)->offset_ <= (a)->cap_))

/* ========== lifetime ========== */

tda_Status tda_arena_init_cap(size_t cap, tda_Al parent, tda_Arena *out) {
    assert(cap > 0);
    assert(parent.vt);
    assert(out);

    // each block is aligned by its own address, so the whole asks for none: any parent
    // serves it, a pool built for small alignments too
    void *data = tda_alloc(parent, block_layout(cap));
    if (!data) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    *out = fill(parent, data, cap);

    return TDA_STATUS_OK;
}

tda_Arena tda_arena_init_buf(void *buf, size_t cap) {
    assert(buf);
    assert(cap > 0);

    return fill((tda_Al){}, buf, cap);
}

void tda_arena_deinit(tda_Arena *self) {
    assert(self);

    // the inert state, never made or already released
    if (!self->data_) {
        return;
    }

    ASSERT_ARENA(self);

    // init_buf: the block is the caller's
    if (self->parent_.vt) {
        tda_dealloc(self->parent_, self->data_, block_layout(self->cap_));
    }

    *self = (tda_Arena){};
}

/* ========== view ========== */

tda_Al tda_arena_to_al(tda_Arena *self) {
    ASSERT_ARENA(self);

    static const tda_AlVtable vtable = {
        .alloc = arena_alloc,
        .alloc_zeroed = nullptr,
        .resize = arena_resize,
        .remap = nullptr,
        .dealloc = arena_dealloc,
    };

    return (tda_Al){.ctx = self, .vt = &vtable};
}

/* ========== mods ========== */

void tda_arena_reset(tda_Arena *self) {
    ASSERT_ARENA(self);

    self->offset_ = 0;
    self->floor_ = 0;
}

tda_ArenaMark tda_arena_mark(tda_Arena *self) {
    ASSERT_ARENA(self);

    const tda_ArenaMark mark = {
        .offset_ = self->offset_,
        .floor_ = self->floor_,
    };
    self->floor_ = self->offset_;

    return mark;
}

void tda_arena_rewind(tda_Arena *self, tda_ArenaMark mark) {
    ASSERT_ARENA(self);
    // only a rewind or a reset moves the offset below a live mark: a block from under
    // the floor never shrinks it, and one from above stays above. So a mark past the
    // offset has already been rewound past
    TDA_EXPECT(mark.offset_ <= self->offset_);
    assert(mark.floor_ <= mark.offset_);

    self->offset_ = mark.offset_;
    self->floor_ = mark.floor_;
}

/* ========== stats ========== */

tda_ArenaStats tda_arena_stats(const tda_Arena *self) {
    ASSERT_ARENA(self);

    return (tda_ArenaStats){
        .cap = self->cap_,
        .used = self->offset_,
        .free = self->cap_ - self->offset_,
    };
}

/* ========== internals ========== */

static void *arena_alloc(void *ctx, tda_Layout layout) {
    assert(ctx);
    tda_Arena *self = ctx;
    ASSERT_ARENA(self);
    assert(layout.size > 0); // tda_alloc answers a request for nothing itself

    // the padding is a count, checked against the capacity before an address is formed
    const void *cursor = tda_byte_offset(self->data_, 1, self->offset_);
    size_t start, end;
    if (ckd_add(&start, self->offset_, tda_align_pad(cursor, layout.align))
        || !block_end(self, start, layout.size, &end)) {
        return nullptr;
    }

    self->offset_ = end;

    return tda_byte_offset_mut(self->data_, 1, start);
}

static bool arena_resize(void *ctx, void *ptr, tda_Layout old, size_t new_size) {
    assert(ctx);
    tda_Arena *self = ctx;
    ASSERT_ARENA(self);
    assert(ptr);
    assert(old.size > 0);
    assert(new_size > 0);
    // the start below is a subtraction from the arena's own pointer, and a foreign
    // block grown in place would set the offset to wherever that lands
    TDA_EXPECT(owns_block(self, ptr, old.size));

    // the last block resizes in place unless it starts under the floor: grown in place
    // it would reach past a mark, and the rewind would hand its tail out again
    const size_t start = block_start(self, ptr);
    if (is_last_block(self, ptr, old.size) && start >= self->floor_) {
        size_t end;
        if (!block_end(self, start, new_size, &end)) {
            return false;
        }
        self->offset_ = end;
        return true;
    }

    // any other block shrinks in place: the arena frees nothing per block, so the tail
    // is lost either way, and a move would cost more and could fail
    return new_size <= old.size;
}

static tda_Arena fill(tda_Al parent, void *data, size_t cap) {
    assert(data);
    assert(cap > 0);

    return (tda_Arena){.parent_ = parent, .data_ = data, .cap_ = cap};
}

static bool is_last_block(const tda_Arena *self, const void *ptr, size_t size) {
    return block_start(self, ptr) + size == self->offset_;
}

static bool owns_block(const tda_Arena *self, const void *ptr, size_t size) {
    // addresses, not pointers: ordering a pointer from outside the arena against the
    // arena's own is undefined, and one from outside is exactly what this is here to catch
    const uintptr_t addr = (uintptr_t) ptr;
    const uintptr_t begin = (uintptr_t) self->data_;

    return addr >= begin && addr - begin <= self->offset_ && size <= self->offset_ - (addr - begin);
}

static size_t block_start(const tda_Arena *self, const void *ptr) {
    return (size_t) tda_byte_diff(ptr, self->data_);
}

static bool block_end(const tda_Arena *self, size_t start, size_t size, size_t *out_end) {
    return !ckd_add(out_end, start, size) && *out_end <= self->cap_;
}

static void arena_dealloc(void *ctx, void *ptr, tda_Layout) {
    assert(ctx);
    tda_Arena *self = ctx;
    ASSERT_ARENA(self);
    assert(ptr); // tda_dealloc takes null itself
    TDA_UNUSED(self); // only the checks read these, and a release build drops them
    TDA_UNUSED(ptr);

    // the arena frees nothing per block
}

static tda_Layout block_layout(size_t cap) {
    return (tda_Layout){.size = cap, .align = 1};
}
