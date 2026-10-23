// One allocator per run, picked by argv[1]: it is built in place, copied out, and the
// copy is given to its own deinit, which has to abort on it. As in test_debug_misuse.c,
// the SIGABRT handler turns the abort into a clean exit and the test passes on the
// failed check it expects. With the checks compiled out, under NDEBUG without
// TDA_HARDENED, there is nothing to catch it, so the run reports a skip.

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

static void copied_arena() {
    tda_Arena arena;
    if (TDA_STATUS_IS_ERR(tda_arena_init(tda_al_default(), 64, &arena))) {
        exit(1);
    }
    tda_Arena copy = arena;
    tda_arena_deinit(&copy);
}

static void copied_pool() {
    tda_Pool pool;
    if (TDA_STATUS_IS_ERR(tda_pool_init(tda_al_default(), TDA_LAYOUT_OF(int), 4, &pool))) {
        exit(1);
    }
    tda_Pool copy = pool;
    tda_pool_deinit(&copy);
}

static void copied_slab() {
    tda_Slab slab;
    tda_slab_init(tda_al_default(), &slab);
    tda_Slab copy = slab;
    tda_slab_deinit(&copy);
}

static void copied_debug() {
    tda_Debug debug;
    tda_debug_init(tda_al_default(), &debug);
    tda_Debug copy = debug;
    tda_debug_deinit(&copy);
}

static void copied_log() {
    tda_Log log;
    tda_log_init(tda_al_default(), stdout, &log);
    tda_Log copy = log;
    tda_log_deinit(&copy);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }

#if defined(NDEBUG) && !defined(TDA_HARDENED)
    return 77; // the skip code test/alloc/CMakeLists.txt names
#endif

    static const struct {
        const char *name;
        void (*run)();
    } cases[] = {
        {"arena", copied_arena},
        {"pool", copied_pool},
        {"slab", copied_slab},
        {"debug", copied_debug},
        {"log", copied_log},
    };

    signal(SIGABRT, on_abort);

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (strcmp(argv[1], cases[i].name) == 0) {
            cases[i].run();
            fprintf(stderr, "the copy went through unnoticed\n");
            return 1;
        }
    }

    return 2;
}
