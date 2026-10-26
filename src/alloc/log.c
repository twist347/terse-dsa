#include "tda/alloc/log.h"

#include "tda/alloc/alloc.h"

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ========== internals ========== */

// each hook passes the call on as it came, a missing hook of the parent allocator
// included: the wrappers answer for it as they would without the log

[[nodiscard]]
static void *log_alloc(void *ctx, tda_Layout layout);

[[nodiscard]]
static void *log_alloc_zeroed(void *ctx, tda_Layout layout);

[[nodiscard]]
static bool log_resize(void *ctx, void *ptr, tda_Layout old, size_t new_size);

[[nodiscard]]
static void *log_remap(void *ctx, void *ptr, tda_Layout old, size_t new_size);

static void log_dealloc(void *ctx, void *ptr, tda_Layout layout);

/// writes one line and flushes it, so a crash right after still leaves it in the log
static void log_line(const tda_Log *self, const char *fmt, ...);

// the log is live while it has a parent: deinit and '= {}' leave none
#define ASSERT_LOG(l) \
    (assert(l), assert((l)->parent_.vt), assert((l)->stream_))

/* ========== lifetime ========== */

tda_Log tda_log_init(FILE *stream, tda_Al parent) {
    assert(stream);
    assert(parent.vt);

    const tda_Log obj = {.parent_ = parent, .stream_ = stream};
    log_line(&obj, "[TDA] log allocator created\n");

    return obj;
}

void tda_log_deinit(tda_Log *self) {
    assert(self);

    // the inert state, never made or already released
    if (!self->parent_.vt) {
        return;
    }

    ASSERT_LOG(self);

    log_line(self, "[TDA] log allocator destroyed\n");

    *self = (tda_Log){};
}

/* ========== view ========== */

tda_Al tda_log_to_al(tda_Log *self) {
    ASSERT_LOG(self);

    static const tda_AlVtable vtable = {
        .alloc = log_alloc,
        .alloc_zeroed = log_alloc_zeroed,
        .resize = log_resize,
        .remap = log_remap,
        .dealloc = log_dealloc,
    };

    return (tda_Al){.ctx = self, .vt = &vtable};
}

/* ========== internals ========== */

static void *log_alloc(void *ctx, tda_Layout layout) {
    assert(ctx);
    const tda_Log *self = ctx;
    ASSERT_LOG(self);
    assert(layout.size > 0); // tda_alloc answers a request for nothing itself

    void *ptr = tda_alloc(self->parent_, layout);
    log_line(self, "[TDA] alloc size = %zu align = %zu -> %p\n", layout.size, layout.align, ptr);

    return ptr;
}

static void *log_alloc_zeroed(void *ctx, tda_Layout layout) {
    assert(ctx);
    const tda_Log *self = ctx;
    ASSERT_LOG(self);
    assert(layout.size > 0); // tda_alloc_zeroed answers a request for nothing itself

    void *ptr = tda_alloc_zeroed(self->parent_, layout);
    log_line(self, "[TDA] alloc_zeroed size = %zu align = %zu -> %p\n", layout.size, layout.align, ptr);

    return ptr;
}

static bool log_resize(void *ctx, void *ptr, tda_Layout old, size_t new_size) {
    assert(ctx);
    const tda_Log *self = ctx;
    ASSERT_LOG(self);
    assert(ptr);
    assert(old.size > 0);
    assert(new_size > 0);

    const bool done = tda_resize(self->parent_, ptr, old, new_size);
    log_line(
        self,
        "[TDA] resize %p size = %zu -> %zu align = %zu: %s\n",
        ptr, old.size, new_size, old.align, done ? "in place" : "refused"
    );

    return done;
}

static void *log_remap(void *ctx, void *ptr, tda_Layout old, size_t new_size) {
    assert(ctx);
    const tda_Log *self = ctx;
    ASSERT_LOG(self);
    assert(ptr);
    assert(old.size > 0);
    assert(new_size > 0);

    // after a move 'ptr' is indeterminate and even reading it is undefined, so the
    // address is saved as an integer beforehand
    const uintptr_t old_addr = (uintptr_t) ptr;
    void *moved = tda_remap(self->parent_, ptr, old, new_size);
    log_line(
        self,
        "[TDA] remap %p size = %zu -> %zu align = %zu -> %p\n",
        (void *) old_addr, old.size, new_size, old.align, moved
    );

    return moved;
}

static void log_dealloc(void *ctx, void *ptr, tda_Layout layout) {
    assert(ctx);
    const tda_Log *self = ctx;
    ASSERT_LOG(self);
    assert(ptr); // tda_dealloc takes null itself

    log_line(self, "[TDA] dealloc %p size = %zu align = %zu\n", ptr, layout.size, layout.align);
    tda_dealloc(self->parent_, ptr, layout);
}

static void log_line(const tda_Log *self, const char *fmt, ...) {
    va_list args{};
    va_start(args, fmt);
    vfprintf(self->stream_, fmt, args);
    va_end(args);

    fflush(self->stream_);
}
