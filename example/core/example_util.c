// for @snippet

#include "tda/core/status.h"
#include "tda/core/util.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/// [unused]
static void on_each(int32_t val, void *ctx) {
    TDA_UNUSED(ctx);

    printf("%" PRId32 "\n", val);
}

/// [unused]

int main() {
    /// [swap]
    int32_t a = 1;
    int32_t b = 2;

    TDA_SWAP(a, b);
    printf("%" PRId32 " %" PRId32 "\n", a, b); // 2 1

    // both operands are addressed before anything moves, so an argument with a side
    // effect is evaluated once
    int32_t v[] = {10, 20, 30, 40};
    size_t i = 0;
    size_t j = 3;

    TDA_SWAP(v[i++], v[j--]);
    printf("%" PRId32 " %" PRId32 " %" PRId32 " %" PRId32 ", i=%zu j=%zu\n",
           v[0], v[1], v[2], v[3], i, j); // 40 20 30 10, i=1 j=2

    // through pointers, name the pointees: TDA_SWAP(pa, pb) also typechecks, but swaps the
    // two local pointers and leaves 'a' and 'b' alone
    int32_t *pa = &a;
    int32_t *pb = &b;

    TDA_SWAP(*pa, *pb);
    printf("%" PRId32 " %" PRId32 "\n", a, b); // 1 2 — the pointees went back
    /// [swap]

    /// [stringify]
    // the argument is spelled as it expands, not as it was written
    printf("%s\n", TDA_STRINGIFY(TDA_STATUS_OK)); // TDA_STATUS_OK
    /// [stringify]

    on_each(7, nullptr);

    return 0;
}
