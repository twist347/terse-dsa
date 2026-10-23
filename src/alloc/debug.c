#include "tda/alloc/debug.h"

#include "tda/alloc/alloc.h"
#include "tda/alloc/default.h"
#include "tda/core/hash.h"
#include "tda/core/util.h"

#include <assert.h>
#include <stdarg.h>
#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========== internals ========== */

// the fills of the MSVC debug heap, so they read the same in a debugger
static constexpr unsigned char FILL_NEW = 0xCD;
static constexpr unsigned char FILL_FREED = 0xDD;
static constexpr unsigned char FILL_GUARD = 0xFD;

// bytes past each block that must stay FILL_GUARD; asked of the parent on top of 'size'
static constexpr size_t GUARD_SIZE = 16;

// freed blocks held back from the parent, so a write through a stale pointer lands in
// memory that is still checked
static constexpr size_t QUARANTINE_LEN = 64;

static constexpr size_t TABLE_MIN_CAP = 64;

typedef struct {
    uintptr_t addr; // 0 marks an empty slot; an allocator never hands out null
    tda_Layout layout;
    uint64_t serial;
} Entry;

// open addressing over the block addresses, linear probing, at most half full
typedef struct {
    Entry *slots;
    size_t cap;
    size_t len;
} Table;

typedef struct {
    void *ptr;
    tda_Layout layout;
    uint64_t serial;
} Freed;

typedef struct {
    tda_Al *parent;
    Table table;
    Freed quarantine[QUARANTINE_LEN]; // a ring; 'quarantine_len' oldest-first from 'head'
    size_t quarantine_head;
    size_t quarantine_len;
    size_t live_bytes;
    size_t peak_bytes;
    uint64_t requests;
    size_t fail_after; // requests let through before refusing starts
    uint64_t break_at;
} DebugCtx;

[[nodiscard]]
static void *debug_alloc(void *ctx, tda_Layout layout);

/// moves nothing: the wrapper's alloc, copy and dealloc keep both blocks checked. It is
/// there to check the old layout before that copy reads 'old.size' bytes of the block
[[nodiscard]]
static void *debug_remap(void *ctx, void *ptr, tda_Layout old, size_t new_size);

static void debug_dealloc(void *ctx, void *ptr, tda_Layout layout);

/// the record of the live block at 'ptr', which must have been made with 'layout'; any
/// other pointer or layout is reported, as being 'given back' or 'resized' to say which
/// call did it. 'take' also takes the record out of the table
[[nodiscard]]
static Entry checked_entry(DebugCtx *debug_ctx, void *ptr, tda_Layout layout, bool take, const char *as);

/// writes "tda debug: ..." to stderr and aborts
[[noreturn]]
static void report(const char *fmt, ...);

/// the next serial, after the break and refusal checks; 0 means refuse the request
[[nodiscard]]
static uint64_t next_serial(DebugCtx *debug_ctx);

/// the i-th quarantined block, oldest first
[[nodiscard]]
static Freed *quarantined(DebugCtx *debug_ctx, size_t i);

/// moves the oldest quarantined block back to the parent, checking its fill
static void release_oldest(DebugCtx *debug_ctx);

/// whether 'n' bytes at 'ptr' all equal 'fill'
[[nodiscard]]
static bool all_equal(const unsigned char *ptr, size_t n, unsigned char fill);

/// what a block of 'layout' costs the parent, the guard included. True when that does
/// not fit in size_t
[[nodiscard]]
static bool ckd_parent_layout(tda_Layout *out, tda_Layout layout);

/// ckd_parent_layout for a block already handed out, which therefore fits
[[nodiscard]]
static tda_Layout parent_layout(tda_Layout layout);

/// the slot a probe for 'addr' starts from
[[nodiscard]]
static size_t table_home(const Table *table, uintptr_t addr);

/// the slot where 'addr' is, or the empty slot where it would go
[[nodiscard]]
static size_t table_probe(const Table *table, uintptr_t addr);

