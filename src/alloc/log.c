#include "tda/alloc/log.h"

#include "tda/alloc/alloc.h"

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ========== internals ========== */

// each hook passes the call on as it came, a missing hook of the wrapped allocator
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

typedef struct {
    tda_Al *wrapped;
    FILE *stream;
} LogCtx;

/// writes one line and flushes it, so a crash right after still leaves it in the log
static void log_line(const LogCtx *log_ctx, const char *fmt, ...);

#define ASSERT_LOG(al)                 \
    (assert(al),                       \
     assert((al)->alloc == log_alloc), \
     assert((al)->ctx))

/* ========== lifetime ========== */

tda_Al *tda_al_log_new(tda_Al *wrapped, FILE *stream) {
    assert(wrapped);
    assert(stream);

    LogCtx *log_ctx = TDA_ALLOC(LogCtx, wrapped, 1);
    if (!log_ctx) {
        return nullptr;
    }

    log_ctx->wrapped = wrapped;
    log_ctx->stream = stream;

    tda_Al *obj = TDA_ALLOC(tda_Al, wrapped, 1);
    if (!obj) {
        TDA_DEALLOC(LogCtx, wrapped, log_ctx, 1);
        return nullptr;
    }

    *obj = (tda_Al){
        .ctx = log_ctx,
        .alloc = log_alloc,
        .alloc_zeroed = log_alloc_zeroed,
        .resize = log_resize,
        .remap = log_remap,
        .dealloc = log_dealloc,
    };

    log_line(log_ctx, "[TDA] log allocator created (wrapping %p)\n", (void *) wrapped);

    return obj;
}

void tda_al_log_drop(tda_Al *self) {
    if (!self) {
        return;
    }

    ASSERT_LOG(self);

    LogCtx *log_ctx = self->ctx;

    log_line(log_ctx, "[TDA] log allocator destroyed (wrapping %p)\n", (void *) log_ctx->wrapped);

    tda_Al *wrapped = log_ctx->wrapped;
    assert(wrapped);

    TDA_DEALLOC(LogCtx, wrapped, log_ctx, 1);
    TDA_DEALLOC(tda_Al, wrapped, self, 1);
}

/* ========== internals ========== */

static void *log_alloc(void *ctx, tda_Layout layout) {
    assert(ctx);

    const LogCtx *log_ctx = ctx;

    void *p = tda_alloc(log_ctx->wrapped, layout);
    log_line(log_ctx, "[TDA] alloc size = %zu align = %zu -> %p\n", layout.size, layout.align, p);

    return p;
}

static void *log_alloc_zeroed(void *ctx, tda_Layout layout) {
    assert(ctx);

    const LogCtx *log_ctx = ctx;

    void *p = tda_alloc_zeroed(log_ctx->wrapped, layout);
    log_line(log_ctx, "[TDA] alloc_zeroed size = %zu align = %zu -> %p\n", layout.size, layout.align, p);

    return p;
}

static bool log_resize(void *ctx, void *ptr, tda_Layout old, size_t new_size) {
    assert(ctx);

    const LogCtx *log_ctx = ctx;

    const bool done = tda_resize(log_ctx->wrapped, ptr, old, new_size);
    log_line(
        log_ctx,
        "[TDA] resize %p size = %zu -> %zu align = %zu: %s\n",
        ptr, old.size, new_size, old.align, done ? "in place" : "refused"
    );

    return done;
}

static void *log_remap(void *ctx, void *ptr, tda_Layout old, size_t new_size) {
    assert(ctx);

    const LogCtx *log_ctx = ctx;

    // after a move 'ptr' is indeterminate and even reading it is undefined, so the
    // address is saved as an integer beforehand
    const uintptr_t old_addr = (uintptr_t) ptr;
    void *p = tda_remap(log_ctx->wrapped, ptr, old, new_size);
    log_line(
        log_ctx,
        "[TDA] remap %p size = %zu -> %zu align = %zu -> %p\n",
        (void *) old_addr, old.size, new_size, old.align, p
    );

    return p;
}

static void log_dealloc(void *ctx, void *ptr, tda_Layout layout) {
    assert(ctx);

    const LogCtx *log_ctx = ctx;

    log_line(log_ctx, "[TDA] dealloc %p size = %zu align = %zu\n", ptr, layout.size, layout.align);
    tda_dealloc(log_ctx->wrapped, ptr, layout);
}

static void log_line(const LogCtx *log_ctx, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(log_ctx->stream, fmt, args);
    va_end(args);

    fflush(log_ctx->stream);
}
