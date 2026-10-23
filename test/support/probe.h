#pragma once

#include "tda/alloc/alloc.h"
#include "tda/alloc/default.h"

#include <assert.h>
#include <stdint.h>

/* ========== type ========== */

/// counting allocator over tda_al_default that can be told to fail on demand.
/// Each instance carries its own state; pass it as the al's ctx, so two
/// probes in one test never share counters.
typedef struct {
    size_t alloc_calls;
    size_t zeroed_calls;
    size_t resize_calls;
    size_t remap_calls;
    size_t dealloc_calls;
    size_t live; // blocks handed out and not yet freed
    tda_Layout last_alloc;
    tda_Layout last_dealloc;
    size_t fail_after; // requests served before the probe starts refusing
} tda_TestProbe;

/* ========== info ========== */

/// count of every request that may hand out new memory; a resize only keeps a block
[[nodiscard]]
static inline size_t tda_test_probe_requests(const tda_TestProbe *self) {
    assert(self);

    return self->alloc_calls + self->zeroed_calls + self->remap_calls;
}

/* ========== mods ========== */

static inline void tda_test_probe_reset(tda_TestProbe *self) {
    assert(self);

    *self = (tda_TestProbe){.fail_after = SIZE_MAX};
}

/// lets the next 'count' requests through and refuses the one after them
static inline void tda_test_probe_fail_after_next(tda_TestProbe *self, size_t count) {
    assert(self);

    self->fail_after = tda_test_probe_requests(self) + count;
}

/* ========== hooks ========== */

[[nodiscard]]
static inline bool tda_test_probe_refuses_(const tda_TestProbe *self) {
    return tda_test_probe_requests(self) > self->fail_after;
}

[[nodiscard]]
static inline void *tda_test_probe_alloc_(void *ctx, tda_Layout layout) {
    tda_TestProbe *self = ctx;
    ++self->alloc_calls;
    self->last_alloc = layout;

    if (tda_test_probe_refuses_(self)) {
        return nullptr;
    }

    void *ptr = tda_alloc(tda_al_default(), layout);
    if (ptr) {
        ++self->live;
    }

    return ptr;
}

[[nodiscard]]
static inline void *tda_test_probe_alloc_zeroed_(void *ctx, tda_Layout layout) {
    tda_TestProbe *self = ctx;
    ++self->zeroed_calls;
    self->last_alloc = layout;

    if (tda_test_probe_refuses_(self)) {
        return nullptr;
    }

    void *ptr = tda_alloc_zeroed(tda_al_default(), layout);
    if (ptr) {
        ++self->live;
    }

    return ptr;
}

// shrinks in place and never grows, so a growth always reaches remap
[[nodiscard]]
static inline bool tda_test_probe_resize_(void *ctx, void *, tda_Layout old, size_t new_size) {
    tda_TestProbe *self = ctx;
    ++self->resize_calls;

    return new_size <= old.size;
}

[[nodiscard]]
static inline void *tda_test_probe_remap_(void *ctx, void *ptr, tda_Layout old, size_t new_size) {
    tda_TestProbe *self = ctx;
    ++self->remap_calls;

    if (tda_test_probe_refuses_(self)) {
        return nullptr; // ptr stays valid, as tda_remap promises
    }

    // default's remap declines an over-aligned block, and so does the probe
    return tda_remap(tda_al_default(), ptr, old, new_size);
}

static inline void tda_test_probe_dealloc_(void *ctx, void *ptr, tda_Layout layout) {
    tda_TestProbe *self = ctx;
    assert(ptr); // the wrapper filters nullptr out

    ++self->dealloc_calls;
    self->last_dealloc = layout;
    --self->live;

    tda_dealloc(tda_al_default(), ptr, layout);
}

/* ========== allocators ========== */

/// alloc and dealloc only: the wrappers must build the rest
[[nodiscard]]
static inline tda_Al tda_test_probe_bare(tda_TestProbe *self) {
    assert(self);

    return (tda_Al){
        .ctx = self,
        .alloc = tda_test_probe_alloc_,
        .dealloc = tda_test_probe_dealloc_,
    };
}

/// all five hooks: the wrappers must prefer these over their fallbacks
[[nodiscard]]
static inline tda_Al tda_test_probe_full(tda_TestProbe *self) {
    assert(self);

    return (tda_Al){
        .ctx = self,
        .alloc = tda_test_probe_alloc_,
        .alloc_zeroed = tda_test_probe_alloc_zeroed_,
        .resize = tda_test_probe_resize_,
        .remap = tda_test_probe_remap_,
        .dealloc = tda_test_probe_dealloc_,
    };
}
