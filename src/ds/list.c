#include "tda/ds/list.h"

#include "tda/core/check.h"
#include "tda/core/util.h"

#include "internal/order.h"
#include "internal/ptr.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

/* ========== internals ========== */

#define ASSERT_LIST(l)                                 \
    (assert(l),                                        \
     assert(tda_layout_is_type((l)->elem)),            \
     assert((l)->al),                                  \
     assert(((l)->len > 0) == ((l)->head != nullptr)), \
     assert(((l)->head != nullptr) == ((l)->tail != nullptr)))

#define ASSERT_NODE(n)                             \
    (assert(n),                                    \
     assert(!(n)->prev || (n)->prev->next == (n)), \
     assert(!(n)->next || (n)->next->prev == (n)))

// the elem follows the two links, at the first offset its alignment allows; only the
// list knows that alignment, so the elem is reached through elem_of
struct tda_ListNode {
    tda_ListNode *next;
    tda_ListNode *prev;
};

struct tda_List {
    tda_ListNode *head;
    tda_ListNode *tail;
    size_t len;
    tda_Layout elem;
    tda_Al *al;
};

/// where a node's elem starts: past the links, on the elem's alignment
[[nodiscard]]
static size_t elem_offset(tda_Layout elem);

/// the links and the elem after them, padded so a pool of them tiles; true when the
/// size overflows
[[nodiscard]]
static bool ckd_node_layout(tda_Layout *out, tda_Layout elem);

/// ckd_node_layout for a list already built, which checked it fits
[[nodiscard]]
static tda_Layout node_layout(const tda_List *self);

[[nodiscard]]
static const void *elem_of(const tda_List *self, const tda_ListNode *node);

[[nodiscard]]
static void *elem_of_mut(const tda_List *self, tda_ListNode *node);

[[nodiscard]]
static tda_Status node_new(const tda_List *self, tda_Al *al, const void *val, tda_ListNode **out);

static void node_drop(const tda_List *self, tda_Al *al, tda_ListNode *node);

/// links the chain 'first'..'last' of 'count' nodes between 'prev' and 'next'; a null
/// neighbour stands for that end of the list
static void link_chain(
    tda_List *self,
    tda_ListNode *first,
    tda_ListNode *last,
    size_t count,
    tda_ListNode *prev,
    tda_ListNode *next
);

/// link_chain for one node
static void link_node(tda_List *self, tda_ListNode *node, tda_ListNode *prev, tda_ListNode *next);

[[nodiscard]]
static tda_Status insert_between(tda_List *self, tda_ListNode *prev, tda_ListNode *next, const void *val);

static void unlink_node(tda_List *self, tda_ListNode *node);

static void remove_node(tda_List *self, tda_ListNode *node);

static void splice_nodes(tda_List *self, tda_List *src, bool front);

/// where the nodes for 'self' come from: 'src' itself on one allocator, otherwise a copy
/// of it on the allocator of 'self', since a node belongs to the allocator that made it
[[nodiscard]]
static tda_Status nodes_for(tda_List *self, tda_List *src, tda_List **out);

/// after the nodes of 'from' went into 'self': a copy is dropped and 'src' emptied
static void release_source(tda_List *src, tda_List *from);

/// splice_front and splice_back
[[nodiscard]]
static tda_Status splice_impl(tda_List *self, tda_List *src, bool front);

/// the walk behind eq and eq_by; a null 'eq' compares bytes
[[nodiscard]]
static bool eq_impl(const tda_List *a, const tda_List *b, tda_Eq eq, void *ctx);

/// forgets every node without freeing any; they were moved or freed already
static void set_empty(tda_List *self);

static void swap_contents(tda_List *a, tda_List *b);

/// the walk shared by find and find_mut; the const one hands the node out as const
[[nodiscard]]
static tda_ListNode *find_node(const tda_List *self, const void *key, tda_Eq eq, void *ctx);

static void clear_nodes(tda_List *self);

[[nodiscard]] [[maybe_unused]]
static bool owns_node(const tda_List *self, const tda_ListNode *node);

/// merges two chains linked through 'next' alone and returns the head of the result.
/// Equal elems keep 'a' before 'b', which makes the sort stable. 'prev' is left stale on
/// purpose: relink_prev repairs it once at the end, not on every step
[[nodiscard]]
static tda_ListNode *merge_chains(const tda_List *self, tda_ListNode *a, tda_ListNode *b, tda_Cmp cmp, void *ctx);

