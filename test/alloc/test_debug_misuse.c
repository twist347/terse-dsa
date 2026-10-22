// One misuse per run, picked by argv[1]. The debug allocator aborts on it; the SIGABRT
// handler turns that into a clean exit, since CTest fails a process killed by a signal
// even when the output matches. The test passes on the report line it expects, which
// test/alloc/CMakeLists.txt names per case.

#include "tda/alloc/debug.h"
#include "tda/alloc/default.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_abort(int sig) {
    (void) sig;
    _Exit(0);
}

static void wrong_size(tda_Al *al) {
    void *p = tda_alloc(al, 32);
    tda_dealloc(al, p, 24);
}

static void double_free(tda_Al *al) {
    void *p = tda_alloc(al, 32);
    tda_dealloc(al, p, 32);
    tda_dealloc(al, p, 32);
}

static void foreign_pointer(tda_Al *al) {
    static unsigned char not_ours[32];
    tda_dealloc(al, not_ours, sizeof(not_ours));
}

static void write_past_end(tda_Al *al) {
    unsigned char *p = tda_alloc(al, 32);
    p[32] = 0; // the first guard byte, still the parent's memory
    tda_dealloc(al, p, 32);
}

static void write_after_free(tda_Al *al) {
    unsigned char *p = tda_alloc(al, 32);
    tda_dealloc(al, p, 32);
    p[5] = 0; // quarantined, so the parent has not got it back yet
    tda_al_debug_drop(al);
}

static void stale_realloc(tda_Al *al) {
    unsigned char *p = tda_alloc(al, 8);
    unsigned char *q = tda_realloc(al, p, 8, 16);
    p[0] = 1; // the old block, kept across a growth
    tda_dealloc(al, q, 16);
    tda_al_debug_drop(al);
}

static void break_at(tda_Al *al) {
    tda_al_debug_break_at(al, 2);
    void *p = tda_alloc(al, 8);
    void *q = tda_alloc(al, 8);
    (void) p;
    (void) q;
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
        {"double_free", double_free},
        {"foreign_pointer", foreign_pointer},
        {"write_past_end", write_past_end},
        {"write_after_free", write_after_free},
        {"stale_realloc", stale_realloc},
        {"break_at", break_at},
    };

    tda_Al *al = tda_al_debug_new(tda_al_default());
    if (!al) {
        return 2;
    }

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
