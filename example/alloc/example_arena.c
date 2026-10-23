// for @snippet

#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"

#include <stdint.h>
#include <stdio.h>

int main() {
    /// [build]
    // the block comes from the parent once and is handed out in pieces
    tda_Arena arena;
    if (TDA_STATUS_IS_ERR(tda_arena_init(tda_al_default(), 1024, &arena))) {
        return 1;
    }

    int rc = 1;
    int32_t *a = TDA_ALLOC(int32_t, TDA_AL(&arena), 4);
    int32_t *b = TDA_ALLOC(int32_t, TDA_AL(&arena), 4);
    if (!a || !b) {
        goto out;
    }

    const tda_ArenaStats st = tda_arena_stats(&arena);
    printf("%zu of %zu used, %zu left\n", st.used, st.cap,
           st.free); // 32 of 1024 used, 992 left
    /// [build]

    /// [reset]
    // a dealloc does nothing: an arena has no per-block free
    TDA_DEALLOC(int32_t, TDA_AL(&arena), a, 4);
    printf("%zu used after a dealloc\n", tda_arena_stats(&arena).used); // 32

    // this is how memory comes back: all at once, and 'a' and 'b' are now dead
    tda_arena_reset(&arena);
    printf("%zu used after the reset\n", tda_arena_stats(&arena).used); // 0
    /// [reset]

    /// [mark]
    // scratch inside a phase: allocate what the phase keeps, then take a mark; the rewind
    // frees only the scratch
    int32_t *kept = TDA_ALLOC(int32_t, TDA_AL(&arena), 4);
    const tda_ArenaMark mark = tda_arena_mark(&arena);

    int32_t *scratch = TDA_ALLOC(int32_t, TDA_AL(&arena), 64);
    if (!kept || !scratch) {
        goto out;
    }
    printf("%zu used with the scratch\n", tda_arena_stats(&arena).used); // 272

    tda_arena_rewind(&arena, mark); // 'scratch' is dead, 'kept' is not
    printf("%zu used after the rewind\n", tda_arena_stats(&arena).used); // 16
    /// [mark]

    rc = 0;
out:
    tda_arena_deinit(&arena);

    /// [buf]
    // no parent and no heap: the arena is a value on the stack, and the whole array is
    // its block
    unsigned char mem[256];
    tda_Arena local;
    tda_arena_init_buf(mem, sizeof(mem), &local);

    int32_t *c = TDA_ALLOC(int32_t, TDA_AL(&local), 8);
    printf(
        "%s, %zu left\n",
        c ? "taken from the array" : "no room",
        tda_arena_stats(&local).free
    ); // taken from the array, 224 left

    tda_arena_deinit(&local); // returns nothing: the array was not borrowed
    /// [buf]

    return rc;
}
