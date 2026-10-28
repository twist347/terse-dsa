// The typed macros write typeof(T), so a type name with a declarator goes in as it is
// spelled. A bare T would make 'int32_t (*)(int32_t) *' or 'int32_t[3] *' out of these,
// which do not parse. A function pointer goes through every family; an array has no value
// to copy in, so it goes through the macros that take an address only.

#include "tda/alloc/alloc.h"
#include "tda/alloc/default.h"
#include "tda/algo/fill.h"
#include "tda/algo/modify.h"
#include "tda/algo/search.h"
#include "tda/core/hash.h"
#include "tda/core/span.h"
#include "tda/ds/arr.h"
#include "tda/ds/deque.h"
#include "tda/ds/hmap.h"
#include "tda/ds/hset.h"
#include "tda/ds/list.h"
#include "tda/ds/pqueue.h"
#include "tda/ds/queue.h"
#include "tda/ds/stack.h"
#include "tda/ds/vec.h"

#include "support/status.h"

#include <unity.h>

#include <stddef.h>
#include <stdint.h>

typedef int32_t (*Fn)(int32_t);

static int32_t twice(int32_t x) {
    return 2 * x;
}

static int32_t thrice(int32_t x) {
    return 3 * x;
}

static int32_t negate(int32_t x) {
    return -x;
}

static bool fn_eq(const void *lhs, const void *rhs, [[maybe_unused]] void *ctx) {
    return *(const Fn *) lhs == *(const Fn *) rhs;
}

// ordered by what each one makes of 1, so a sorted order exists
static int fn_cmp(const void *lhs, const void *rhs, [[maybe_unused]] void *ctx) {
    int32_t a = (*(const Fn *) lhs)(1), b = (*(const Fn *) rhs)(1);

    return (a > b) - (a < b);
}

static tda_Hash fn_hash(const void *val, [[maybe_unused]] void *ctx) {
    return tda_hash_bytes(val, sizeof(Fn));
}

void setUp() {
}

void tearDown() {
}

