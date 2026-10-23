#include "tda/alloc/debug.h"

#include "tda/alloc/alloc.h"
#include "tda/alloc/default.h"
#include "tda/core/check.h"
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

// the header sizes the quarantine without seeing this one
static_assert(QUARANTINE_LEN == TDA_DEBUG_QUARANTINE_);

static constexpr size_t TABLE_MIN_CAP = 64;

// the record of one live block; laid out in the header, so the struct has a size
typedef tda_DebugEntry_ BlockRecord;

// open addressing over the block addresses, linear probing, at most half full
typedef tda_DebugTable_ BlockTable;

// a block given back and held in the quarantine
typedef tda_DebugFreed_ FreedBlock;

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
static BlockRecord checked_entry(tda_Debug *self, const void *ptr, tda_Layout layout, bool take, const char *as);

/// writes "tda debug: ..." to stderr and aborts
[[noreturn]]
static void report(const char *fmt, ...);

/// the next serial, after the break and refusal checks; 0 means refuse the request
[[nodiscard]]
static uint64_t next_serial(tda_Debug *self);

/// the i-th quarantined block, oldest first
[[nodiscard]]
static FreedBlock *quarantined(tda_Debug *self, size_t i);

/// moves the oldest quarantined block back to the parent, checking its fill
static void release_oldest(tda_Debug *self);

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
static size_t table_home(const BlockTable *table, uintptr_t addr);

/// the slot where 'addr' is, or the empty slot where it would go
[[nodiscard]]
static size_t table_probe(const BlockTable *table, uintptr_t addr);

/// records a new block; false when the table could not grow
[[nodiscard]]
static bool table_insert(BlockTable *table, BlockRecord entry);

/// takes the entry for 'addr' out; false when there is none
[[nodiscard]]
static bool table_take(BlockTable *table, uintptr_t addr, BlockRecord *out);

// the ctx check is how a moved or copied allocator gets caught, so it stays under
// TDA_HARDENED
#define ASSERT_DEBUG(d)                     \
    (assert(d),                             \
     assert((d)->al_.alloc == debug_alloc), \
     TDA_EXPECT((d)->al_.ctx == (d)))

/* ========== lifetime ========== */

void tda_debug_init(tda_Al *parent, tda_Debug *out) {
    assert(parent);
    assert(out);

    // no resize, and a remap that only checks: every growth moves, through the wrapper's
    // alloc, copy and dealloc, so every block passes the checks
    *out = (tda_Debug){
        .al_ = {
            .ctx = out,
            .alloc = debug_alloc,
            .remap = debug_remap,
            .dealloc = debug_dealloc,
        },
        .parent_ = parent,
        .fail_after_ = SIZE_MAX,
    };
}

void tda_debug_deinit(tda_Debug *self) {
    assert(self);

    // the inert state, never filled in or already released
    if (!self->al_.alloc) {
        return;
    }

    ASSERT_DEBUG(self);

    tda_debug_flush_quarantine(self);

    TDA_DEALLOC(BlockRecord, tda_al_default(), self->table_.slots, self->table_.cap);

    *self = (tda_Debug){};
}

/* ========== quarantine ========== */

void tda_debug_flush_quarantine(tda_Debug *self) {
    ASSERT_DEBUG(self);

    while (self->quarantine_len_ > 0) {
        release_oldest(self);
    }
}

/* ========== stats ========== */

tda_DebugStats tda_debug_stats(const tda_Debug *self) {
    ASSERT_DEBUG(self);


    return (tda_DebugStats){
        .live_blocks = self->table_.len,
        .live_bytes = self->live_bytes_,
        .peak_bytes = self->peak_bytes_,
        .requests = self->requests_,
    };
}

