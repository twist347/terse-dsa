// Every typed macro that takes one value takes it as &(T[1]){ (val) }[0], so a struct goes
// in as a scalar does: a variable of its type, or a literal in parentheses. Each case runs
// both forms through one macro family and reads the result back.

#include "tda/alloc/default.h"
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

#include "support/pair.h"
#include "support/status.h"

#include <unity.h>

#include <stdint.h>

void setUp() {
}

void tearDown() {
}

static void assert_pair(int64_t a, int64_t b, const Pair *p) {
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT64(a, p->a);
    TEST_ASSERT_EQUAL_INT64(b, p->b);
}

static tda_Hash hash_pair(const void *val, void *) {
    const Pair *p = val;

    return tda_hash_combine(tda_hash_i64(&p->a, nullptr), tda_hash_i64(&p->b, nullptr));
}

static bool eq_pair(const void *lhs, const void *rhs, void *) {
    const Pair *l = lhs, *r = rhs;

    return l->a == r->a && l->b == r->b;
}

/* ========== contiguous ========== */

static void test_vec_macros_take_a_struct() {
    const Pair p = {1, 2};
    tda_Vec v = TDA_VEC_INIT(Pair, tda_al_default());

    TDA_TEST_OK(TDA_VEC_PUSH(Pair, &v, p));
    TDA_TEST_OK(TDA_VEC_PUSH(Pair, &v, ((Pair){3, 4})));
    TDA_TEST_OK(TDA_VEC_INSERT(Pair, &v, 0, ((Pair){5, 6})));
    TDA_VEC_SET(Pair, &v, 2, p);
    TDA_TEST_OK(tda_vec_reserve(&v, 4));
    TDA_VEC_PUSH_ASSUME_CAP(Pair, &v, ((Pair){7, 8}));

    assert_pair(5, 6, TDA_VEC_GET_AS(Pair, &v, 0));
    assert_pair(1, 2, TDA_VEC_GET_AS(Pair, &v, 1));
    assert_pair(1, 2, TDA_VEC_GET_AS(Pair, &v, 2));
    assert_pair(7, 8, TDA_VEC_GET_AS(Pair, &v, 3));

    tda_vec_deinit(&v);
}

static void test_arr_and_span_set_take_a_struct() {
    const Pair p = {1, 2};
    tda_Arr a;
    TDA_TEST_OK(TDA_ARR_INIT_LEN(Pair, 2, tda_al_default(), &a));

    TDA_ARR_SET(Pair, &a, 0, p);
    TDA_ARR_SET(Pair, &a, 1, ((Pair){3, 4}));
    assert_pair(1, 2, TDA_ARR_GET_AS(Pair, &a, 0));
    assert_pair(3, 4, TDA_ARR_GET_AS(Pair, &a, 1));

    const tda_SpanMut s = tda_arr_to_span_mut(&a);
    TDA_SPAN_SET(Pair, s, 0, ((Pair){5, 6}));
    TDA_SPAN_SET(Pair, s, 1, p);
    assert_pair(5, 6, TDA_ARR_GET_AS(Pair, &a, 0));
    assert_pair(1, 2, TDA_ARR_GET_AS(Pair, &a, 1));

    tda_arr_deinit(&a);
}

static void test_deque_macros_take_a_struct() {
    const Pair p = {1, 2};
    tda_Deque d = TDA_DEQUE_INIT(Pair, tda_al_default());

    TDA_TEST_OK(TDA_DEQUE_PUSH_BACK(Pair, &d, p));
    TDA_TEST_OK(TDA_DEQUE_PUSH_FRONT(Pair, &d, ((Pair){3, 4})));
    TDA_TEST_OK(TDA_DEQUE_INSERT(Pair, &d, 1, ((Pair){5, 6})));
    TDA_DEQUE_SET(Pair, &d, 2, ((Pair){7, 8}));

    assert_pair(3, 4, TDA_DEQUE_GET_AS(Pair, &d, 0));
    assert_pair(5, 6, TDA_DEQUE_GET_AS(Pair, &d, 1));
    assert_pair(7, 8, TDA_DEQUE_GET_AS(Pair, &d, 2));

    tda_deque_deinit(&d);
}

/* ========== adapters ========== */

static void test_adapter_pushes_take_a_struct() {
    const Pair p = {1, 2};

    tda_Stack st = TDA_STACK_INIT(Pair, tda_al_default());
    TDA_TEST_OK(TDA_STACK_PUSH(Pair, &st, p));
    TDA_TEST_OK(TDA_STACK_PUSH(Pair, &st, ((Pair){3, 4})));
    assert_pair(3, 4, TDA_STACK_TOP_AS(Pair, &st));
    tda_stack_deinit(&st);

    tda_Queue q = TDA_QUEUE_INIT(Pair, tda_al_default());
    TDA_TEST_OK(TDA_QUEUE_PUSH(Pair, &q, p));
    TDA_TEST_OK(TDA_QUEUE_PUSH(Pair, &q, ((Pair){3, 4})));
    assert_pair(1, 2, TDA_QUEUE_FRONT_AS(Pair, &q));
    assert_pair(3, 4, TDA_QUEUE_BACK_AS(Pair, &q));
    tda_queue_deinit(&q);

    tda_PQueue pq = TDA_PQUEUE_INIT(Pair, tda_test_pair_cmp_a, nullptr, tda_al_default());
    TDA_TEST_OK(TDA_PQUEUE_PUSH(Pair, &pq, p));
    TDA_TEST_OK(TDA_PQUEUE_PUSH(Pair, &pq, ((Pair){3, 4})));
    assert_pair(3, 4, TDA_PQUEUE_TOP_AS(Pair, &pq));
    TDA_PQUEUE_REPLACE_TOP(Pair, &pq, ((Pair){0, 9}));
    assert_pair(1, 2, TDA_PQUEUE_TOP_AS(Pair, &pq));
    tda_pqueue_deinit(&pq);
}