/// records a new block; false when the table could not grow
[[nodiscard]]
static bool table_insert(Table *table, Entry entry);

/// takes the entry for 'addr' out; false when there is none
[[nodiscard]]
static bool table_take(Table *table, uintptr_t addr, Entry *out);

#define ASSERT_DEBUG(al)                 \
    (assert(al),                         \
     assert((al)->alloc == debug_alloc), \
     assert((al)->ctx))

/* ========== lifetime ========== */

tda_Al *tda_al_debug_new(tda_Al *parent) {
    assert(parent);

    DebugCtx *debug_ctx = TDA_ALLOC(DebugCtx, parent, 1);
    if (!debug_ctx) {
        return nullptr;
    }

    tda_Al *obj = TDA_ALLOC(tda_Al, parent, 1);
    if (!obj) {
        TDA_DEALLOC(DebugCtx, parent, debug_ctx, 1);
        return nullptr;
    }

    *debug_ctx = (DebugCtx){.parent = parent, .fail_after = SIZE_MAX};

    // no resize, and a remap that only checks: every growth moves, through the wrapper's
    // alloc, copy and dealloc, so every block passes the checks
    *obj = (tda_Al){
        .ctx = debug_ctx,
        .alloc = debug_alloc,
        .alloc_zeroed = nullptr,
        .resize = nullptr,
        .remap = debug_remap,
        .dealloc = debug_dealloc,
    };

    return obj;
}

void tda_al_debug_drop(tda_Al *self) {
    if (!self) {
        return;
    }

    ASSERT_DEBUG(self);

    DebugCtx *debug_ctx = self->ctx;
    while (debug_ctx->quarantine_len > 0) {
        release_oldest(debug_ctx);
    }

    TDA_DEALLOC(Entry, tda_al_default(), debug_ctx->table.slots, debug_ctx->table.cap);

    tda_Al *parent = debug_ctx->parent;
    TDA_DEALLOC(DebugCtx, parent, debug_ctx, 1);
    TDA_DEALLOC(tda_Al, parent, self, 1);
}

/* ========== stats ========== */

tda_AlDebugStats tda_al_debug_stats(const tda_Al *self) {
    ASSERT_DEBUG(self);

    const DebugCtx *debug_ctx = self->ctx;

    return (tda_AlDebugStats){
        .live_blocks = debug_ctx->table.len,
        .live_bytes = debug_ctx->live_bytes,
        .peak_bytes = debug_ctx->peak_bytes,
        .requests = debug_ctx->requests,
    };
}

size_t tda_al_debug_fprint_leaks(const tda_Al *self, FILE *stream) {
    ASSERT_DEBUG(self);
    assert(stream);

    const DebugCtx *debug_ctx = self->ctx;
    const Table *table = &debug_ctx->table;

    for (size_t i = 0; i < table->cap; ++i) {
        const Entry *e = &table->slots[i];
        if (e->addr != 0) {
            fprintf(
                stream,
                "tda debug: leak: block #%llu, %zu bytes at %p\n",
                (unsigned long long) e->serial, e->layout.size, (void *) e->addr
            );
        }
    }
    fprintf(
        stream,
        "tda debug: %zu leaked, %zu bytes\n",
        table->len, debug_ctx->live_bytes
    );

    return table->len;
}

/* ========== fault injection ========== */

void tda_al_debug_fail_after(tda_Al *self, size_t n) {
    ASSERT_DEBUG(self);

    DebugCtx *debug_ctx = self->ctx;

    size_t at;
    debug_ctx->fail_after = ckd_add(&at, (size_t) debug_ctx->requests, n) ? SIZE_MAX : at;
}

void tda_al_debug_break_at(tda_Al *self, uint64_t serial) {
    ASSERT_DEBUG(self);

    DebugCtx *debug_ctx = self->ctx;
    debug_ctx->break_at = serial;
}

/* ========== internals ========== */

