#include "tda/algo/sort.h"

#include "tda/algo/copy.h"
#include "tda/algo/heap.h"
#include "tda/algo/merge.h"
#include "tda/core/check.h"
#include "tda/core/util.h"

#include <assert.h>
#include <stdbit.h>
#include <string.h>

/* ========== internals ========== */

// below this many elems insertion sort wins: no partitioning overhead, and the range
// is short enough that its quadratic cost does not show
static constexpr size_t INSERTION_THRESHOLD = 16;

// the largest elem insertion sort sets aside to slide the rest in one memmove; above it
// every step is a swap
static constexpr size_t INSERTION_HELD_MAX = 256;

[[nodiscard]]
static size_t median3(tda_Span s, size_t a, size_t b, size_t c, tda_Cmp cmp);

/// median of three medians, sampled across the whole range
[[nodiscard]]
static size_t ninther(tda_Span s, size_t left, size_t right, tda_Cmp cmp);

// the block of elems equal to the pivot after a three-way split, as the
// inclusive range [lt, gt]: everything below lt is smaller, above gt larger
typedef struct {
    size_t lt;
    size_t gt;
} Split;

/// splits [left, right] into < pivot | == pivot | > pivot and returns the
/// bounds of the middle run
[[nodiscard]]
static Split partition3(tda_SpanMut s, size_t left, size_t right, size_t pivot_idx, tda_Cmp cmp);

/// sorts the inclusive range [left, right]
static void quicksort(tda_SpanMut s, size_t left, size_t right, size_t depth, tda_Cmp cmp);

[[nodiscard]]
static size_t depth_limit(size_t len);

/// where the elem at 'idx' belongs among the sorted ones before it: past every one not
/// greater, so equal elems keep their order. The elem is still in place while this looks,
/// so the comparator is only ever handed pointers into the span, aligned as its elems are
[[nodiscard]]
static size_t insertion_slot(tda_Span s, size_t idx, tda_Cmp cmp);

/* ========== sort ========== */

void tda_span_insertion_sort(tda_SpanMut s, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);

    const tda_Span cs = tda_span_mut_to_span(s);

    // too big to set aside: every step is a swap
    if (s.elem_size > INSERTION_HELD_MAX) {
        for (size_t i = 1; i < s.len; ++i) {
            const size_t j = insertion_slot(cs, i, cmp);
            for (size_t k = i; k > j; --k) {
                tda_span_swap_elems(s, k - 1, k);
            }
        }
        return;
    }

    // set the elem aside and slide the run above its slot up in one memmove, instead of a
    // three-move swap per step
    unsigned char held[INSERTION_HELD_MAX];
    for (size_t i = 1; i < s.len; ++i) {
        const size_t j = insertion_slot(cs, i, cmp);
        if (j == i) {
            continue;
        }
        memcpy(held, tda_span_get(cs, i), s.elem_size);
        memmove(tda_span_get_mut(s, j + 1), tda_span_get(cs, j), (i - j) * s.elem_size);
        memcpy(tda_span_get_mut(s, j), held, s.elem_size);
    }
}

void tda_span_sort(tda_SpanMut s, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);

    if (s.len < 2) {
        return;
    }

    quicksort(s, 0, s.len - 1, depth_limit(s.len), cmp);
}

tda_Status tda_span_sort_stable(tda_SpanMut s, tda_Cmp cmp, tda_Al *al) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    assert(al);

    if (s.len < 2) {
        return TDA_STATUS_OK;
    }

    const size_t bytes = s.len * s.elem_size;

    void *buf = tda_alloc(al, bytes);
    if (!buf) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    tda_SpanMut src = s;
    tda_SpanMut dst = tda_span_from_data_mut(buf, s.len, s.elem_size);

    // bottom-up merge sort: merge runs of width 1, 2, 4, ...
    for (size_t width = 1; width < s.len; width *= 2) {
        for (size_t i = 0; i < s.len; i += 2 * width) {
            const size_t mid = i + width < s.len ? i + width : s.len;
            const size_t end = i + 2 * width < s.len ? i + 2 * width : s.len;

            const tda_Span run = tda_span_mut_to_span(src);
            tda_span_merge(
                tda_span_sub_mut(dst, i, end - i),
                tda_span_sub(run, i, mid - i),
                tda_span_sub(run, mid, end - mid),
                cmp
            );
        }
        TDA_SWAP(src, dst);
    }

    // if result ended up in buf, copy back to original
    if (src.data != s.data) {
        tda_span_copy(s, tda_span_mut_to_span(src));
    }

    tda_dealloc(al, buf, bytes);

    return TDA_STATUS_OK;
}

