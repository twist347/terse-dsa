#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/export.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/// @file

/// @defgroup alloc_debug alloc/debug
/// @ingroup alloc
/// @brief an allocator that checks how another one is used
///
/// For builds without sanitizers. Every block is recorded with its size and its serial,
/// the number of the request that made it, and every misuse stops the program with a
/// line on stderr:
///
/// - a dealloc or realloc with a size or an alignment other than the block was made with;
/// - a pointer it never handed out, or one already given back;
/// - a write past the end of a block, seen when the block is given back;
/// - a write into a block after it was given back;
/// - a parent that hands out a block it already handed out and has not had back.
///
/// A new block is filled with 0xCD and a freed one with 0xDD, so a read of either shows
/// up as a pattern rather than as plausible data. A freed block does not go back to the
/// parent at once: it waits in a quarantine of the last freed blocks, and its fill is
/// checked when it leaves. A realloc always moves the block, so a pointer kept across a
/// growth always points at freed memory.
///
/// A quarantined block still belongs to the parent. A reset or a rewind of the parent —
/// an arena's or a pool's — takes it back and hands it out again, and the quarantine
/// would then report the new owner's writes; tda_debug_flush_quarantine first.
///
/// There are no stack traces. A report names the serial instead; tda_debug_break_at
/// with that serial stops the program when the block is made, which a debugger turns into
/// a stack.
///
/// The record lives in a table taken from tda_al_default, not from the parent, so an
/// arena or a pool underneath sees only the caller's blocks, each 16 bytes longer for the
/// guard past its end; a pool sized for exactly the caller's blocks therefore serves
/// none of them. The parent must outlive the allocator.
///
/// @par Example
/// @snippet alloc/example_debug.c leaks
/// @snippet alloc/example_debug.c oom
/// @{

/// @cond
// the allocator's own records, laid out here only so the struct below has a size

typedef struct {
    uintptr_t addr; // 0 marks an empty slot; an allocator never hands out null
    tda_Layout layout;
    uint64_t serial;
} tda_DebugEntry_;

typedef struct {
    tda_DebugEntry_ *slots;
    size_t cap;
    size_t len;
} tda_DebugTable_;

typedef struct {
    void *ptr;
    tda_Layout layout;
    uint64_t serial;
} tda_DebugFreed_;

// how many freed blocks wait before going back; the source checks it against its own
enum { TDA_DEBUG_QUARANTINE_ = 64 };
/// @endcond

/// A debug allocator. A value the caller holds, on the stack or inside a struct:
/// tda_debug_init makes it, tda_debug_to_al hands out views of it, and tda_debug_deinit
/// releases what it holds. `= {}` is the inert state. It holds the quarantine in place,
/// a few kilobytes.
///
/// The fields belong to the allocator: read and change them only through the functions
/// here.
///
/// @warning once a view from tda_debug_to_al is out, the allocator must not move nor be
///          copied: the view points at it, and so does every container built on it.
///          While no view is out, a copy is a second owner of its table, as a copy of a
///          container is.
typedef struct tda_Debug {
    tda_Al parent_;          ///< where the blocks come from
    tda_DebugTable_ table_;  ///< the live blocks
    /// a ring of the last freed blocks, quarantine_len_ of them oldest first from
    /// quarantine_head_
    tda_DebugFreed_ quarantine_[TDA_DEBUG_QUARANTINE_];
    size_t quarantine_head_; ///< where the oldest one is
    size_t quarantine_len_;  ///< how many wait
    size_t live_bytes_;      ///< what the live blocks were asked for
    size_t peak_bytes_;      ///< the most live_bytes_ has been
    uint64_t requests_;      ///< requests seen, refused ones included
    uint64_t fail_after_;    ///< requests let through before refusing starts
    uint64_t break_at_;      ///< the serial to stop at; 0 for none
} tda_Debug;

/// @name lifetime
/// @{

/// a debug allocator over 'parent'; takes nothing until the first request, so it
/// cannot fail
/// @param parent where the blocks come from; borrowed, and must outlive the allocator
/// @return the allocator
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Debug tda_debug_init(tda_Al parent);

/// gives the quarantined blocks back to the parent, checking each, frees the table and
/// leaves the allocator inert
/// @param self a debug allocator, or an inert one, for which this is a no-op
/// @warning reports nothing about blocks still live; call tda_debug_fprint_leaks
///          first. A block left live stays with the parent, 16 bytes longer than asked
/// @bigo{q + c}; q quarantined blocks, checked byte by byte, and the table capacity c
TDA_API
void tda_debug_deinit(tda_Debug *self);

/// @}

/// @name view
/// @{

/// a view of the allocator, to hand to a container or a wrapper
/// @param self the allocator; from now on it must stay where it is
/// @return the view, valid while the allocator stays where it is and until its deinit
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al tda_debug_to_al(tda_Debug *self);

/// @}

/// @name quarantine
/// @{

/// gives every quarantined block back to the parent now, checking each one's fill as
/// it leaves
/// @param self the debug allocator
/// @note call it before a reset or a rewind of the parent, which would otherwise take
///       back blocks the quarantine still holds
/// @bigo{q}, the bytes of the quarantined blocks
TDA_API
void tda_debug_flush_quarantine(tda_Debug *self);

/// @}

/// @name stats
/// @{

/// What a debug allocator has seen.
typedef struct {
    size_t live_blocks; ///< blocks handed out and not yet given back
    size_t live_bytes;  ///< their sizes, as asked
    size_t peak_bytes;  ///< the most live_bytes has been
    uint64_t requests;  ///< requests so far, failed ones included: the last serial
} tda_DebugStats;

/// what the allocator has seen
/// @param self the allocator
/// @return what it has seen
/// @bigo{1}
[[nodiscard]] TDA_API
tda_DebugStats tda_debug_stats(const tda_Debug *self);

/// writes one line per live block, with its serial and size, and a total
/// @param self the allocator
/// @param stream where the lines go
/// @return negative if a write failed, as fprintf; 0 otherwise. How many blocks are
///         live is tda_debug_stats's live_blocks
/// @bigo{c} in the table capacity
TDA_API
int tda_debug_fprint_leaks(const tda_Debug *self, FILE *stream);

/// @}

/// @name fault injection
/// @{

/// lets the next 'count' requests through and refuses every one after them, as if the
/// parent were out of memory
/// @param self the allocator
/// @param count how many to let through; UINT64_MAX turns refusing off
/// @note a request is an alloc or an alloc_zeroed, a realloc's among them; giving back
///       is never refused
/// @bigo{1}
TDA_API
void tda_debug_fail_after(tda_Debug *self, uint64_t count);

/// stops the program when the request with this serial arrives, before it is served
/// @param self the allocator
/// @param serial as a leak report or tda_DebugStats names it; 0 turns it off
/// @bigo{1}
TDA_API
void tda_debug_break_at(tda_Debug *self, uint64_t serial);

/// @}

/// @}