static void *debug_alloc(void *ctx, tda_Layout layout) {
    assert(ctx);
    assert(layout.size > 0); // tda_alloc answers a request for nothing itself

    DebugCtx *debug_ctx = ctx;

    const uint64_t serial = next_serial(debug_ctx);
    if (serial == 0) {
        return nullptr;
    }

    tda_Layout with_guard;
    if (ckd_parent_layout(&with_guard, layout)) {
        return nullptr;
    }

    unsigned char *block = tda_alloc(debug_ctx->parent, with_guard);
    if (!block) {
        return nullptr;
    }

    const Entry entry = {.addr = (uintptr_t) block, .layout = layout, .serial = serial};
    if (!table_insert(&debug_ctx->table, entry)) {
        tda_dealloc(debug_ctx->parent, block, with_guard);
        return nullptr;
    }

    memset(block, FILL_NEW, layout.size);
    memset(block + layout.size, FILL_GUARD, GUARD_SIZE);

    debug_ctx->live_bytes += layout.size;
    if (debug_ctx->live_bytes > debug_ctx->peak_bytes) {
        debug_ctx->peak_bytes = debug_ctx->live_bytes;
    }

    return block;
}

static void *debug_remap(void *ctx, void *ptr, tda_Layout old, size_t new_size) {
    assert(ctx);
    TDA_UNUSED(new_size);

    [[maybe_unused]] const Entry entry = checked_entry(ctx, ptr, old, false, "resized");

    return nullptr;
}

static void debug_dealloc(void *ctx, void *ptr, tda_Layout layout) {
    assert(ctx);
    assert(ptr); // tda_dealloc takes null itself

    DebugCtx *debug_ctx = ctx;

    const Entry entry = checked_entry(debug_ctx, ptr, layout, true, "given back");

    unsigned char *block = ptr;
    if (!all_equal(block + layout.size, GUARD_SIZE, FILL_GUARD)) {
        report(
            "block #%llu at %p (%zu bytes) was written past its end",
            (unsigned long long) entry.serial, ptr, layout.size
        );
    }

    debug_ctx->live_bytes -= layout.size;
    memset(block, FILL_FREED, parent_layout(layout).size);

    if (debug_ctx->quarantine_len == QUARANTINE_LEN) {
        release_oldest(debug_ctx);
    }
    ++debug_ctx->quarantine_len;
    *quarantined(debug_ctx, debug_ctx->quarantine_len - 1) =
            (Freed){.ptr = ptr, .layout = layout, .serial = entry.serial};
}

static Entry checked_entry(DebugCtx *debug_ctx, void *ptr, tda_Layout layout, bool take, const char *as) {
    Table *table = &debug_ctx->table;
    const uintptr_t addr = (uintptr_t) ptr;

    Entry entry;
    const bool found = take
        ? table_take(table, addr, &entry)
        : table->cap > 0 && (entry = table->slots[table_probe(table, addr)]).addr != 0;
    if (!found) {
        for (size_t i = 0; i < debug_ctx->quarantine_len; ++i) {
            const Freed *f = quarantined(debug_ctx, i);
            if (f->ptr == ptr) {
                const unsigned long long serial = f->serial;
                report("block #%llu at %p freed twice", serial, ptr);
            }
        }
        report("%p was not handed out by this allocator, or was freed long ago", ptr);
    }

    if (entry.layout.size != layout.size || entry.layout.align != layout.align) {
        report(
            "block #%llu at %p made with %zu bytes at %zu, %s with %zu at %zu",
            (unsigned long long) entry.serial, ptr, entry.layout.size, entry.layout.align,
            as, layout.size, layout.align
        );
    }

    return entry;
}

static void report(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fputs("tda debug: ", stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);

    abort();
}

static uint64_t next_serial(DebugCtx *debug_ctx) {
    const uint64_t serial = ++debug_ctx->requests;

    if (serial == debug_ctx->break_at) {
        report("break at block #%llu", (unsigned long long) serial);
    }
    if (serial > debug_ctx->fail_after) {
        return 0;
    }

    return serial;
}