size_t tda_debug_fprint_leaks(const tda_Debug *self, FILE *stream) {
    ASSERT_DEBUG(self);
    assert(stream);

    const BlockTable *table = &self->table_;

    for (size_t i = 0; i < table->cap; ++i) {
        const BlockRecord *entry = &table->slots[i];
        if (entry->addr != 0) {
            fprintf(
                stream,
                "tda debug: leak: block #%llu, %zu bytes at %p\n",
                (unsigned long long) entry->serial, entry->layout.size, (void *) entry->addr
            );
        }
    }
    fprintf(
        stream,
        "tda debug: %zu leaked, %zu bytes\n",
        table->len, self->live_bytes_
    );

    return table->len;
}

/* ========== fault injection ========== */

void tda_debug_fail_after(tda_Debug *self, size_t count) {
    ASSERT_DEBUG(self);


    size_t at;
    self->fail_after_ = ckd_add(&at, (size_t) self->requests_, count) ? SIZE_MAX : at;
}

void tda_debug_break_at(tda_Debug *self, uint64_t serial) {
    ASSERT_DEBUG(self);

    self->break_at_ = serial;
}

/* ========== internals ========== */

static void *debug_alloc(void *ctx, tda_Layout layout) {
    assert(ctx);
    assert(layout.size > 0); // tda_alloc answers a request for nothing itself

    tda_Debug *self = ctx;

    const uint64_t serial = next_serial(self);
    if (serial == 0) {
        return nullptr;
    }

    tda_Layout with_guard;
    if (ckd_parent_layout(&with_guard, layout)) {
        return nullptr;
    }

    unsigned char *block = tda_alloc(self->parent_, with_guard);
    if (!block) {
        return nullptr;
    }

    const BlockRecord entry = {.addr = (uintptr_t) block, .layout = layout, .serial = serial};
    if (!table_insert(&self->table_, entry)) {
        tda_dealloc(self->parent_, block, with_guard);
        return nullptr;
    }

    memset(block, FILL_NEW, layout.size);
    memset(block + layout.size, FILL_GUARD, GUARD_SIZE);

    self->live_bytes_ += layout.size;
    if (self->live_bytes_ > self->peak_bytes_) {
        self->peak_bytes_ = self->live_bytes_;
    }

    return block;
}

static void *debug_remap(void *ctx, void *ptr, tda_Layout old, size_t new_size) {
    assert(ctx);
    TDA_UNUSED(new_size);

    [[maybe_unused]] const BlockRecord entry = checked_entry(ctx, ptr, old, false, "resized");

    return nullptr;
}

static void debug_dealloc(void *ctx, void *ptr, tda_Layout layout) {
    assert(ctx);
    assert(ptr); // tda_dealloc takes null itself

    tda_Debug *self = ctx;

    const BlockRecord entry = checked_entry(self, ptr, layout, true, "given back");

    unsigned char *block = ptr;
    if (!all_equal(block + layout.size, GUARD_SIZE, FILL_GUARD)) {
        report(
            "block #%llu at %p (%zu bytes) was written past its end",
            (unsigned long long) entry.serial, ptr, layout.size
        );
    }

    self->live_bytes_ -= layout.size;
    memset(block, FILL_FREED, parent_layout(layout).size);

    if (self->quarantine_len_ == QUARANTINE_LEN) {
        release_oldest(self);
    }
    ++self->quarantine_len_;
    *quarantined(self, self->quarantine_len_ - 1) =
            (FreedBlock){.ptr = ptr, .layout = layout, .serial = entry.serial};
}

