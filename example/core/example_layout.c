// for @snippet

#include "tda/alloc/default.h"
#include "tda/core/layout.h"
#include "tda/ds/vec.h"

#include <stdint.h>
#include <stdio.h>

/// [type_def]
// eight floats an AVX load wants on a 32-byte boundary, past anything malloc promises
typedef struct {
    alignas(32) float lane[8];
} Vec8;
/// [type_def]

int main() {
    /// [type]
    const tda_Layout l = TDA_LAYOUT_OF(Vec8);
    printf("%zu bytes at %zu\n", l.size, l.align); // 32 bytes at 32

    // the typed macros pass that layout, so every elem lands on its boundary, through
    // every growth, on any allocator
    tda_Vec *v = nullptr;
    if (TDA_STATUS_IS_ERR(TDA_VEC_NEW(Vec8, tda_al_default(), &v))) {
        return 1;
    }
    for (int i = 0; i < 100; ++i) {
        if (TDA_STATUS_IS_ERR(tda_vec_push(v, &(Vec8){{(float) i}}))) {
            tda_vec_drop(v);
            return 1;
        }
    }
    printf("%s\n", (uintptr_t) tda_vec_data(v) % 32 == 0 ? "aligned" : "not aligned"); // aligned
    tda_vec_drop(v);
    /// [type]

    /// [build]
    // a header and a payload in one block, laid out field by field as a C struct would
    // be: each field at the first offset its alignment allows, the whole padded at the end
    tda_Layout block = TDA_LAYOUT_OF(uint32_t);
    size_t payload_offset;
    if (tda_layout_ckd_extend(&block, TDA_LAYOUT_OF(Vec8), &payload_offset) || tda_layout_ckd_pad(&block)) {
        return 1; // the sizes overflowed size_t
    }
    printf("payload at %zu, %zu bytes at %zu\n", payload_offset, block.size, block.align);
    // payload at 32, 64 bytes at 32
    /// [build]

    return 0;
}