static Freed *quarantined(DebugCtx *debug_ctx, size_t i) {
    assert(i < debug_ctx->quarantine_len);

    return &debug_ctx->quarantine[(debug_ctx->quarantine_head + i) % QUARANTINE_LEN];
}

static void release_oldest(DebugCtx *debug_ctx) {
    assert(debug_ctx->quarantine_len > 0);

    const Freed f = *quarantined(debug_ctx, 0);
    debug_ctx->quarantine_head = (debug_ctx->quarantine_head + 1) % QUARANTINE_LEN;
    --debug_ctx->quarantine_len;

    const tda_Layout with_guard = parent_layout(f.layout);
    if (!all_equal(f.ptr, with_guard.size, FILL_FREED)) {
        report(
            "block #%llu at %p (%zu bytes) was written after it was freed",
            (unsigned long long) f.serial, f.ptr, f.layout.size
        );
    }

    tda_dealloc(debug_ctx->parent, f.ptr, with_guard);
}

static bool all_equal(const unsigned char *ptr, size_t n, unsigned char fill) {
    for (size_t i = 0; i < n; ++i) {
        if (ptr[i] != fill) {
            return false;
        }
    }

    return true;
}

static bool ckd_parent_layout(tda_Layout *out, tda_Layout layout) {
    out->align = layout.align;

    return ckd_add(&out->size, layout.size, GUARD_SIZE);
}

static tda_Layout parent_layout(tda_Layout layout) {
    tda_Layout out;
    [[maybe_unused]] const bool overflow = ckd_parent_layout(&out, layout);
    assert(!overflow);

    return out;
}

static size_t table_home(const Table *table, uintptr_t addr) {
    assert(table->cap > 0);

    // the low bits of a block address are zeros of its alignment; the mixer spreads the
    // rest over all of them
    const uint64_t key = addr;

    return (size_t) tda_hash_u64(&key, nullptr) & (table->cap - 1);
}

static size_t table_probe(const Table *table, uintptr_t addr) {
    size_t i = table_home(table, addr);
    while (table->slots[i].addr != 0 && table->slots[i].addr != addr) {
        i = (i + 1) & (table->cap - 1);
    }

    return i;
}

static bool table_insert(Table *table, Entry entry) {
    assert(entry.addr != 0);

    if (2 * (table->len + 1) > table->cap) {
        const size_t new_cap = table->cap ? 2 * table->cap : TABLE_MIN_CAP;
        Entry *slots = TDA_ALLOC_ZEROED(Entry, tda_al_default(), new_cap);
        if (!slots) {
            return false;
        }

        const Table old = *table;
        *table = (Table){.slots = slots, .cap = new_cap, .len = old.len};
        for (size_t i = 0; i < old.cap; ++i) {
            if (old.slots[i].addr != 0) {
                table->slots[table_probe(table, old.slots[i].addr)] = old.slots[i];
            }
        }
        TDA_DEALLOC(Entry, tda_al_default(), old.slots, old.cap);
    }

    const size_t i = table_probe(table, entry.addr);
    assert(table->slots[i].addr == 0); // the parent handed out a live block twice

    table->slots[i] = entry;
    ++table->len;

    return true;
}

static bool table_take(Table *table, uintptr_t addr, Entry *out) {
    if (table->cap == 0) {
        return false;
    }

    size_t hole = table_probe(table, addr);
    if (table->slots[hole].addr == 0) {
        return false;
    }

    *out = table->slots[hole];
    table->slots[hole] = (Entry){};
    --table->len;

    // backward shift: pull later entries of the run into the hole when their home slot
    // lies at or before it, so a probe never stops early at the hole
    const size_t mask = table->cap - 1;
    for (size_t i = (hole + 1) & mask; table->slots[i].addr != 0; i = (i + 1) & mask) {
        const size_t home = table_home(table, table->slots[i].addr);
        // distance from home to i against distance from hole to i, both around the ring
        if (((i - home) & mask) >= ((i - hole) & mask)) {
            table->slots[hole] = table->slots[i];
            table->slots[i] = (Entry){};
            hole = i;
        }
    }

    return true;
}
