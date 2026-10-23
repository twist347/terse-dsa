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

static void fprint_person(FILE *stream, const void *val) {
    const Person *p = val;

    fprintf(stream, "%s(%" PRId32 ")", p->name, p->age);
}
/// [custom]

int main() {
    /// [ready]
    const tda_Span nums = TDA_SPAN_OF(int32_t, 5, 3, 1);
    tda_span_print(nums, tda_fprint_i32); // [5, 3, 1]

    // cstr reads a pointer to a pointer, quotes the string and prints a null one as null.
    // It also needs its own type name: the macro writes 'const T', and
    // 'const const char *' is not a type
    typedef const char *Cstr;

    const tda_Span names = TDA_SPAN_OF(Cstr, "ann", nullptr, "bo, jr");
    tda_span_print(names, tda_fprint_cstr); // ["ann", null, "bo, jr"]
    /// [ready]

    /// [use]
    const tda_Span people = TDA_SPAN_OF(Person, { "ann", 31 }, { "bo", 4 });
    tda_span_print(people, fprint_person); // [ann(31), bo(4)]
    /// [use]

    return 0;
}