static BlockRecord checked_entry(tda_Debug *self, const void *ptr, tda_Layout layout, bool take, const char *as) {
    BlockTable *table = &self->table_;
    const uintptr_t addr = (uintptr_t) ptr;

    BlockRecord entry;
    const bool found = take
        ? table_take(table, addr, &entry)
        : table->cap > 0 && (entry = table->slots[table_probe(table, addr)]).addr != 0;
    if (!found) {
        for (size_t i = 0; i < self->quarantine_len_; ++i) {
            const FreedBlock *freed = quarantined(self, i);
            if (freed->ptr == ptr) {
                const unsigned long long serial = freed->serial;
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

static uint64_t next_serial(tda_Debug *self) {
    const uint64_t serial = ++self->requests_;

    if (serial == self->break_at_) {
        report("break at block #%llu", (unsigned long long) serial);
    }
    if (serial > self->fail_after_) {
        return 0;
    }

    return serial;
}

static FreedBlock *quarantined(tda_Debug *self, size_t i) {
    assert(i < self->quarantine_len_);

    return &self->quarantine_[(self->quarantine_head_ + i) % QUARANTINE_LEN];
}

static void release_oldest(tda_Debug *self) {
    assert(self->quarantine_len_ > 0);

    const FreedBlock freed = *quarantined(self, 0);
    self->quarantine_head_ = (self->quarantine_head_ + 1) % QUARANTINE_LEN;
    --self->quarantine_len_;

    const tda_Layout with_guard = parent_layout(freed.layout);
    if (!all_equal(freed.ptr, with_guard.size, FILL_FREED)) {
        report(
            "block #%llu at %p (%zu bytes) was written after it was freed",
            (unsigned long long) freed.serial, freed.ptr, freed.layout.size
        );
    }

    tda_dealloc(self->parent_, freed.ptr, with_guard);
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

static size_t table_home(const BlockTable *table, uintptr_t addr) {
    assert(table->cap > 0);

    // the low bits of a block address are zeros of its alignment; the mixer spreads the
    // rest over all of them
    const uint64_t key = addr;

    return (size_t) tda_hash_u64(&key, nullptr) & (table->cap - 1);
}

static size_t table_probe(const BlockTable *table, uintptr_t addr) {
    size_t i = table_home(table, addr);
    while (table->slots[i].addr != 0 && table->slots[i].addr != addr) {
        i = (i + 1) & (table->cap - 1);
    }

    return i;
}

static bool table_insert(BlockTable *table, BlockRecord entry) {
    assert(entry.addr != 0);

    if (2 * (table->len + 1) > table->cap) {
        const size_t new_cap = table->cap ? 2 * table->cap : TABLE_MIN_CAP;
        BlockRecord *slots = TDA_ALLOC_ZEROED(BlockRecord, tda_al_default(), new_cap);
        if (!slots) {
            return false;
        }

        const BlockTable old = *table;
        *table = (BlockTable){.slots = slots, .cap = new_cap, .len = old.len};
        for (size_t i = 0; i < old.cap; ++i) {
            if (old.slots[i].addr != 0) {
                table->slots[table_probe(table, old.slots[i].addr)] = old.slots[i];
            }
        }
        TDA_DEALLOC(BlockRecord, tda_al_default(), old.slots, old.cap);
    }

    const size_t i = table_probe(table, entry.addr);
    assert(table->slots[i].addr == 0); // the parent handed out a live block twice

    table->slots[i] = entry;
    ++table->len;

    return true;
}

static bool table_take(BlockTable *table, uintptr_t addr, BlockRecord *out) {
    if (table->cap == 0) {
        return false;
    }

    size_t hole = table_probe(table, addr);
    if (table->slots[hole].addr == 0) {
        return false;
    }

    *out = table->slots[hole];
    table->slots[hole] = (BlockRecord){};
    --table->len;

    // backward shift: pull later entries of the run into the hole when their home slot
    // lies at or before it, so a probe never stops early at the hole
    const size_t mask = table->cap - 1;
    for (size_t i = (hole + 1) & mask; table->slots[i].addr != 0; i = (i + 1) & mask) {
        const size_t home = table_home(table, table->slots[i].addr);
        // distance from home to i against distance from hole to i, both around the ring
        if (((i - home) & mask) >= ((i - hole) & mask)) {
            table->slots[hole] = table->slots[i];
            table->slots[i] = (BlockRecord){};
            hole = i;
        }
    }

    return true;
}
