// for @snippet

#include "tda/alloc/default.h"
#include "tda/core/print.h"
#include "tda/core/status.h"
#include "tda/ds/arr.h"

#include <stdio.h>

/// [propagate]
// a fallible op returns the status and writes its result through a trailing 'out'
[[nodiscard]]
static tda_Status two_arrs(tda_Al *al, tda_Arr *out) {
    tda_Arr a = {};
    tda_Status st = TDA_ARR_OF(int32_t, al, &a, 5, 3, 1);
    if (TDA_STATUS_IS_ERR(st)) {
        return st; // nothing is held yet, so return the error directly
    }

    tda_Arr b = {};
    st = tda_arr_copy(&a, &b);
    if (TDA_STATUS_IS_ERR(st)) {
        goto fail; // 'a' is held and C has no defer, so fail through a goto
    }
    tda_arr_deinit(&b);

    *out = a; // 'out' is written only on success
    return TDA_STATUS_OK;

fail:
    tda_arr_deinit(&a);

    return st;
}

/// [propagate]

int main() {
    /// [report]
    tda_Arr a = {};
    const tda_Status st = two_arrs(tda_al_default(), &a);

    printf("%s\n", tda_status_to_str(st)); // TDA_STATUS_OK
    if (TDA_STATUS_IS_ERR(st)) {
        return 1;
    }

    tda_arr_print(&a, tda_fprint_i32); // [5, 3, 1]
    tda_arr_deinit(&a);
    /// [report]

    return 0;
}
