// One allocator per run, picked by argv[1]: a view is taken, the allocator is released,
// and the stale view is used. The hook's liveness check has to abort on it; as in
// test_debug_misuse.c the SIGABRT handler turns the abort into a clean exit, and the test
// passes on the failed check it expects. Under NDEBUG the checks are compiled out, so
// the run reports a skip.

#include "tda/alloc/arena.h"
#include "tda/alloc/debug.h"
#include "tda/alloc/default.h"
#include "tda/alloc/log.h"
#include "tda/alloc/pool.h"
#include "tda/alloc/slab.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_abort(int) {
    _Exit(0);
}

static constexpr tda_Layout BYTES = {.size = 8, .align = 8};

static void stale_arena() {
    tda_Arena arena;
    if (TDA_STATUS_IS_ERR(tda_arena_init_cap(64, tda_al_default(), &arena))) {
        exit(1);
    }
    const tda_Al al = tda_arena_to_al(&arena);
    tda_arena_deinit(&arena);
    (void) tda_alloc(al, BYTES);
}

static void stale_pool() {
    tda_Pool pool;
    if (TDA_STATUS_IS_ERR(tda_pool_init_cap(4, BYTES, tda_al_default(), &pool))) {
        exit(1);
    }
    const tda_Al al = tda_pool_to_al(&pool);
    tda_pool_deinit(&pool);
    (void) tda_alloc(al, BYTES);
}

static void stale_slab() {
    tda_Slab slab = tda_slab_init(tda_al_default());
    const tda_Al al = tda_slab_to_al(&slab);
    tda_slab_deinit(&slab);
    (void) tda_alloc(al, BYTES);
}

static void stale_debug() {
    tda_Debug debug = tda_debug_init(tda_al_default());
    const tda_Al al = tda_debug_to_al(&debug);
    tda_debug_deinit(&debug);
    (void) tda_alloc(al, BYTES);
}

static void stale_log() {
    tda_Log log = tda_log_init(stdout, tda_al_default());
    const tda_Al al = tda_log_to_al(&log);
    tda_log_deinit(&log);
    (void) tda_alloc(al, BYTES);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }

#ifdef NDEBUG
    return 77; // the skip code test/alloc/CMakeLists.txt names
#endif

    static const struct {
        const char *name;
        void (*run)();
    } cases[] = {
        {"arena", stale_arena},
        {"pool", stale_pool},
        {"slab", stale_slab},
        {"debug", stale_debug},
        {"log", stale_log},
    };

    signal(SIGABRT, on_abort);

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (strcmp(argv[1], cases[i].name) == 0) {
            cases[i].run();
            fprintf(stderr, "the stale view went through unnoticed\n");
            return 1;
        }
    }

    return 2;
}
