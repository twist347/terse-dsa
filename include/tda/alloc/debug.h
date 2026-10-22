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
/// - a dealloc or realloc with a size other than the one the block was made with;
/// - a pointer it never handed out, or one already given back;
/// - a write past the end of a block, seen when the block is given back;
/// - a write into a block after it was given back.
///
/// A new block is filled with 0xCD and a freed one with 0xDD, so a read of either shows
/// up as a pattern rather than as plausible data. A freed block does not go back to the
/// parent at once: it waits in a quarantine of the last freed blocks, and its fill is
/// checked when it leaves. A realloc always moves the block, so a pointer kept across a
/// growth always points at freed memory.
///
/// There are no stack traces. A report names the serial instead; tda_al_debug_break_at
/// with that serial stops the program when the block is made, which a debugger turns into
/// a stack.
///
/// The record lives in a table taken from tda_al_default, not from the parent, so an
/// arena or a pool underneath sees only the caller's blocks, each 16 bytes longer for the
/// guard past its end. The allocator's own two blocks come from the parent, which must
/// outlive it.
///
/// @par Example
/// @snippet alloc/example_debug.c leaks
/// @snippet alloc/example_debug.c oom
/// @{

/// @name lifetime
/// @{

/// a debug allocator over 'parent'
/// @param parent where the blocks come from; borrowed
/// @return the allocator, or null if 'parent' could not give its two blocks
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_al_debug_new(tda_Al *parent);

/// gives the quarantined blocks back to the parent, checking each, then its own blocks
/// @param self the allocator; null is a no-op
/// @warning reports nothing about blocks still live; call tda_al_debug_fprint_leaks
///          first. A block left live stays with the parent, 16 bytes longer than asked
/// @bigo{q + c}; q quarantined blocks, checked byte by byte, and the table capacity c
TDA_API
void tda_al_debug_drop(tda_Al *self);

/// @}

/// @name stats
/// @{

/// What a debug allocator has seen.
typedef struct {
    size_t live_blocks;  ///< blocks handed out and not yet given back
    size_t live_bytes;   ///< their sizes, as asked
    size_t peak_bytes;   ///< the most live_bytes has been
    uint64_t requests;   ///< requests so far, failed ones included: the last serial
} tda_AlDebugStats;

/// what the allocator has seen
/// @param self the allocator
/// @return the four numbers
/// @bigo{1}
[[nodiscard]] TDA_API
tda_AlDebugStats tda_al_debug_stats(const tda_Al *self);

/// writes one line per live block, with its serial and size, and a total
/// @param self the allocator
/// @param stream where the lines go
/// @return how many blocks are live; 0 is a clean run
/// @bigo{c} in the table capacity
TDA_API
size_t tda_al_debug_fprint_leaks(const tda_Al *self, FILE *stream);

/// @}

/// @name fault injection
/// @{

/// lets the next 'n' requests through and refuses every one after them, as if the
/// parent were out of memory
/// @param self the allocator
/// @param n how many to let through; SIZE_MAX turns refusing off
/// @note a request is an alloc, a calloc or a realloc to a non-zero size; giving back
///       is never refused
/// @bigo{1}
TDA_API
void tda_al_debug_fail_after(tda_Al *self, size_t n);

/// stops the program when the request with this serial arrives, before it is served
/// @param self the allocator
/// @param serial as a leak report or tda_AlDebugStats names it; 0 turns it off
/// @bigo{1}
TDA_API
void tda_al_debug_break_at(tda_Al *self, uint64_t serial);

/// @}

/// @}
