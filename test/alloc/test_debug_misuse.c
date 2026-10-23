// One misuse per run, picked by argv[1]. The debug allocator aborts on it; the SIGABRT
// handler turns that into a clean exit, since CTest fails a process killed by a signal
// even when the output matches. The test passes on the report line it expects, which
// test/alloc/CMakeLists.txt names per case.

#include "tda/alloc/debug.h"
#include "tda/alloc/default.h"
#include "tda/core/util.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_abort(int) {
    _Exit(0);
}

static tda_Layout bytes(size_t size) {
    return (tda_Layout){.size = size, .align = 8};
}

// the allocator every case runs on; a case that needs its own ops reaches it here
static tda_Debug debug;

static void wrong_size(tda_Al *al) {
    void *p = tda_alloc(al, bytes(32));
    tda_dealloc(al, p, bytes(24));
}

static void wrong_align(tda_Al *al) {
    void *p = tda_alloc(al, (tda_Layout){.size = 32, .align = 64});
    tda_dealloc(al, p, bytes(32));
}

// the old size is what the copy reads, so it is checked before the copy
static void realloc_wrong_size(tda_Al *al) {
    void *p = tda_alloc(al, bytes(8));
    TDA_UNUSED(tda_realloc(al, p, bytes(64), 128));
}

static void realloc_wrong_align(tda_Al *al) {
    void *p = tda_alloc(al, (tda_Layout){.size = 64, .align = 64});
    TDA_UNUSED(tda_realloc(al, p, bytes(64), 128));
}

static void double_free(tda_Al *al) {
    void *p = tda_alloc(al, bytes(32));
    tda_dealloc(al, p, bytes(32));
    tda_dealloc(al, p, bytes(32));
}

static void foreign_pointer(tda_Al *al) {
    static unsigned char not_ours[32];
    tda_dealloc(al, not_ours, bytes(sizeof(not_ours)));
}

static void write_past_end(tda_Al *al) {
    unsigned char *p = tda_alloc(al, bytes(32));
    p[32] = 0; // the first guard byte, still the parent's memory
    tda_dealloc(al, p, bytes(32));
}

static void write_after_free(tda_Al *al) {
    unsigned char *p = tda_alloc(al, bytes(32));
    tda_dealloc(al, p, bytes(32));
    p[5] = 0; // quarantined, so the parent has not got it back yet
    tda_debug_deinit(&debug);
}

static void stale_realloc(tda_Al *al) {
    unsigned char *p = tda_alloc(al, bytes(8));
    unsigned char *q = tda_realloc(al, p, bytes(8), 16);
    p[0] = 1; // the old block, kept across a growth
    tda_dealloc(al, q, bytes(16));
    tda_debug_deinit(&debug);
}

static void break_at(tda_Al *al) {
    tda_debug_break_at(&debug, 2);
    TDA_UNUSED(tda_alloc(al, bytes(8)));
    TDA_UNUSED(tda_alloc(al, bytes(8)));
}

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }

    static const struct {
        const char *name;
        void (*run)(tda_Al *);
    } cases[] = {
        {"wrong_size", wrong_size},
        {"wrong_align", wrong_align},
        {"realloc_wrong_size", realloc_wrong_size},
        {"realloc_wrong_align", realloc_wrong_align},
        {"double_free", double_free},
        {"foreign_pointer", foreign_pointer},
        {"write_past_end", write_past_end},
        {"write_after_free", write_after_free},
        {"stale_realloc", stale_realloc},
        {"break_at", break_at},
    };

    tda_debug_init(tda_al_default(), &debug);
    tda_Al *al = TDA_AL(&debug);

    signal(SIGABRT, on_abort);

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (strcmp(argv[1], cases[i].name) == 0) {
            cases[i].run(al);
            fprintf(stderr, "ran on past the misuse\n");
            return 1;
        }
    }

    return 2;
}
