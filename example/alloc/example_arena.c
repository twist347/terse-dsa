// for @snippet

#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"

#include <stdint.h>
#include <stdio.h>

int main() {
    /// [build]
    // the block comes from the parent once, and is handed out in pieces from there on
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    if (!arena) {
        return 1;
    }

    int rc = 1;
    int32_t *a = TDA_ALLOC(int32_t, arena, 4);
    int32_t *b = TDA_ALLOC(int32_t, arena, 4);
    if (!a || !b) {
        goto drop;
    }

    const tda_AlArenaStats st = tda_al_arena_stats(arena);
    printf("%zu of %zu used, %zu left\n", st.used, st.cap,
           st.available); // 32 of 1024 used, 992 left — each piece is padded to the alignment
    /// [build]

    /// [reset]
    // giving one piece back does nothing at all: an arena has no per-block free
    TDA_DEALLOC(int32_t, arena, a, 4);
    printf("%zu used after a dealloc\n", tda_al_arena_stats(arena).used); // 32

    // this is how the memory comes back — all of it at once, and 'a' and 'b' are dead
    tda_al_arena_reset(arena);
    printf("%zu used after the reset\n", tda_al_arena_stats(arena).used); // 0
    /// [reset]

    rc = 0;
drop:
    tda_al_arena_drop(arena);

    /// [buf]
    // no parent and no heap: the arena, its header included, lives in the array
    unsigned char mem[256];
    tda_Al *local = tda_al_arena_from_buf(mem, sizeof mem);
    if (!local) {
        return 1;
    }

    int32_t *c = TDA_ALLOC(int32_t, local, 8);
    printf("%s, %zu left\n", c ? "taken from the array" : "no room",
           tda_al_arena_stats(local).available); // taken from the array, 144 left — the header took the front

    tda_al_arena_drop(local); // takes nothing back: the array was never borrowed from anyone
    /// [buf]

    return rc;
}