void tda_span_partial_sort(tda_SpanMut s, size_t count, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    TDA_EXPECT(count <= s.len);

    if (count == 0 || s.len < 2) {
        return;
    }

    if (count >= s.len) {
        tda_span_sort(s, cmp);
        return;
    }

    // place element that would be at position count in sorted order
    tda_span_nth_elem(s, count, cmp);

    // now the first count elems are the count smallest (order unspecified) -> sort them
    tda_span_sort(tda_span_sub_mut(s, 0, count), cmp);
}

void tda_span_nth_elem(tda_SpanMut s, size_t nth, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    TDA_EXPECT(nth < s.len);

    if (s.len < 2) {
        return;
    }

    size_t left = 0, right = s.len - 1;
    size_t depth = depth_limit(s.len);

    // left <= nth <= right holds every round, which is what keeps lt - 1 and gt + 1
    // inside the range below
    while (left < right) {
        // the ceiling sort has, for the same reason: past it the pivots have gone bad.
        // Sorting what is left settles nth along with the rest, O(n log n) whatever the data
        if (depth == 0) {
            tda_span_sort(tda_span_sub_mut(s, left, right - left + 1), cmp);
            return;
        }
        --depth;

        const size_t pivot_idx = ninther(tda_span_mut_to_span(s), left, right, cmp);

        const Split p = partition3(s, left, right, pivot_idx, cmp);
        const size_t lt = p.lt;
        const size_t gt = p.gt;

        if (nth < lt) {
            right = lt - 1;
        } else if (nth > gt) {
            left = gt + 1;
        } else {
            return; // nth landed inside the run of elems equal to the pivot
        }
    }
}

/* ========== info ========== */

bool tda_span_is_sorted(tda_Span s, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);

    return tda_span_is_sorted_until(s, cmp) == s.len;
}

size_t tda_span_is_sorted_until(tda_Span s, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);

    for (size_t i = 1; i < s.len; ++i) {
        const void *prev = tda_span_get(s, i - 1);
        const void *cur = tda_span_get(s, i);
        if (cmp(prev, cur) > 0) {
            return i;
        }
    }
    return s.len;
}

/* ========== internals ========== */

static size_t median3(tda_Span s, size_t a, size_t b, size_t c, tda_Cmp cmp) {
    const void *a_ptr = tda_span_get(s, a);
    const void *b_ptr = tda_span_get(s, b);
    const void *c_ptr = tda_span_get(s, c);

    const int ab = cmp(a_ptr, b_ptr);
    const int ac = cmp(a_ptr, c_ptr);
    const int bc = cmp(b_ptr, c_ptr);

    // A between B and C: either c <= a <= b, or b <= a <= c
    if ((ab <= 0 && ac >= 0) || (ab >= 0 && ac <= 0)) return a;
    // B between A and C: either a <= b <= c, or c <= b <= a
    if ((ab <= 0 && bc <= 0) || (ab >= 0 && bc >= 0)) return b;
    // else C is median
    return c;
}