/* ========== list ========== */

static void test_list_macros_take_a_struct() {
    const Pair p = {1, 2};
    tda_List l = TDA_LIST_INIT(Pair, tda_al_default());

    TDA_TEST_OK(TDA_LIST_PUSH_BACK(Pair, &l, p));
    TDA_TEST_OK(TDA_LIST_PUSH_FRONT(Pair, &l, ((Pair){3, 4})));
    tda_ListNode *back = tda_list_back_node_mut(&l);
    TDA_TEST_OK(TDA_LIST_INSERT_BEFORE(Pair, &l, back, ((Pair){5, 6})));
    TDA_TEST_OK(TDA_LIST_INSERT_AFTER(Pair, &l, back, ((Pair){7, 8})));

    assert_pair(3, 4, TDA_LIST_FRONT_AS(Pair, &l));
    assert_pair(7, 8, TDA_LIST_BACK_AS(Pair, &l));

    const tda_ListNode *found = TDA_LIST_FIND(Pair, &l, p, eq_pair, nullptr);
    assert_pair(1, 2, TDA_LIST_NODE_ELEM_AS(Pair, &l, found));
    tda_ListNode *mid = TDA_LIST_FIND_MUT(Pair, &l, ((Pair){5, 6}), eq_pair, nullptr);
    assert_pair(5, 6, TDA_LIST_NODE_ELEM_AS(Pair, &l, mid));

    tda_list_deinit(&l);
}

/* ========== hash ========== */

static void test_hmap_macros_take_a_struct_key_and_value() {
    const Pair k = {1, 2}, v = {10, 20};
    tda_HMap m = TDA_HMAP_INIT(Pair, Pair, hash_pair, eq_pair, nullptr, tda_al_default());

    TDA_TEST_OK(TDA_HMAP_INSERT(Pair, Pair, &m, k, v, nullptr));
    TDA_TEST_OK(TDA_HMAP_INSERT(Pair, Pair, &m, ((Pair){3, 4}), ((Pair){30, 40}), nullptr));
    tda_HMapNode *node;
    TDA_TEST_OK(TDA_HMAP_GET_OR_INSERT(Pair, Pair, &m, ((Pair){5, 6}), ((Pair){50, 60}), &node));

    assert_pair(10, 20, TDA_HMAP_GET_AS(Pair, Pair, &m, k));
    assert_pair(30, 40, TDA_HMAP_GET_MUT_AS(Pair, Pair, &m, ((Pair){3, 4})));
    TEST_ASSERT_TRUE(TDA_HMAP_CONTAINS(Pair, &m, ((Pair){5, 6})));
    TEST_ASSERT_NOT_NULL(TDA_HMAP_FIND(Pair, &m, k));
    TEST_ASSERT_NOT_NULL(TDA_HMAP_FIND_MUT(Pair, &m, k));

    Pair out_k, out_v;
    TEST_ASSERT_TRUE(TDA_HMAP_TAKE(Pair, Pair, &m, k, &out_k, &out_v));
    assert_pair(1, 2, &out_k);
    assert_pair(10, 20, &out_v);
    TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(Pair, &m, ((Pair){3, 4})));
    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(&m));

    tda_hmap_deinit(&m);
}

static void test_hset_macros_take_a_struct_key() {
    const Pair k = {1, 2};
    tda_HSet s = TDA_HSET_INIT(Pair, hash_pair, eq_pair, nullptr, tda_al_default());

    TDA_TEST_OK(TDA_HSET_INSERT(Pair, &s, k, nullptr));
    TDA_TEST_OK(TDA_HSET_INSERT(Pair, &s, ((Pair){3, 4}), nullptr));

    TEST_ASSERT_TRUE(TDA_HSET_CONTAINS(Pair, &s, ((Pair){3, 4})));
    TEST_ASSERT_NOT_NULL(TDA_HSET_FIND(Pair, &s, k));
    TEST_ASSERT_NOT_NULL(TDA_HSET_FIND_MUT(Pair, &s, k));

    Pair out_k;
    TEST_ASSERT_TRUE(TDA_HSET_TAKE(Pair, &s, k, &out_k));
    assert_pair(1, 2, &out_k);
    TEST_ASSERT_TRUE(TDA_HSET_REMOVE(Pair, &s, ((Pair){3, 4})));
    TEST_ASSERT_EQUAL_size_t(0, tda_hset_len(&s));

    tda_hset_deinit(&s);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_vec_macros_take_a_struct);
    RUN_TEST(test_arr_and_span_set_take_a_struct);
    RUN_TEST(test_deque_macros_take_a_struct);
    RUN_TEST(test_adapter_pushes_take_a_struct);
    RUN_TEST(test_list_macros_take_a_struct);
    RUN_TEST(test_hmap_macros_take_a_struct_key_and_value);
    RUN_TEST(test_hset_macros_take_a_struct_key);

    return UNITY_END();
}