static void test_a_function_pointer_goes_through_the_span_macros() {
    tda_SpanMut s = TDA_SPAN_OF_MUT(int32_t (*)(int32_t), twice, thrice, negate);

    TDA_SPAN_SET(int32_t (*)(int32_t), s, 2, twice);
    TEST_ASSERT_EQUAL_INT32(4, (*TDA_SPAN_GET_MUT_AS(int32_t (*)(int32_t), s, 2))(2));

    TDA_SPAN_FILL(int32_t (*)(int32_t), s, thrice);
    TDA_SPAN_FOR_EACH_MUT_AS(int32_t (*)(int32_t), f, s) {
        TEST_ASSERT_EQUAL_INT32(6, (*f)(2));
    }

    TDA_SPAN_REPLACE(int32_t (*)(int32_t), int32_t (*)(int32_t), s, thrice, negate, fn_eq, nullptr);
    TEST_ASSERT_EQUAL_size_t(3, TDA_SPAN_COUNT(int32_t (*)(int32_t), tda_span_mut_to_span(s), negate, fn_eq, nullptr));

    Fn data[] = { negate, twice, thrice };
    tda_SpanMut d = TDA_SPAN_FROM_DATA_MUT(int32_t (*)(int32_t), data, 3);
    size_t idx = 0;

    TEST_ASSERT_TRUE(TDA_SPAN_FIND(int32_t (*)(int32_t), tda_span_mut_to_span(d), twice, fn_eq, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(1, idx);
    TEST_ASSERT_TRUE(TDA_SPAN_BINARY_SEARCH(int32_t (*)(int32_t), tda_span_mut_to_span(d), thrice, fn_cmp, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(2, idx);
}

static void test_a_function_pointer_goes_through_the_linear_containers() {
    tda_Vec v = TDA_VEC_INIT(int32_t (*)(int32_t), tda_al_default());
    Fn out = nullptr;

    TDA_TEST_OK(TDA_VEC_PUSH(int32_t (*)(int32_t), &v, twice));
    TDA_TEST_OK(TDA_VEC_INSERT(int32_t (*)(int32_t), &v, 0, thrice));
    TDA_VEC_SET(int32_t (*)(int32_t), &v, 1, negate);
    TEST_ASSERT_EQUAL_INT32(3, (*TDA_VEC_FRONT_MUT_AS(int32_t (*)(int32_t), &v))(1));
    TDA_VEC_POP_TO(int32_t (*)(int32_t), &v, &out);
    TEST_ASSERT_EQUAL_INT32(-1, out(1));
    tda_vec_deinit(&v);

    tda_Deque d = TDA_DEQUE_INIT(int32_t (*)(int32_t), tda_al_default());

    TDA_TEST_OK(TDA_DEQUE_PUSH_BACK(int32_t (*)(int32_t), &d, twice));
    TDA_TEST_OK(TDA_DEQUE_PUSH_FRONT(int32_t (*)(int32_t), &d, thrice));
    TDA_TEST_OK(TDA_DEQUE_INSERT(int32_t (*)(int32_t), &d, 1, negate));
    TDA_DEQUE_SET(int32_t (*)(int32_t), &d, 2, negate);
    TEST_ASSERT_EQUAL_INT32(-1, (*TDA_DEQUE_GET_MUT_AS(int32_t (*)(int32_t), &d, 1))(1));
    TDA_DEQUE_POP_FRONT_TO(int32_t (*)(int32_t), &d, &out);
    TEST_ASSERT_EQUAL_INT32(3, out(1));
    TDA_DEQUE_POP_BACK_TO(int32_t (*)(int32_t), &d, &out);
    TEST_ASSERT_EQUAL_INT32(-1, out(1));
    tda_deque_deinit(&d);

    tda_List l = TDA_LIST_INIT(int32_t (*)(int32_t), tda_al_default());

    TDA_TEST_OK(TDA_LIST_PUSH_BACK(int32_t (*)(int32_t), &l, twice));
    TDA_TEST_OK(TDA_LIST_PUSH_FRONT(int32_t (*)(int32_t), &l, thrice));
    tda_ListNode *node = TDA_LIST_FIND_MUT(int32_t (*)(int32_t), &l, twice, fn_eq, nullptr);

    TEST_ASSERT_NOT_NULL(node);
    TDA_TEST_OK(TDA_LIST_INSERT_BEFORE(int32_t (*)(int32_t), &l, node, negate));
    TDA_TEST_OK(TDA_LIST_INSERT_AFTER(int32_t (*)(int32_t), &l, node, negate));
    TEST_ASSERT_EQUAL_INT32(2, (*TDA_LIST_NODE_ELEM_MUT_AS(int32_t (*)(int32_t), &l, node))(1));
    TDA_LIST_POP_BACK_TO(int32_t (*)(int32_t), &l, &out);
    TEST_ASSERT_EQUAL_INT32(-1, out(1));
    TDA_LIST_POP_FRONT_TO(int32_t (*)(int32_t), &l, &out);
    TEST_ASSERT_EQUAL_INT32(3, out(1));
    tda_list_deinit(&l);

    tda_Arr a = {};

    TDA_TEST_OK(TDA_ARR_INIT_LEN(int32_t (*)(int32_t), 2, tda_al_default(), &a));
    TDA_ARR_SET(int32_t (*)(int32_t), &a, 1, thrice);
    TEST_ASSERT_EQUAL_INT32(3, (*TDA_ARR_BACK_MUT_AS(int32_t (*)(int32_t), &a))(1));
    tda_arr_deinit(&a);
}

static void test_a_function_pointer_goes_through_the_adapters() {
    tda_Stack s = TDA_STACK_INIT(int32_t (*)(int32_t), tda_al_default());
    Fn out = nullptr;

    TDA_TEST_OK(TDA_STACK_PUSH(int32_t (*)(int32_t), &s, twice));
    TEST_ASSERT_EQUAL_INT32(2, (*TDA_STACK_TOP_MUT_AS(int32_t (*)(int32_t), &s))(1));
    TDA_STACK_POP_TO(int32_t (*)(int32_t), &s, &out);
    TEST_ASSERT_EQUAL_INT32(2, out(1));
    tda_stack_deinit(&s);

    tda_Queue q = TDA_QUEUE_INIT(int32_t (*)(int32_t), tda_al_default());

    TDA_TEST_OK(TDA_QUEUE_PUSH(int32_t (*)(int32_t), &q, twice));
    TDA_TEST_OK(TDA_QUEUE_PUSH(int32_t (*)(int32_t), &q, thrice));
    TEST_ASSERT_EQUAL_INT32(3, (*TDA_QUEUE_BACK_MUT_AS(int32_t (*)(int32_t), &q))(1));
    TDA_QUEUE_POP_TO(int32_t (*)(int32_t), &q, &out);
    TEST_ASSERT_EQUAL_INT32(2, out(1));
    tda_queue_deinit(&q);

    tda_PQueue p = TDA_PQUEUE_INIT(int32_t (*)(int32_t), fn_cmp, nullptr, tda_al_default());

    TDA_TEST_OK(TDA_PQUEUE_PUSH(int32_t (*)(int32_t), &p, negate));
    TDA_TEST_OK(TDA_PQUEUE_PUSH(int32_t (*)(int32_t), &p, twice));
    TDA_PQUEUE_REPLACE_TOP(int32_t (*)(int32_t), &p, thrice);
    TDA_PQUEUE_POP_TO(int32_t (*)(int32_t), &p, &out);
    TEST_ASSERT_EQUAL_INT32(3, out(1));
    tda_pqueue_deinit(&p);
}

static void test_a_function_pointer_goes_through_the_hashed_containers() {
    tda_HMap m = TDA_HMAP_INIT(int32_t (*)(int32_t), int32_t (*)(int32_t), fn_hash, fn_eq, nullptr, tda_al_default());
    Fn key = nullptr, val = nullptr;

    TDA_TEST_OK(TDA_HMAP_INSERT(int32_t (*)(int32_t), int32_t (*)(int32_t), &m, twice, negate, nullptr));
    TEST_ASSERT_TRUE(TDA_HMAP_CONTAINS(int32_t (*)(int32_t), &m, twice));
    *TDA_HMAP_GET_MUT_AS(int32_t (*)(int32_t), int32_t (*)(int32_t), &m, twice) = thrice;

    tda_HMapNode *node = TDA_HMAP_FIND_MUT(int32_t (*)(int32_t), &m, twice);

    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT32(3, (*TDA_HMAP_NODE_VAL_MUT_AS(int32_t (*)(int32_t), &m, node))(1));
    TEST_ASSERT_TRUE(TDA_HMAP_TAKE(int32_t (*)(int32_t), int32_t (*)(int32_t), &m, twice, &key, &val));
    TEST_ASSERT_EQUAL_INT32(2, key(1));
    TEST_ASSERT_EQUAL_INT32(3, val(1));
    tda_hmap_deinit(&m);

    tda_HSet h = TDA_HSET_INIT(int32_t (*)(int32_t), fn_hash, fn_eq, nullptr, tda_al_default());

    TDA_TEST_OK(TDA_HSET_INSERT(int32_t (*)(int32_t), &h, negate, nullptr));
    TEST_ASSERT_TRUE(TDA_HSET_CONTAINS(int32_t (*)(int32_t), &h, negate));
    TEST_ASSERT_TRUE(TDA_HSET_TAKE(int32_t (*)(int32_t), &h, negate, &key));
    TEST_ASSERT_EQUAL_INT32(-1, key(1));
    tda_hset_deinit(&h);
}

static void test_an_array_goes_through_the_address_macros() {
    int32_t (*block)[3] = TDA_ALLOC(int32_t[3], tda_al_default(), 2);

    TEST_ASSERT_NOT_NULL(block);
    block[1][2] = 7;
    block = TDA_REALLOC(int32_t[3], tda_al_default(), block, 2, 4);
    TEST_ASSERT_NOT_NULL(block);
    TEST_ASSERT_EQUAL_INT32(7, block[1][2]);
    TDA_DEALLOC(int32_t[3], tda_al_default(), block, 4);

    int32_t rows[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t[3], rows, 2);

    TEST_ASSERT_EQUAL_INT32(6, (*TDA_SPAN_GET_MUT_AS(int32_t[3], s, 1))[2]);
    TDA_SPAN_FOR_EACH_MUT_AS(int32_t[3], row, s) {
        (*row)[0] = 0;
    }
    TEST_ASSERT_EQUAL_INT32(0, rows[1][0]);

    int32_t got[3] = {};
    tda_Vec v = {};

    TDA_TEST_OK(TDA_VEC_INIT_LEN(int32_t[3], 2, tda_al_default(), &v));
    (*TDA_VEC_GET_MUT_AS(int32_t[3], &v, 1))[2] = 9;
    (*TDA_VEC_FRONT_MUT_AS(int32_t[3], &v))[0] = 8;
    TDA_VEC_POP_TO(int32_t[3], &v, &got);
    TEST_ASSERT_EQUAL_INT32(9, got[2]);
    tda_vec_deinit(&v);

    tda_Deque d = {};

    TDA_TEST_OK(TDA_DEQUE_INIT_LEN(int32_t[3], 2, tda_al_default(), &d));
    TDA_DEQUE_FOR_EACH_MUT_AS(int32_t[3], row, &d) {
        (*row)[1] = 5;
    }
    TDA_DEQUE_POP_FRONT_TO(int32_t[3], &d, &got);
    TEST_ASSERT_EQUAL_INT32(5, got[1]);
    tda_deque_deinit(&d);

    tda_List l = TDA_LIST_INIT(int32_t[3], tda_al_default());

    TDA_TEST_OK(tda_list_push_back(&l, rows[1]));
    (*TDA_LIST_FRONT_MUT_AS(int32_t[3], &l))[1] = 3;
    TDA_LIST_POP_BACK_TO(int32_t[3], &l, &got);
    TEST_ASSERT_EQUAL_INT32(3, got[1]);
    tda_list_deinit(&l);

    tda_Stack st = TDA_STACK_INIT(int32_t[3], tda_al_default());

    TDA_TEST_OK(tda_stack_push(&st, rows[0]));
    (*TDA_STACK_TOP_MUT_AS(int32_t[3], &st))[2] = 1;
    TDA_STACK_POP_TO(int32_t[3], &st, &got);
    TEST_ASSERT_EQUAL_INT32(1, got[2]);
    tda_stack_deinit(&st);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_a_function_pointer_goes_through_the_span_macros);
    RUN_TEST(test_a_function_pointer_goes_through_the_linear_containers);
    RUN_TEST(test_a_function_pointer_goes_through_the_adapters);
    RUN_TEST(test_a_function_pointer_goes_through_the_hashed_containers);
    RUN_TEST(test_an_array_goes_through_the_address_macros);

    return UNITY_END();
}
