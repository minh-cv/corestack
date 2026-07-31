#ifndef RING_BUFFER_ELEM_TYPE
#error "RING_BUFFER_ELEM_TYPE not defined"
#else

#ifndef RING_BUFFER_TYPEDEF
#error "RING_BUFFER_TYPEDEF not defined"
#else

#include "macro_utility.h"
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>

typedef struct RING_BUFFER_TYPEDEF RING_BUFFER_TYPEDEF;

struct RING_BUFFER_TYPEDEF {
    #ifdef RING_BUFFER_CAPACITY
    RING_BUFFER_ELEM_TYPE data[RING_BUFFER_CAPACITY];
    #else
    RING_BUFFER_ELEM_TYPE* data;
    size_t capacity;
    #endif
    size_t head;
    size_t tail;
    bool full;
};

#ifdef RING_BUFFER_CAPACITY
#define RING_BUFFER_CAPACITY_IMPL(obj) RING_BUFFER_CAPACITY
#else
#define RING_BUFFER_CAPACITY_IMPL(obj) (obj->capacity)
#endif

MAYBE_UNUSED static bool CONCAT(RING_BUFFER_TYPEDEF, _empty)(RING_BUFFER_TYPEDEF const* rb) {
    return !rb->full && rb->head == rb->tail;
}

MAYBE_UNUSED static int CONCAT(RING_BUFFER_TYPEDEF, _push_front)(RING_BUFFER_TYPEDEF* rb, RING_BUFFER_ELEM_TYPE* elem) {
    if (rb->full) {
        return -1;
    }
    rb->head = (rb->head + RING_BUFFER_CAPACITY_IMPL(rb) - 1) % RING_BUFFER_CAPACITY_IMPL(rb);
    rb->data[rb->head] = *elem;
    if (rb->head == rb->tail) {
        rb->full = true;
    }
    return 0;
}

MAYBE_UNUSED static int CONCAT(RING_BUFFER_TYPEDEF, _push_back)(RING_BUFFER_TYPEDEF* rb, RING_BUFFER_ELEM_TYPE const * elem) {
    if (rb->full) {
        return -1;
    }
    rb->data[rb->tail] = *elem;
    rb->tail = (rb->tail + 1) % RING_BUFFER_CAPACITY_IMPL(rb);
    if (rb->head == rb->tail) {
        rb->full = true;
    }
    return 0;
}

MAYBE_UNUSED static int CONCAT(RING_BUFFER_TYPEDEF, _pop_front)(RING_BUFFER_TYPEDEF* rb) {
    if (CONCAT(RING_BUFFER_TYPEDEF, _empty)(rb)) {
        return -1;
    }
    rb->head = (rb->head + 1) % RING_BUFFER_CAPACITY_IMPL(rb);
    rb->full = false;
    return 0;
}

MAYBE_UNUSED static int CONCAT(RING_BUFFER_TYPEDEF, _pop_back)(RING_BUFFER_TYPEDEF* rb) {
    if (CONCAT(RING_BUFFER_TYPEDEF, _empty)(rb)) {
        return -1;
    }
    rb->tail = (rb->tail + RING_BUFFER_CAPACITY_IMPL(rb) - 1) % RING_BUFFER_CAPACITY_IMPL(rb);
    rb->full = false;
    return 0;
}

MAYBE_UNUSED static RING_BUFFER_ELEM_TYPE* CONCAT(RING_BUFFER_TYPEDEF, _front)(RING_BUFFER_TYPEDEF const* rb) {
    if (CONCAT(RING_BUFFER_TYPEDEF, _empty)(rb)) {
        return NULL;
    }
    return (RING_BUFFER_ELEM_TYPE*)&rb->data[rb->head];
}

MAYBE_UNUSED static RING_BUFFER_ELEM_TYPE* CONCAT(RING_BUFFER_TYPEDEF, _back)(RING_BUFFER_TYPEDEF const* rb) {
    if (CONCAT(RING_BUFFER_TYPEDEF, _empty)(rb)) {
        return NULL;
    }
    return (RING_BUFFER_ELEM_TYPE*)&rb->data[(rb->tail + RING_BUFFER_CAPACITY_IMPL(rb) - 1) % RING_BUFFER_CAPACITY_IMPL(rb)];
}

MAYBE_UNUSED static size_t CONCAT(RING_BUFFER_TYPEDEF, _size)(RING_BUFFER_TYPEDEF const* rb) {
    if (rb->full) {
        return RING_BUFFER_CAPACITY_IMPL(rb);
    }
    return (rb->tail + RING_BUFFER_CAPACITY_IMPL(rb) - rb->head) % RING_BUFFER_CAPACITY_IMPL(rb);
}

MAYBE_UNUSED static size_t CONCAT(RING_BUFFER_TYPEDEF, _capacity)(RING_BUFFER_TYPEDEF const* rb) {
    (void)rb;
    return RING_BUFFER_CAPACITY_IMPL(rb);
}

#ifdef RING_BUFFER_CAPACITY
MAYBE_UNUSED static void CONCAT(RING_BUFFER_TYPEDEF, _init)(RING_BUFFER_TYPEDEF* rb) {
    rb->head = 0;
    rb->tail = 0;   
    rb->full = false;
}
#else
MAYBE_UNUSED static int CONCAT(RING_BUFFER_TYPEDEF, _init)(RING_BUFFER_TYPEDEF* rb, size_t capacity) {
    if (capacity == 0) {
        assert(false);
        return -1;
    }
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->full = false;
    rb->data = malloc(capacity*sizeof(RING_BUFFER_ELEM_TYPE));
    if (rb->data == NULL) {
        return -1;
    }
    return 0;
}

MAYBE_UNUSED static void CONCAT(RING_BUFFER_TYPEDEF, _free)(RING_BUFFER_TYPEDEF* rb) {
    assert(rb->data != NULL);
    free((void*)rb->data);
    rb->data = NULL;
}

#endif

#undef RING_BUFFER_ELEM_TYPE
#undef RING_BUFFER_CAPACITY
#undef RING_BUFFER_TYPEDEF
#undef RING_BUFFER_CAPACITY_IMPL

#endif
#endif
