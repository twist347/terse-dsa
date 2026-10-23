// for @snippet

#include "tda/algo/search.h"
#include "tda/algo/sort.h"
#include "tda/core/cmp.h"
#include "tda/core/print.h"
#include "tda/core/span.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/// [struct]
// a struct comparator delegates to the one for its field, by address; comparators
// only take pointers, so there is no by-value form
typedef struct {
    const char *name;
    int32_t age;
} Person;

static int cmp_person_by_age(const void *lhs, const void *rhs, void *) {
    const Person *a = lhs;
    const Person *b = rhs;

    return tda_cmp_i32(&a->age, &b->age, nullptr);
}

static void fprint_person(FILE *stream, const void *val) {
    const Person *p = val;

    fprintf(stream, "%s(%" PRId32 ")", p->name, p->age);
}
/// [struct]

int main() {
    /// [ready]
    const tda_SpanMut nums = TDA_SPAN_OF_MUT(int32_t, 5, 3, 1, 4, 2);

    tda_span_sort(nums, tda_cmp_i32, nullptr);
    tda_span_mut_print(nums, tda_fprint_i32); // [1, 2, 3, 4, 5]

    tda_span_sort(nums, tda_cmp_desc_i32, nullptr);
    tda_span_mut_print(nums, tda_fprint_i32); // [5, 4, 3, 2, 1]

    // equality is the other half: it only says whether two match, and it is what the
    // hash containers and searches take
    size_t idx;
    if (tda_span_find(tda_span_mut_to_span(nums), &(int32_t){4}, tda_eq_i32, nullptr, &idx)) {
        printf("4 is at %zu\n", idx); // 4 is at 1
    }
    /// [ready]

    /// [use]
    const tda_SpanMut people = TDA_SPAN_OF_MUT(Person, { "ann", 31 }, { "bo", 4 }, { "cy", 17 });

    tda_span_sort(people, cmp_person_by_age, nullptr);
    tda_span_mut_print(people, fprint_person); // [bo(4), cy(17), ann(31)]
    /// [use]

    return 0;
}
