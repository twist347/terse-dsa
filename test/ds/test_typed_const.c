// The read accessors hand out a const T * for any type name T. A pointer elem is where a
// plain 'const T *' goes wrong: for T = const char * it spells const char **, through
// which the stored pointer could be overwritten. _Generic does not evaluate its operand,
// so the checks run at compile time on null handles.

#include "tda/alloc/default.h"
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

#define READS_CONST(expr) static_assert(_Generic((expr), const char *const *: true, default: false))

READS_CONST(TDA_SPAN_GET_AS(const char *, (tda_Span){}, 0));

READS_CONST(TDA_ARR_FRONT_AS(const char *, (const tda_Arr *) nullptr));
READS_CONST(TDA_ARR_BACK_AS(const char *, (const tda_Arr *) nullptr));
READS_CONST(TDA_ARR_GET_AS(const char *, (const tda_Arr *) nullptr, 0));

READS_CONST(TDA_VEC_FRONT_AS(const char *, (const tda_Vec *) nullptr));
READS_CONST(TDA_VEC_BACK_AS(const char *, (const tda_Vec *) nullptr));
READS_CONST(TDA_VEC_GET_AS(const char *, (const tda_Vec *) nullptr, 0));

READS_CONST(TDA_DEQUE_FRONT_AS(const char *, (const tda_Deque *) nullptr));
READS_CONST(TDA_DEQUE_BACK_AS(const char *, (const tda_Deque *) nullptr));
READS_CONST(TDA_DEQUE_GET_AS(const char *, (const tda_Deque *) nullptr, 0));

READS_CONST(TDA_LIST_FRONT_AS(const char *, (const tda_List *) nullptr));
READS_CONST(TDA_LIST_BACK_AS(const char *, (const tda_List *) nullptr));
READS_CONST(TDA_LIST_NODE_ELEM_AS(const char *, (const tda_List *) nullptr, (const tda_ListNode *) nullptr));

READS_CONST(TDA_STACK_TOP_AS(const char *, (const tda_Stack *) nullptr));
READS_CONST(TDA_QUEUE_FRONT_AS(const char *, (const tda_Queue *) nullptr));
READS_CONST(TDA_QUEUE_BACK_AS(const char *, (const tda_Queue *) nullptr));
READS_CONST(TDA_PQUEUE_TOP_AS(const char *, (const tda_PQueue *) nullptr));

READS_CONST(TDA_HMAP_GET_AS(const char *, const char *, (const tda_HMap *) nullptr, "key"));
READS_CONST(TDA_HMAP_NODE_KEY_AS(const char *, (const tda_HMap *) nullptr, (const tda_HMapNode *) nullptr));
READS_CONST(TDA_HMAP_NODE_VAL_AS(const char *, (const tda_HMap *) nullptr, (const tda_HMapNode *) nullptr));
READS_CONST(TDA_HSET_NODE_KEY_AS(const char *, (const tda_HSet *) nullptr, (const tda_HSetNode *) nullptr));

void setUp() {
}

void tearDown() {
}

// the walks declare their loop variable, which the checks above cannot reach
static void test_the_walks_bind_a_const_elem() {
    size_t seen = 0;
    TDA_SPAN_FOR_EACH_AS(const char *, word, TDA_SPAN_OF(const char *, "a", "b")) {
        READS_CONST(word);
        ++seen;
    }
    TEST_ASSERT_EQUAL_size_t(2, seen);

    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_OF(const char *, tda_al_default(), &d, "a", "b"));

    seen = 0;
    TDA_DEQUE_FOR_EACH_AS(const char *, word, &d) {
        READS_CONST(word);
        ++seen;
    }
    TEST_ASSERT_EQUAL_size_t(2, seen);

    tda_deque_deinit(&d);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_the_walks_bind_a_const_elem);

    return UNITY_END();
}
