// for @snippet

#include "tda/core/print.h"
#include "tda/core/span.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/// [custom]
// a printer gets the elem by address and writes only the value; brackets and commas
// belong to whoever prints the container
typedef struct {
    const char *name;
    int32_t age;
} Person;

static int fprint_person(FILE *stream, const void *val, void *) {
    const Person *p = val;

    return fprintf(stream, "%s(%" PRId32 ")", p->name, p->age);
}
/// [custom]

/// [ctx]
// ctx tells a printer what the elem cannot: here how wide to pad each number
static int fprint_padded(FILE *stream, const void *val, void *ctx) {
    return fprintf(stream, "%*" PRId32, *(const int *) ctx, *(const int32_t *) val);
}

// a container's fprint writes no newline, so it prints an elem of another container;
// ctx carries the inner elems' printer
static int fprint_row(FILE *stream, const void *val, void *ctx) {
    return tda_span_fprint(*(const tda_Span *) val, stream, *(tda_FPrint *) ctx, nullptr);
}
/// [ctx]

int main() {
    /// [ready]
    const tda_Span nums = TDA_SPAN_OF(int32_t, 5, 3, 1);
    tda_span_print(nums, tda_fprint_i32, nullptr); // [5, 3, 1]

    // cstr reads a pointer to a pointer, quotes the string and prints a null one as null.
    // It also needs its own type name: the macro writes 'const T', and
    // 'const const char *' is not a type
    typedef const char *Cstr;

    const tda_Span names = TDA_SPAN_OF(Cstr, "ann", nullptr, "bo, jr");
    tda_span_print(names, tda_fprint_cstr, nullptr); // ["ann", null, "bo, jr"]
    /// [ready]

    /// [use]
    const tda_Span people = TDA_SPAN_OF(Person, { "ann", 31 }, { "bo", 4 });
    tda_span_print(people, fprint_person, nullptr); // [ann(31), bo(4)]

    tda_span_print(nums, fprint_padded, &(int){3}); // [  5,   3,   1]

    const tda_Span rows = TDA_SPAN_OF(tda_Span, nums, TDA_SPAN_OF(int32_t, 7));
    tda_span_print(rows, fprint_row, &(tda_FPrint){tda_fprint_i32}); // [[5, 3, 1], [7]]
    /// [use]

    return 0;
}