static size_t ninther(tda_Span s, size_t left, size_t right, tda_Cmp cmp) {
    const size_t len = right - left + 1;
    const size_t mid = left + len / 2;

    // on a range shorter than 8 the step is 0, every triple collapses to a single elem
    // and this degenerates into a plain median of left, mid and right — still valid,
    // just less well sampled. nth_elem reaches those short ranges; quicksort does not.
    const size_t step = len / 8;

    const size_t lo = median3(s, left, left + step, left + 2 * step, cmp);
    const size_t md = median3(s, mid - step, mid, mid + step, cmp);
    const size_t hi = median3(s, right - 2 * step, right - step, right, cmp);

    return median3(s, lo, md, hi, cmp);
}

static Split partition3(tda_SpanMut s, size_t left, size_t right, size_t pivot_idx, tda_Cmp cmp) {
    assert(left <= pivot_idx && pivot_idx <= right);

    tda_span_swap_elems(s, left, pivot_idx);

    const tda_Span v = tda_span_mut_to_span(s);

    size_t lo = left;
    size_t i = left + 1;
    size_t hi = right;

    // invariant: [left, lo-1] < pivot, [lo, i-1] == pivot, [i, hi] untouched,
    // [hi+1, right] > pivot. The equal run always holds at least the pivot elem, so
    // s[lo] is a copy of the pivot value and can be read instead of buffering it —
    // which matters here, where the elem size is only known at runtime.
    while (i <= hi) {
        const int c = cmp(tda_span_get(v, i), tda_span_get(v, lo));

        if (c < 0) {
            tda_span_swap_elems(s, i, lo);
            ++lo;
            ++i;
        } else if (c > 0) {
            tda_span_swap_elems(s, i, hi);
            --hi; // cannot wrap: the loop stops before hi reaches left
        } else {
            ++i;
        }
    }

    return (Split){.lt = lo, .gt = hi};
}

static size_t insertion_slot(tda_Span s, size_t idx, tda_Cmp cmp) {
    const void *val = tda_span_get(s, idx);

    size_t slot = idx;
    while (slot > 0 && cmp(tda_span_get(s, slot - 1), val) > 0) {
        --slot;
    }
    return slot;
}

static size_t depth_limit(size_t len) {
    assert(len > 1);

    // balanced partitions need floor(log2(len)) levels; twice that is the slack allowed
    // before the pivots are judged to have gone bad
    return 2 * (stdc_bit_width(len) - 1);
}

static void quicksort(tda_SpanMut s, size_t left, size_t right, size_t depth, tda_Cmp cmp) {
    while (left < right) {
        if (right - left + 1 <= INSERTION_THRESHOLD) {
            tda_span_insertion_sort(tda_span_sub_mut(s, left, right - left + 1), cmp);
            return;
        }

        // deeper than balanced partitions would ever go, so the pivots have gone bad;
        // heapsort is O(n log n) whatever the data, which puts a ceiling on the worst case
        if (depth == 0) {
            const tda_SpanMut range = tda_span_sub_mut(s, left, right - left + 1);
            tda_span_make_heap(range, cmp);
            tda_span_sort_heap(range, cmp);
            return;
        }
        --depth;

        // sampled across the range, not just at its two ends and middle: partitioning
        // leaves the smallest elem of the left side sitting at that side's last
        // position, and median-of-3 would then keep picking a near-minimum pivot
        const size_t pivot_idx = ninther(tda_span_mut_to_span(s), left, right, cmp);

        // three-way, so a run of equal keys is settled in one pass. A two-way split
        // peels those off one elem at a time, which is quadratic on repeated keys.
        const Split p = partition3(s, left, right, pivot_idx, cmp);
        const size_t lt = p.lt;
        const size_t gt = p.gt;

        // recurse into the shorter side and loop on the longer one: the recursion then
        // halves its range every time, so the stack stays O(log n) whatever the data
        if (lt - left < right - gt) {
            if (lt > left) {
                quicksort(s, left, lt - 1, depth, cmp);
            }
            left = gt + 1;
        } else {
            if (gt < right) {
                quicksort(s, gt + 1, right, depth, cmp);
            }
            if (lt == left) {
                return; // nothing sits below the pivot run
            }
            right = lt - 1;
        }
    }
}
