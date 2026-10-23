// for @snippet

#include "tda/alloc/default.h"
#include "tda/core/print.h"
#include "tda/core/status.h"
#include "tda/ds/arr.h"

#include <stdio.h>

/// [propagate]
// a fallible op returns the status and writes its results through trailing outs, only
// on success: either both are built or neither is
[[nodiscard]]
static tda_Status arr_and_copy(tda_Al *al, tda_Arr *out_arr, tda_Arr *out_copy) {
    tda_Arr arr;
    tda_Status st = TDA_ARR_OF(int32_t, al, &arr, 5, 3, 1);
    if (TDA_STATUS_IS_ERR(st)) {
        return st; // nothing is held yet, so return the error directly
    }

    tda_Arr copy;
    st = tda_arr_copy(&arr, &copy);
    if (TDA_STATUS_IS_ERR(st)) {
        goto fail; // 'arr' is held and C has no defer, so fail through a goto
    }

    *out_arr = arr;
    *out_copy = copy;
    return TDA_STATUS_OK;

fail:
    tda_arr_deinit(&arr);

    return st;
}
/// [propagate]

int main() {
    /// [report]
    tda_Arr arr;
    tda_Arr copy;
    const tda_Status st = arr_and_copy(tda_al_default(), &arr, &copy);

    printf("%s\n", tda_status_to_str(st)); // TDA_STATUS_OK
    if (TDA_STATUS_IS_ERR(st)) {
        return 1;
    }

    tda_arr_print(&copy, tda_fprint_i32); // [5, 3, 1]
    tda_arr_deinit(&copy);
    tda_arr_deinit(&arr);
    /// [report]

    return 0;
}
