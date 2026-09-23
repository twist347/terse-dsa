#include "tda/ds/list.h"

#include "tda/core/check.h"
#include "tda/core/util.h"

#include "internal/ptr.h"

#include <assert.h>
#include <stdckdint.h>
#include <string.h>

/* ========== internals ========== */

#define ASSERT_LIST(l)                                         \
    (assert(l),                                                \
     assert((l)->elem_size > 0),                               \
     assert((l)->al),                                          \
     assert(((l)->len > 0) == ((l)->head != nullptr)),         \
     assert(((l)->head != nullptr) == ((l)->tail != nullptr)))

#define ASSERT_NODE(n)                             \
    (assert(n),                                    \
     assert(!(n)->prev || (n)->prev->next == (n)), \
     assert(!(n)->next || (n)->next->prev == (n)))

struct tda_ListNode {
    tda_ListNode *next;
    tda_ListNode *prev;
    alignas(max_align_t) unsigned char elem[];
};

struct tda_List {
    tda_ListNode *head;
    tda_ListNode *tail;
    size_t len;
    size_t elem_size;
    tda_Al *al;
};

[[nodiscard]]
static size_t node_bytes(size_t elem_size);

[[nodiscard]]
static tda_Status node_new(tda_Al *al, size_t elem_size, const void *val, tda_ListNode **out);

static void node_drop(tda_Al *al, size_t elem_size, tda_ListNode *node);

static void link_node(tda_List *self, tda_ListNode *node, tda_ListNode *prev, tda_ListNode *next);

[[nodiscard]]
static tda_Status insert_between(tda_List *self, tda_ListNode *prev, tda_ListNode *next, const void *val);

static void unlink_node(tda_List *self, tda_ListNode *node);

static void remove_node(tda_List *self, tda_ListNode *node);

static void splice_nodes(tda_List *self, tda_List *src, bool front);

static void swap_contents(tda_List *a, tda_List *b);

/// the walk both find doors take. The node comes back mutable and the const door hands it
/// out as const: the walk is the same either way
[[nodiscard]]
static tda_ListNode *find_node(const tda_List *self, const void *key, tda_Eq eq);

static void clear_nodes(tda_List *self);

[[nodiscard]] [[maybe_unused]]
static bool owns_node(const tda_List *self, const tda_ListNode *node);

/// merges two chains linked through 'next' alone and returns the head of the result.
/// Equal elems keep 'a' before 'b', which is what makes the sort stable. 'prev' is left
/// wrong on purpose: relink_prev repairs it once, at the end, instead of on every step
[[nodiscard]]
static tda_ListNode *merge_chains(tda_ListNode *a, tda_ListNode *b, tda_Cmp cmp);

/// sorts a chain of 'len' nodes linked through 'next' alone and returns its new head
[[nodiscard]]
static tda_ListNode *sort_chain(tda_ListNode *head, size_t len, tda_Cmp cmp);

/// walks the list forward and rebuilds every 'prev' and the tail from the 'next' chain
static void relink_prev(tda_List *self);

/// merges 'src' into 'self' by relinking and leaves 'src' empty; both already sorted
static void merge_into(tda_List *self, tda_List *src, tda_Cmp cmp);

/* ========== lifetime ========== */

tda_Status tda_list_new(size_t elem_size, tda_Al *al, tda_List **out) {
    assert(elem_size > 0);
    assert(al);
    assert(out);

    tda_List *obj = tda_alloc(al, sizeof(tda_List));
    if (!obj) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    obj->head = nullptr;
    obj->tail = nullptr;
    obj->len = 0;
    obj->elem_size = elem_size;
    obj->al = al;

    ASSERT_LIST(obj);

    *out = obj;

    return TDA_STATUS_OK;
}

tda_Status tda_list_from_data(const void *data, size_t len, size_t elem_size, tda_Al *al, tda_List **out) {
    assert(data || len == 0);
    assert(elem_size > 0);
    assert(al);
    assert(out);

    tda_List *list;
    tda_Status st = tda_list_new(elem_size, al, &list);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    for (size_t i = 0; i < len; ++i) {
        st = tda_list_push_back(list, tda_byte_offset(data, elem_size, i));
        if (TDA_STATUS_IS_ERR(st)) {
            tda_list_drop(list);
            return st;
        }
    }

    *out = list;

    return TDA_STATUS_OK;
}