/// sorts a chain of 'len' nodes linked through 'next' alone and returns its new head
[[nodiscard]]
static tda_ListNode *sort_chain(const tda_List *self, tda_ListNode *head, size_t len, tda_Cmp cmp, void *ctx);

/// walks the list forward and rebuilds every 'prev' and the tail from the 'next' chain
static void relink_prev(tda_List *self);

/// merges 'src' into 'self' by relinking and leaves 'src' empty; both already sorted
static void merge_into(tda_List *self, tda_List *src, tda_Cmp cmp, void *ctx);

/* ========== lifetime ========== */

tda_Status tda_list_new(tda_Layout elem, tda_Al *al, tda_List **out) {
    assert(tda_layout_is_type(elem));
    assert(al);
    assert(out);

    // checked once here, so node_layout needs no check of its own
    tda_Layout node;
    if (ckd_node_layout(&node, elem)) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    tda_List *obj = TDA_ALLOC(tda_List, al, 1);
    if (!obj) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    obj->head = nullptr;
    obj->tail = nullptr;
    obj->len = 0;
    obj->elem = elem;
    obj->al = al;

    ASSERT_LIST(obj);

    *out = obj;

    return TDA_STATUS_OK;
}

tda_Status tda_list_from_data(const void *data, size_t len, tda_Layout elem, tda_Al *al, tda_List **out) {
    assert(data || len == 0);
    assert(tda_layout_is_type(elem));
    assert(al);
    assert(out);

    tda_List *list;
    tda_Status st = tda_list_new(elem, al, &list);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    for (size_t i = 0; i < len; ++i) {
        st = tda_list_push_back(list, tda_byte_offset(data, elem.size, i));
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

    return tda_list_from_data(s.data, s.len, s.elem, al, out);
}

void tda_list_drop(tda_List *self) {
    if (!self) {
        return;
    }

    ASSERT_LIST(self);

    tda_Al *al_copy = self->al;
    clear_nodes(self);
    TDA_DEALLOC(tda_List, al_copy, self, 1);
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
    tda_Status st = tda_list_new(self->elem, al, &obj);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    for (const tda_ListNode *node = self->head; node; node = node->next) {
        st = tda_list_push_back(obj, elem_of(self, node));
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
    TDA_EXPECT(tda_layout_eq(self->elem, other->elem));

    if (self == other) {
        return TDA_STATUS_OK;
    }

    const tda_ListNode *src = other->head;
    const tda_ListNode *dst = self->head;

    while (src && dst) {
        src = src->next;
        dst = dst->next;
    }

    // 'src' is the first elem with no node in the target; those nodes are allocated up
    // front, so a failure happens before any mutation
    tda_List spare = {
        .head = nullptr,
        .tail = nullptr,
        .len = 0,
        .elem = self->elem,
        .al = self->al,
    };

    for (const tda_ListNode *node = src; node; node = node->next) {
        const tda_Status st = tda_list_push_back(&spare, elem_of(other, node));
        if (TDA_STATUS_IS_ERR(st)) {
            clear_nodes(&spare);
            return st;
        }
    }

    // from here on nothing can fail
    const tda_ListNode *from = other->head;
    for (tda_ListNode *to = self->head; to && from; to = to->next, from = from->next) {
        memcpy(elem_of_mut(self, to), elem_of(other, from), self->elem.size);
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
    TDA_EXPECT(tda_layout_eq(self->elem, other->elem));

    if (self == other) {
        return TDA_STATUS_OK;
    }

    // one allocator: swap (no node moves), then release what 'self' held from 'other'
    if (self->al == other->al) {
        TDA_SWAP(*self, *other);
        clear_nodes(other);

        ASSERT_LIST(self);
        ASSERT_LIST(other);

        return TDA_STATUS_OK;
    }

    // two allocators: build the copy first, so a refusal changes nothing
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
    TDA_EXPECT(tda_layout_eq(a->elem, b->elem));

    return eq_impl(a, b, nullptr, nullptr);
}

bool tda_list_eq_by(const tda_List *a, const tda_List *b, tda_Eq eq, void *ctx) {
    ASSERT_LIST(a);
    ASSERT_LIST(b);
    TDA_EXPECT(tda_layout_eq(a->elem, b->elem));
    assert(eq);

    return eq_impl(a, b, eq, ctx);
}

/* ========== info ========== */

size_t tda_list_len(const tda_List *self) {
    ASSERT_LIST(self);

    return self->len;
}

size_t tda_list_elem_size(const tda_List *self) {
    ASSERT_LIST(self);

    return self->elem.size;
}

tda_Layout tda_list_elem_layout(const tda_List *self) {
    ASSERT_LIST(self);

    return self->elem;
}

tda_Al *tda_list_al(const tda_List *self) {
    ASSERT_LIST(self);

    return self->al;
}

tda_Layout tda_list_block_layout(tda_Layout elem) {
    assert(tda_layout_is_type(elem));

    tda_Layout node;
    [[maybe_unused]] const bool overflow = ckd_node_layout(&node, elem);
    assert(!overflow); // asserted to be one tda_list_new accepts

    return (tda_Layout){
        .size = node.size > sizeof(tda_List) ? node.size : sizeof(tda_List),
        .align = node.align > alignof(tda_List) ? node.align : alignof(tda_List),
    };
}

/* ========== access ========== */

const void *tda_list_front(const tda_List *self) {
    ASSERT_LIST(self);
    TDA_EXPECT(self->len > 0);

    return elem_of(self, self->head);
}

void *tda_list_front_mut(tda_List *self) {
    ASSERT_LIST(self);
    TDA_EXPECT(self->len > 0);

    return elem_of_mut(self, self->head);
}

const void *tda_list_back(const tda_List *self) {
    ASSERT_LIST(self);
    TDA_EXPECT(self->len > 0);

    return elem_of(self, self->tail);
}

void *tda_list_back_mut(tda_List *self) {
    ASSERT_LIST(self);
    TDA_EXPECT(self->len > 0);

    return elem_of_mut(self, self->tail);
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

const tda_ListNode *tda_list_find(const tda_List *self, const void *key, tda_Eq eq, void *ctx) {
    ASSERT_LIST(self);
    assert(key);
    assert(eq);

    return find_node(self, key, eq, ctx);
}

tda_ListNode *tda_list_find_mut(tda_List *self, const void *key, tda_Eq eq, void *ctx) {
    ASSERT_LIST(self);
    assert(key);
    assert(eq);

    return find_node(self, key, eq, ctx);
}

const void *tda_list_node_elem(const tda_List *self, const tda_ListNode *node) {
    ASSERT_LIST(self);
    ASSERT_NODE(node);

    return elem_of(self, node);
}

void *tda_list_node_elem_mut(tda_List *self, tda_ListNode *node) {
    ASSERT_LIST(self);
    ASSERT_NODE(node);

    return elem_of_mut(self, node);
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

void tda_list_retain(tda_List *self, tda_Pred pred, void *ctx) {
    ASSERT_LIST(self);
    assert(pred);

    for (tda_ListNode *node = self->head, *next; node; node = next) {
        next = node->next;
        if (!pred(elem_of(self, node), ctx)) {
            remove_node(self, node);
        }
    }
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
    TDA_EXPECT(tda_layout_eq(self->elem, src->elem));

    return splice_impl(self, src, true);
}

tda_Status tda_list_splice_back(tda_List *self, tda_List *src) {
    ASSERT_LIST(self);
    ASSERT_LIST(src);
    TDA_EXPECT(self != src);
    TDA_EXPECT(tda_layout_eq(self->elem, src->elem));

    return splice_impl(self, src, false);
}

void tda_list_swap(tda_List *self, tda_List *other) {
    ASSERT_LIST(self);
    ASSERT_LIST(other);
    TDA_EXPECT(tda_layout_eq(self->elem, other->elem));
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
    TDA_EXPECT(tda_layout_eq(self->elem, src->elem));
    assert(node);
    assert(owns_node(src, node));
    assert(!at || owns_node(self, at));
    TDA_EXPECT(at != node);

    if (self->al != src->al) {
        // a node belongs to the allocator that made it, so copy the elem into a new node
        // of 'self' and release the old one
        tda_ListNode *prev = at ? at->prev : self->tail;
        const tda_Status st = insert_between(self, prev, at, elem_of(src, node));
        if (TDA_STATUS_IS_ERR(st)) {
            return st;
        }

        remove_node(src, node);

        ASSERT_LIST(self);
        ASSERT_LIST(src);

        return TDA_STATUS_OK;
    }

    // unlink first so 'self == src' works: 'at->prev' is then read from a list without
    // 'node', so moving a node one step forward lands in the right place
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

void tda_list_sort(tda_List *self, tda_Cmp cmp, void *ctx) {
    ASSERT_LIST(self);
    assert(cmp);

    if (self->len < 2) {
        return;
    }

    self->head = sort_chain(self, self->head, self->len, cmp, ctx);
    relink_prev(self);

    ASSERT_LIST(self);
}

tda_Status tda_list_merge(tda_List *self, tda_List *src, tda_Cmp cmp, void *ctx) {
    ASSERT_LIST(self);
    ASSERT_LIST(src);
    TDA_EXPECT(self != src);
    TDA_EXPECT(tda_layout_eq(self->elem, src->elem));
    assert(cmp);

    if (src->len == 0) {
        return TDA_STATUS_OK;
    }

    tda_List *from;
    const tda_Status st = nodes_for(self, src, &from);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    merge_into(self, from, cmp, ctx);
    release_source(src, from);

    ASSERT_LIST(self);
    ASSERT_LIST(src);

    return TDA_STATUS_OK;
}

/* ========== copy to span ========== */

void tda_list_copy_to_span(const tda_List *self, tda_SpanMut dst) {
    ASSERT_LIST(self);
    TDA_SPAN_ASSERT(dst);
    TDA_EXPECT(tda_layout_eq(dst.elem, self->elem));
    TDA_EXPECT(dst.len == self->len);

    size_t i = 0;
    for (const tda_ListNode *node = self->head; node; node = node->next, ++i) {
        memcpy(tda_byte_offset_mut(dst.data, self->elem.size, i), elem_of(self, node), self->elem.size);
    }
}

void tda_list_copy_from_span(tda_List *self, tda_Span src) {
    ASSERT_LIST(self);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(tda_layout_eq(src.elem, self->elem));
    TDA_EXPECT(src.len == self->len);

    size_t i = 0;
    for (tda_ListNode *node = self->head; node; node = node->next, ++i) {
        memcpy(elem_of_mut(self, node), tda_byte_offset(src.data, self->elem.size, i), self->elem.size);
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
        fprint(stream, elem_of(self, node));
    }
    fputs("]\n", stream);
}

void tda_list_print(const tda_List *self, tda_FPrint fprint) {
    ASSERT_LIST(self);
    assert(fprint);

    tda_list_fprint(self, stdout, fprint);
}

/* ========== internals ========== */

static size_t elem_offset(tda_Layout elem) {
    return tda_align_up(sizeof(tda_ListNode), elem.align);
}

static bool ckd_node_layout(tda_Layout *out, tda_Layout elem) {
    *out = TDA_LAYOUT_OF(tda_ListNode);
    size_t offset;

    return tda_layout_ckd_extend(out, elem, &offset) || tda_layout_ckd_pad(out);
}

static tda_Layout node_layout(const tda_List *self) {
    tda_Layout node;
    [[maybe_unused]] const bool overflow = ckd_node_layout(&node, self->elem);
    assert(!overflow); // tda_list_new refuses the rest

    return node;
}

static const void *elem_of(const tda_List *self, const tda_ListNode *node) {
    return tda_byte_offset(node, 1, elem_offset(self->elem));
}

static void *elem_of_mut(const tda_List *self, tda_ListNode *node) {
    return tda_byte_offset_mut(node, 1, elem_offset(self->elem));
}

static tda_Status node_new(const tda_List *self, tda_Al *al, const void *val, tda_ListNode **out) {
    assert(al);
    assert(val);
    assert(out);

    tda_ListNode *node = tda_alloc(al, node_layout(self));
    if (!node) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    node->next = nullptr;
    node->prev = nullptr;
    memcpy(elem_of_mut(self, node), val, self->elem.size);

    *out = node;

    return TDA_STATUS_OK;
}

static void node_drop(const tda_List *self, tda_Al *al, tda_ListNode *node) {
    tda_dealloc(al, node, node_layout(self));
}

static void link_chain(
    tda_List *self,
    tda_ListNode *first,
    tda_ListNode *last,
    size_t count,
    tda_ListNode *prev,
    tda_ListNode *next
) {
    first->prev = prev;
    last->next = next;

    if (prev) {
        prev->next = first;
    } else {
        self->head = first;
    }

    if (next) {
        next->prev = last;
    } else {
        self->tail = last;
    }

    self->len += count;
}

static void link_node(tda_List *self, tda_ListNode *node, tda_ListNode *prev, tda_ListNode *next) {
    link_chain(self, node, node, 1, prev, next);
}

// backs push_front/push_back/insert_before/insert_after, which differ only in the
// neighbours they pass; link_node reads a null neighbour as that end of the list
static tda_Status insert_between(tda_List *self, tda_ListNode *prev, tda_ListNode *next, const void *val) {
    tda_ListNode *node;
    const tda_Status st = node_new(self, self->al, val, &node);
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
    node_drop(self, self->al, node);

    ASSERT_LIST(self);
}

static void splice_nodes(tda_List *self, tda_List *src, bool front) {
    assert(self->al == src->al);
    assert(tda_layout_eq(self->elem, src->elem));
    assert(src->len > 0);

    tda_ListNode *prev = front ? nullptr : self->tail;
    tda_ListNode *next = front ? self->head : nullptr;
    link_chain(self, src->head, src->tail, src->len, prev, next);
    set_empty(src);

    ASSERT_LIST(self);
    ASSERT_LIST(src);
}

static tda_Status nodes_for(tda_List *self, tda_List *src, tda_List **out) {
    if (self->al == src->al) {
        *out = src;
        return TDA_STATUS_OK;
    }

    return tda_list_copy_with(src, self->al, out);
}

static void release_source(tda_List *src, tda_List *from) {
    if (from != src) {
        tda_list_drop(from);
        clear_nodes(src);
    }
}

static tda_Status splice_impl(tda_List *self, tda_List *src, bool front) {
    if (src->len == 0) {
        return TDA_STATUS_OK;
    }

    tda_List *from;
    const tda_Status st = nodes_for(self, src, &from);
    if (TDA_STATUS_IS_ERR(st)) {
        return st;
    }

    splice_nodes(self, from, front);
    release_source(src, from);

    ASSERT_LIST(self);
    ASSERT_LIST(src);

    return TDA_STATUS_OK;
}

static bool eq_impl(const tda_List *a, const tda_List *b, tda_Eq eq, void *ctx) {
    if (a == b) {
        return true;
    }

    if (a->len != b->len) {
        return false;
    }

    for (const tda_ListNode *x = a->head, *y = b->head; x; x = x->next, y = y->next) {
        const void *ex = elem_of(a, x);
        const void *ey = elem_of(b, y);
        if (eq ? !eq(ex, ey, ctx) : memcmp(ex, ey, a->elem.size) != 0) {
            return false;
        }
    }

    return true;
}

static void set_empty(tda_List *self) {
    self->head = nullptr;
    self->tail = nullptr;
    self->len = 0;
}

static void swap_contents(tda_List *a, tda_List *b) {
    TDA_SWAP(a->head, b->head);
    TDA_SWAP(a->tail, b->tail);
    TDA_SWAP(a->len, b->len);
}

static tda_ListNode *find_node(const tda_List *self, const void *key, tda_Eq eq, void *ctx) {
    for (tda_ListNode *node = self->head; node; node = node->next) {
        if (eq(elem_of(self, node), key, ctx)) {
            return node;
        }
    }

    return nullptr;
}

static void clear_nodes(tda_List *self) {
    tda_ListNode *node = self->head;
    while (node) {
        tda_ListNode *next = node->next;
        node_drop(self, self->al, node);
        node = next;
    }

    set_empty(self);
}

static tda_ListNode *merge_chains(const tda_List *self, tda_ListNode *a, tda_ListNode *b, tda_Cmp cmp, void *ctx) {
    assert(cmp);

    tda_ListNode *head = nullptr;
    tda_ListNode **tail = &head;

    while (a && b) {
        if (!tda_right_goes_first(cmp, ctx, elem_of(self, a), elem_of(self, b))) {
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

static tda_ListNode *sort_chain(const tda_List *self, tda_ListNode *head, size_t len, tda_Cmp cmp, void *ctx) {
    assert(head);
    assert(cmp);

    if (len < 2) {
        return head;
    }

    const size_t half = len / 2;

    // walk to the last node of the left half, so the chain can be cut after it
    tda_ListNode *left_tail = head;
    for (size_t i = 1; i < half; ++i) {
        left_tail = left_tail->next;
    }

    tda_ListNode *right = left_tail->next;
    left_tail->next = nullptr;

    tda_ListNode *left_sorted = sort_chain(self, head, half, cmp, ctx);
    tda_ListNode *right_sorted = sort_chain(self, right, len - half, cmp, ctx);

    return merge_chains(self, left_sorted, right_sorted, cmp, ctx);
}

static void relink_prev(tda_List *self) {
    tda_ListNode *prev = nullptr;

    for (tda_ListNode *node = self->head; node; node = node->next) {
        node->prev = prev;
        prev = node;
    }

    self->tail = prev;
}

static void merge_into(tda_List *self, tda_List *src, tda_Cmp cmp, void *ctx) {
    assert(self->al == src->al);
    assert(tda_layout_eq(self->elem, src->elem));
    assert(src->len > 0);

    self->head = merge_chains(self, self->head, src->head, cmp, ctx);
    self->len += src->len;
    relink_prev(self);

    set_empty(src);

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
