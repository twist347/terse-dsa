#include "tda/alloc/log.h"

#include "tda/alloc/alloc.h"

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ========== internals ========== */

[[nodiscard]]
static void *log_alloc(void *ctx, size_t size);

[[nodiscard]]
static void *log_calloc(void *ctx, size_t num, size_t size);

[[nodiscard]]
static void *log_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size);

static void log_dealloc(void *ctx, void *ptr, size_t size);

typedef struct {
    tda_Al *wrapped;
    FILE *stream;
} LogCtx;

/// one line to the log, flushed at once so that a crash right after it leaves it behind
static void log_line(const LogCtx *log_ctx, const char *fmt, ...);

#define ASSERT_LOG(al)                 \
    (assert(al),                       \
     assert((al)->alloc == log_alloc), \
     assert((al)->ctx))

/* ========== lifetime ========== */

tda_Al *tda_al_log_new(tda_Al *wrapped, FILE *stream) {
    assert(wrapped);
    assert(stream);

    LogCtx *log_ctx = tda_alloc(wrapped, sizeof(LogCtx));
    if (!log_ctx) {
        return nullptr;
    }

    log_ctx->wrapped = wrapped;
    log_ctx->stream = stream;

    tda_Al *obj = tda_alloc(wrapped, sizeof(tda_Al));
    if (!obj) {
        tda_dealloc(wrapped, log_ctx, sizeof(LogCtx));
        return nullptr;
    }

    obj->ctx = log_ctx;
    obj->alloc = log_alloc;
    obj->calloc = log_calloc;
    obj->realloc = log_realloc;
    obj->dealloc = log_dealloc;

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

    tda_dealloc(wrapped, log_ctx, sizeof(LogCtx));
    tda_dealloc(wrapped, self, sizeof(tda_Al));
}

/* ========== internals ========== */

static void *log_alloc(void *ctx, size_t size) {
    assert(ctx);

    const LogCtx *log_ctx = ctx;
    assert(log_ctx->stream);

    void *p = tda_alloc(log_ctx->wrapped, size);
    log_line(log_ctx, "[TDA] alloc size = %zu -> %p\n", size, p);
    return p;
}

static void *log_calloc(void *ctx, size_t num, size_t size) {
    assert(ctx);

    const LogCtx *log_ctx = ctx;
    assert(log_ctx->stream);

    void *p = tda_calloc(log_ctx->wrapped, num, size);
    log_line(log_ctx, "[TDA] calloc num = %zu size = %zu -> %p\n", num, size, p);
    return p;
}

static void *log_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size) {
    assert(ctx);

    const LogCtx *log_ctx = ctx;
    assert(log_ctx->stream);

    // a moved block leaves 'ptr' indeterminate, and even reading it is undefined; the
    // address is kept as an integer while it is still valid
    const uintptr_t old_addr = (uintptr_t) ptr;
    void *p = tda_realloc(log_ctx->wrapped, ptr, old_size, new_size);
    log_line(
        log_ctx,
        "[TDA] realloc %p old size = %zu new_size = %zu -> %p\n",
        (void *) old_addr,
        old_size,
        new_size,
        p
    );
    return p;
}

static void log_dealloc(void *ctx, void *ptr, size_t size) {
    assert(ctx);

    const LogCtx *log_ctx = ctx;
    assert(log_ctx->stream);

    log_line(log_ctx, "[TDA] dealloc %p size = %zu\n", ptr, size);
    tda_dealloc(log_ctx->wrapped, ptr, size);
}

static void log_line(const LogCtx *log_ctx, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(log_ctx->stream, fmt, args);
    va_end(args);

    fflush(log_ctx->stream);
}