tda_Status tda_list_from_span(tda_Span s, tda_Al *al, tda_List **out) {
    TDA_SPAN_ASSERT(s);
    assert(al);
    assert(out);

    return tda_list_from_data(s.data, s.len, s.elem_size, al, out);
}

void tda_list_drop(tda_List *self) {
    if (!self) {
        return;
    }

    ASSERT_LIST(self);

    tda_Al *al_copy = self->al;
    clear_nodes(self);
    tda_dealloc(al_copy, self, sizeof(tda_List));
}

/* ========== copy ========== */

tda_Status tda_list_copy(const tda_List *self, tda_List **out) {
    ASSERT_LIST(self);

    return tda_list_copy_with(self, self->al, out);
}

tda_Status tda_list_copy_with(const tda_List *self, tda_Al *al, tda_List **out) {
    ASSERT_LIST(self);
    assert(al);
    assert(out);

    tda_List *obj;
    tda_Status st = tda_list_new(self->elem_size, al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    for (const tda_ListNode *node = self->head; node; node = node->next) {
        st = tda_list_push_back(obj, node->elem);
        if (TDA_STATUS_IS_ERR(st)) {
            tda_list_drop(obj);
            return st;
        }
    }

    *out = obj;

    return TDA_STATUS_OK;
}

tda_Status tda_list_copy_assign(tda_List *self, const tda_List *other) {
    ASSERT_LIST(self);
    ASSERT_LIST(other);
    TDA_EXPECT(self->elem_size == other->elem_size);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    const tda_ListNode *src = other->head;
    const tda_ListNode *dst = self->head;

    while (src && dst) {
        src = src->next;
        dst = dst->next;
    }

    // 'src' is the first elem the target has no node for; those nodes are
    // allocated up front, so the only failure happens before any mutation
    tda_List spare = {
        .head = nullptr,
        .tail = nullptr,
        .len = 0,
        .elem_size = self->elem_size,
        .al = self->al,
    };

    for (const tda_ListNode *node = src; node; node = node->next) {
        const tda_Status st = tda_list_push_back(&spare, node->elem);
        if (TDA_STATUS_IS_ERR(st)) {
            clear_nodes(&spare);
            return st;
        }
    }

    // from here on nothing can fail
    const tda_ListNode *from = other->head;
    for (tda_ListNode *to = self->head; to && from; to = to->next, from = from->next) {
        memcpy(to->elem, from->elem, self->elem_size);
    }

    while (self->len > other->len) {
        remove_node(self, self->tail);
    }

    if (spare.len > 0) {
        splice_nodes(self, &spare, false);
    }

    ASSERT_LIST(self);

    return TDA_STATUS_OK;
}

tda_Status tda_list_move_assign(tda_List *self, tda_List *other) {
    ASSERT_LIST(self);
    ASSERT_LIST(other);
    TDA_EXPECT(self->elem_size == other->elem_size);

    if (self == other) {
        return TDA_STATUS_OK;
    }

    // one allocator: the nodes change list without moving. What 'self' held ends up in 'other' and is released
    // there, through the very allocator that made it
    if (self->al == other->al) {
        TDA_SWAP(*self, *other);
        clear_nodes(other);

        ASSERT_LIST(self);
        ASSERT_LIST(other);

        return TDA_STATUS_OK;
    }

    // two allocators: the whole copy is built on the target's before anything of it is
    // touched, so a refusal leaves both as they were
    tda_List *obj;
    const tda_Status st = tda_list_copy_with(other, self->al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    TDA_SWAP(*self, *obj);
    tda_list_drop(obj);
    clear_nodes(other);

    ASSERT_LIST(self);
    ASSERT_LIST(other);

    return TDA_STATUS_OK;
}

/* ========== compare ========== */

bool tda_list_eq(const tda_List *a, const tda_List *b) {
    ASSERT_LIST(a);
    ASSERT_LIST(b);
    TDA_EXPECT(a->elem_size == b->elem_size);

    if (a == b) {
        return true;
    }

    if (a->len != b->len) {
        return false;
    }

    const tda_ListNode *x = a->head;
    const tda_ListNode *y = b->head;
    while (x) {
        if (memcmp(x->elem, y->elem, a->elem_size) != 0) {
            return false;
        }
        x = x->next;
        y = y->next;
    }

    return true;
}

bool tda_list_eq_by(const tda_List *a, const tda_List *b, tda_Eq eq) {
    ASSERT_LIST(a);
    ASSERT_LIST(b);
    TDA_EXPECT(a->elem_size == b->elem_size);
    assert(eq);

    if (a == b) {
        return true;
    }

    if (a->len != b->len) {
        return false;
    }

    const tda_ListNode *x = a->head;
    const tda_ListNode *y = b->head;
    while (x) {
        if (!eq(x->elem, y->elem)) {
            return false;
        }
        x = x->next;
        y = y->next;
    }

    return true;
}

/* ========== info ========== */

size_t tda_list_len(const tda_List *self) {
    ASSERT_LIST(self);

    return self->len;
}

size_t tda_list_elem_size(const tda_List *self) {
    ASSERT_LIST(self);

    return self->elem_size;
}

tda_Al *tda_list_al(const tda_List *self) {
    ASSERT_LIST(self);

    return self->al;
}

/* ========== access ========== */

const void *tda_list_front(const tda_List *self) {
    ASSERT_LIST(self);
    TDA_EXPECT(self->len > 0);

    return self->head->elem;
}

void *tda_list_front_mut(tda_List *self) {
    ASSERT_LIST(self);
    TDA_EXPECT(self->len > 0);

    return self->head->elem;
}

const void *tda_list_back(const tda_List *self) {
    ASSERT_LIST(self);
    TDA_EXPECT(self->len > 0);

    return self->tail->elem;
}

void *tda_list_back_mut(tda_List *self) {
    ASSERT_LIST(self);
    TDA_EXPECT(self->len > 0);

    return self->tail->elem;
}

/* ========== nodes ========== */

const tda_ListNode *tda_list_front_node(const tda_List *self) {
    ASSERT_LIST(self);

    return self->head;
}

tda_ListNode *tda_list_front_node_mut(tda_List *self) {
    ASSERT_LIST(self);

    return self->head;
}

const tda_ListNode *tda_list_back_node(const tda_List *self) {
    ASSERT_LIST(self);

    return self->tail;
}

tda_ListNode *tda_list_back_node_mut(tda_List *self) {
    ASSERT_LIST(self);

    return self->tail;
}

const tda_ListNode *tda_list_node_next(const tda_ListNode *node) {
    ASSERT_NODE(node);

    return node->next;
}

tda_ListNode *tda_list_node_next_mut(tda_ListNode *node) {
    ASSERT_NODE(node);

    return node->next;
}

const tda_ListNode *tda_list_node_prev(const tda_ListNode *node) {
    ASSERT_NODE(node);

    return node->prev;
}

tda_ListNode *tda_list_node_prev_mut(tda_ListNode *node) {
    ASSERT_NODE(node);

    return node->prev;
}

const tda_ListNode *tda_list_find(const tda_List *self, const void *key, tda_Eq eq) {
    ASSERT_LIST(self);
    assert(key);
    assert(eq);

    return find_node(self, key, eq);
}

tda_ListNode *tda_list_find_mut(tda_List *self, const void *key, tda_Eq eq) {
    ASSERT_LIST(self);
    assert(key);
    assert(eq);

    return find_node(self, key, eq);
}

const void *tda_list_node_elem(const tda_ListNode *node) {
    ASSERT_NODE(node);

    return node->elem;
}

void *tda_list_node_elem_mut(tda_ListNode *node) {
    ASSERT_NODE(node);

    return node->elem;
}

/* ========== mods ========== */

tda_Status tda_list_push_front(tda_List *self, const void *val) {
    ASSERT_LIST(self);
    assert(val);

    return insert_between(self, nullptr, self->head, val);
}

tda_Status tda_list_push_back(tda_List *self, const void *val) {
    ASSERT_LIST(self);
    assert(val);

    return insert_between(self, self->tail, nullptr, val);
}

void tda_list_pop_front(tda_List *self) {
    ASSERT_LIST(self);
    TDA_EXPECT(self->len > 0);

    remove_node(self, self->head);
}

void tda_list_pop_back(tda_List *self) {
    ASSERT_LIST(self);
    TDA_EXPECT(self->len > 0);

    remove_node(self, self->tail);
}

tda_Status tda_list_insert_before(tda_List *self, tda_ListNode *at, const void *val) {
    ASSERT_LIST(self);
    ASSERT_NODE(at);
    assert(owns_node(self, at));
    assert(val);

    return insert_between(self, at->prev, at, val);
}

tda_Status tda_list_insert_after(tda_List *self, tda_ListNode *at, const void *val) {
    ASSERT_LIST(self);
    ASSERT_NODE(at);
    assert(owns_node(self, at));
    assert(val);

    return insert_between(self, at, at->next, val);
}

void tda_list_remove(tda_List *self, tda_ListNode *node) {
    ASSERT_LIST(self);
    ASSERT_NODE(node);
    assert(owns_node(self, node));

    remove_node(self, node);
}

void tda_list_clear(tda_List *self) {
    ASSERT_LIST(self);

    clear_nodes(self);

    ASSERT_LIST(self);
}

tda_Status tda_list_splice_front(tda_List *self, tda_List *src) {
    ASSERT_LIST(self);
    ASSERT_LIST(src);
    TDA_EXPECT(self != src);
    TDA_EXPECT(self->elem_size == src->elem_size);

    if (src->len == 0) {
        return TDA_STATUS_OK;
    }

    if (self->al == src->al) {
        splice_nodes(self, src, true);
        return TDA_STATUS_OK;
    }

    tda_List *copy;
    const tda_Status st = tda_list_copy_with(src, self->al, &copy);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    splice_nodes(self, copy, true);
    tda_list_drop(copy);
    clear_nodes(src);

    ASSERT_LIST(self);
    ASSERT_LIST(src);

    return TDA_STATUS_OK;
}

tda_Status tda_list_splice_back(tda_List *self, tda_List *src) {
    ASSERT_LIST(self);
    ASSERT_LIST(src);
    TDA_EXPECT(self != src);
    TDA_EXPECT(self->elem_size == src->elem_size);

    if (src->len == 0) {
        return TDA_STATUS_OK;
    }

    if (self->al == src->al) {
        splice_nodes(self, src, false);
        return TDA_STATUS_OK;
    }

    tda_List *copy;
    const tda_Status st = tda_list_copy_with(src, self->al, &copy);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    splice_nodes(self, copy, false);
    tda_list_drop(copy);
    clear_nodes(src);

    ASSERT_LIST(self);
    ASSERT_LIST(src);

    return TDA_STATUS_OK;
}

void tda_list_swap(tda_List *self, tda_List *other) {
    ASSERT_LIST(self);
    ASSERT_LIST(other);
    TDA_EXPECT(self->elem_size == other->elem_size);
    TDA_EXPECT(self->al == other->al);

    if (self == other) {
        return;
    }

    swap_contents(self, other);

    ASSERT_LIST(self);
    ASSERT_LIST(other);
}

tda_Status tda_list_splice_node(tda_List *self, tda_ListNode *at, tda_List *src, tda_ListNode *node) {
    ASSERT_LIST(self);
    ASSERT_LIST(src);
    TDA_EXPECT(self->elem_size == src->elem_size);
    assert(node);
    assert(owns_node(src, node));
    assert(!at || owns_node(self, at));
    TDA_EXPECT(at != node);

    if (self->al != src->al) {
        // a node belongs to the allocator that made it, so it cannot change lists: the
        // elem is copied into a node of 'self' and the old one goes
        tda_ListNode *prev = at ? at->prev : self->tail;
        const tda_Status st = insert_between(self, prev, at, node->elem);
        if (TDA_STATUS_IS_ERR(st)) {
            return st;
        }

        remove_node(src, node);

        ASSERT_LIST(self);
        ASSERT_LIST(src);

        return TDA_STATUS_OK;
    }

    // unlinking first is what makes 'self == src' work: 'at->prev' is read from a list
    // that no longer holds 'node', so moving a node one step forward lands where it must
    unlink_node(src, node);
    link_node(self, node, at ? at->prev : self->tail, at);

    ASSERT_LIST(self);
    ASSERT_LIST(src);
    ASSERT_NODE(node);

    return TDA_STATUS_OK;
}

/* ========== relink ========== */

void tda_list_reverse(tda_List *self) {
    ASSERT_LIST(self);

    tda_ListNode *cur = self->head;
    while (cur) {
        tda_ListNode *next = cur->next;
        TDA_SWAP(cur->next, cur->prev);
        cur = next;
    }

    TDA_SWAP(self->head, self->tail);

    ASSERT_LIST(self);
}

void tda_list_sort(tda_List *self, tda_Cmp cmp) {
    ASSERT_LIST(self);
    assert(cmp);

    if (self->len < 2) {
        return;
    }

    self->head = sort_chain(self->head, self->len, cmp);
    relink_prev(self);

    ASSERT_LIST(self);
}

tda_Status tda_list_merge(tda_List *self, tda_List *src, tda_Cmp cmp) {
    ASSERT_LIST(self);
    ASSERT_LIST(src);
    TDA_EXPECT(self != src);
    TDA_EXPECT(self->elem_size == src->elem_size);
    assert(cmp);

    if (src->len == 0) {
        return TDA_STATUS_OK;
    }

    if (self->al == src->al) {
        merge_into(self, src, cmp);
        return TDA_STATUS_OK;
    }

    tda_List *copy;
    const tda_Status st = tda_list_copy_with(src, self->al, &copy);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    merge_into(self, copy, cmp);
    tda_list_drop(copy);
    clear_nodes(src);

    ASSERT_LIST(self);
    ASSERT_LIST(src);

    return TDA_STATUS_OK;
}

/* ========== copy to span ========== */

void tda_list_copy_to_span(const tda_List *self, tda_SpanMut dst) {
    ASSERT_LIST(self);
    TDA_SPAN_ASSERT(dst);
    TDA_EXPECT(dst.elem_size == self->elem_size);
    TDA_EXPECT(dst.len == self->len);

    size_t i = 0;
    for (const tda_ListNode *node = self->head; node; node = node->next, ++i) {
        memcpy(tda_byte_offset_mut(dst.data, self->elem_size, i), node->elem, self->elem_size);
    }
}

void tda_list_copy_from_span(tda_List *self, tda_Span src) {
    ASSERT_LIST(self);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(src.elem_size == self->elem_size);
    TDA_EXPECT(src.len == self->len);

    size_t i = 0;
    for (tda_ListNode *node = self->head; node; node = node->next, ++i) {
        memcpy(node->elem, tda_byte_offset(src.data, self->elem_size, i), self->elem_size);
    }
}

/* ========== print ========== */

void tda_list_fprint(const tda_List *self, FILE *stream, tda_FPrint fprint) {
    ASSERT_LIST(self);
    assert(stream);
    assert(fprint);

    fputc('[', stream);
    for (const tda_ListNode *node = self->head; node; node = node->next) {
        if (node != self->head) {
            fputs(", ", stream);
        }
        fprint(stream, node->elem);
    }
    fputs("]\n", stream);
}

void tda_list_print(const tda_List *self, tda_FPrint fprint) {
    ASSERT_LIST(self);
    assert(fprint);

    tda_list_fprint(self, stdout, fprint);
}

/* ========== internals ========== */

static size_t node_bytes(size_t elem_size) {
    return sizeof(tda_ListNode) + elem_size;
}

static tda_Status node_new(tda_Al *al, size_t elem_size, const void *val, tda_ListNode **out) {
    assert(al);
    assert(elem_size > 0);
    assert(val);
    assert(out);

    size_t bytes;
    if (ckd_add(&bytes, sizeof(tda_ListNode), elem_size)) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    tda_ListNode *node = tda_alloc(al, bytes);
    if (!node) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    assert(tda_ptr_is_aligned(node, alignof(max_align_t)));

    node->next = nullptr;
    node->prev = nullptr;
    memcpy(node->elem, val, elem_size);

    *out = node;

    return TDA_STATUS_OK;
}

static void node_drop(tda_Al *al, size_t elem_size, tda_ListNode *node) {
    tda_dealloc(al, node, node_bytes(elem_size));
}

static void link_node(tda_List *self, tda_ListNode *node, tda_ListNode *prev, tda_ListNode *next) {
    node->prev = prev;
    node->next = next;

    if (prev) {
        prev->next = node;
    } else {
        self->head = node;
    }

    if (next) {
        next->prev = node;
    } else {
        self->tail = node;
    }

    ++self->len;
}

// the whole of push_front/push_back/insert_before/insert_after: the four differ only in
// which pair of neighbours they hand over, and link_node already reads a null neighbour
// as "this end of the list"
static tda_Status insert_between(tda_List *self, tda_ListNode *prev, tda_ListNode *next, const void *val) {
    tda_ListNode *node;
    const tda_Status st = node_new(self->al, self->elem_size, val, &node);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    link_node(self, node, prev, next);

    ASSERT_LIST(self);
    ASSERT_NODE(node);

    return TDA_STATUS_OK;
}

static void unlink_node(tda_List *self, tda_ListNode *node) {
    ASSERT_NODE(node);
    assert(self->len > 0);

    if (node->prev) {
        node->prev->next = node->next;
    } else {
        self->head = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    } else {
        self->tail = node->prev;
    }

    --self->len;
}

static void remove_node(tda_List *self, tda_ListNode *node) {
    unlink_node(self, node);
    node_drop(self->al, self->elem_size, node);

    ASSERT_LIST(self);
}

static void splice_nodes(tda_List *self, tda_List *src, bool front) {
    assert(self->al == src->al);
    assert(self->elem_size == src->elem_size);
    assert(src->len > 0);

    if (self->len == 0) {
        self->head = src->head;
        self->tail = src->tail;
    } else if (front) {
        src->tail->next = self->head;
        self->head->prev = src->tail;
        self->head = src->head;
    } else {
        src->head->prev = self->tail;
        self->tail->next = src->head;
        self->tail = src->tail;
    }

    self->len += src->len;

    src->head = nullptr;
    src->tail = nullptr;
    src->len = 0;

    ASSERT_LIST(self);
    ASSERT_LIST(src);
}

static void swap_contents(tda_List *a, tda_List *b) {
    TDA_SWAP(a->head, b->head);
    TDA_SWAP(a->tail, b->tail);
    TDA_SWAP(a->len, b->len);
}

static tda_ListNode *find_node(const tda_List *self, const void *key, tda_Eq eq) {
    for (tda_ListNode *node = self->head; node; node = node->next) {
        if (eq(node->elem, key)) {
            return node;
        }
    }

    return nullptr;
}

static void clear_nodes(tda_List *self) {
    tda_ListNode *node = self->head;
    while (node) {
        tda_ListNode *next = node->next;
        node_drop(self->al, self->elem_size, node);
        node = next;
    }

    self->head = nullptr;
    self->tail = nullptr;
    self->len = 0;
}

static tda_ListNode *merge_chains(tda_ListNode *a, tda_ListNode *b, tda_Cmp cmp) {
    assert(cmp);

    tda_ListNode *head = nullptr;
    tda_ListNode **tail = &head;

    while (a && b) {
        if (cmp(a->elem, b->elem) <= 0) {
            *tail = a;
            a = a->next;
        } else {
            *tail = b;
            b = b->next;
        }
        tail = &(*tail)->next;
    }

    *tail = a ? a : b;

    return head;
}

static tda_ListNode *sort_chain(tda_ListNode *head, size_t len, tda_Cmp cmp) {
    assert(head);
    assert(cmp);

    if (len < 2) {
        return head;
    }

    const size_t half = len / 2;

    // walk to the LAST node of the left half, so the chain can be cut behind it
    tda_ListNode *left_tail = head;
    for (size_t i = 1; i < half; ++i) {
        left_tail = left_tail->next;
    }

    tda_ListNode *right = left_tail->next;
    left_tail->next = nullptr;

    return merge_chains(sort_chain(head, half, cmp), sort_chain(right, len - half, cmp), cmp);
}

static void relink_prev(tda_List *self) {
    tda_ListNode *prev = nullptr;

    for (tda_ListNode *node = self->head; node; node = node->next) {
        node->prev = prev;
        prev = node;
    }

    self->tail = prev;
}

static void merge_into(tda_List *self, tda_List *src, tda_Cmp cmp) {
    assert(self->al == src->al);
    assert(self->elem_size == src->elem_size);
    assert(src->len > 0);

    self->head = merge_chains(self->head, src->head, cmp);
    self->len += src->len;
    relink_prev(self);

    src->head = nullptr;
    src->tail = nullptr;
    src->len = 0;

    ASSERT_LIST(self);
    ASSERT_LIST(src);
}

static bool owns_node(const tda_List *self, const tda_ListNode *node) {
    for (const tda_ListNode *cur = self->head; cur; cur = cur->next) {
        if (cur == node) {
            return true;
        }
    }
    return false;
}
