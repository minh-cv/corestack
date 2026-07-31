#ifndef RING_BUFFER_ELEM_TYPE
#error "RING_BUFFER_ELEM_TYPE not defined"
#else

#ifndef RING_BUFFER_TYPEDEF
#error "RING_BUFFER_TYPEDEF not defined"
#else

#include "../macro_utility.h"
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>

/* ring_buffer.h instantiates its own IDeque backing, reserving the name
 * <RING_BUFFER_TYPEDEF>_IDeque for it - the caller doesn't include ideque.h separately.
 * <RING_BUFFER_TYPEDEF>_IDeque is a real typedef once ideque.h finishes, so the rest of this
 * file just recomputes the same token via CONCAT to reference it; it doesn't need
 * IDEQUE_TYPEDEF itself to stay defined (ideque.h undefs its own params at the end). */
#define IDEQUE_ELEM_TYPE RING_BUFFER_ELEM_TYPE
#define IDEQUE_TYPEDEF CONCAT(RING_BUFFER_TYPEDEF, _IDeque)
#include "ideque.h"

#define RING_BUFFER_IDEQUE CONCAT(RING_BUFFER_TYPEDEF, _IDeque)

/* The public <RING_BUFFER_TYPEDEF>_<method> functions below act on RING_BUFFER_TYPEDEF* directly
 * (no upcast needed - that's the whole point of calling them by their concrete type). The
 * IDeque vtable, however, is only ever called through IDeque(base)* pointers, so it needs
 * separate adapter functions with that exact signature; those live under a fixed
 * "_ideque_<method>_impl_<hash>" name (mirroring DTOR_WRAPPER_NAME's fixed-hash-suffixed
 * wrapper names in dtor.h) to keep them clearly marked as generated glue, distinct from the
 * public API above. */
#define RING_BUFFER_IDEQUE_IMPL(method) \
    CONCAT(CONCAT(RING_BUFFER_TYPEDEF, _ideque_), CONCAT(method, _impl_c39a5de2b1879dc073ff35b6623578a728e2d958c6bc522fd7aea7a3a80261b4))

typedef struct RING_BUFFER_TYPEDEF RING_BUFFER_TYPEDEF;

struct RING_BUFFER_TYPEDEF {
    RING_BUFFER_IDEQUE base;
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

static bool CONCAT(RING_BUFFER_TYPEDEF, _empty)(const RING_BUFFER_TYPEDEF* rb) {
    return !rb->full && rb->head == rb->tail;
}

static int CONCAT(RING_BUFFER_TYPEDEF, _push_front)(RING_BUFFER_TYPEDEF* rb, RING_BUFFER_ELEM_TYPE* elem) {
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

static int CONCAT(RING_BUFFER_TYPEDEF, _push_back)(RING_BUFFER_TYPEDEF* rb, RING_BUFFER_ELEM_TYPE* elem) {
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

static int CONCAT(RING_BUFFER_TYPEDEF, _pop_front)(RING_BUFFER_TYPEDEF* rb) {
    if (CONCAT(RING_BUFFER_TYPEDEF, _empty)(rb)) {
        return -1;
    }
    rb->head = (rb->head + 1) % RING_BUFFER_CAPACITY_IMPL(rb);
    rb->full = false;
    return 0;
}

static int CONCAT(RING_BUFFER_TYPEDEF, _pop_back)(RING_BUFFER_TYPEDEF* rb) {
    if (CONCAT(RING_BUFFER_TYPEDEF, _empty)(rb)) {
        return -1;
    }
    rb->tail = (rb->tail + RING_BUFFER_CAPACITY_IMPL(rb) - 1) % RING_BUFFER_CAPACITY_IMPL(rb);
    rb->full = false;
    return 0;
}

static RING_BUFFER_ELEM_TYPE* CONCAT(RING_BUFFER_TYPEDEF, _front)(const RING_BUFFER_TYPEDEF* rb) {
    if (CONCAT(RING_BUFFER_TYPEDEF, _empty)(rb)) {
        return NULL;
    }
    return (RING_BUFFER_ELEM_TYPE*)&rb->data[rb->head];
}

static RING_BUFFER_ELEM_TYPE* CONCAT(RING_BUFFER_TYPEDEF, _back)(const RING_BUFFER_TYPEDEF* rb) {
    if (CONCAT(RING_BUFFER_TYPEDEF, _empty)(rb)) {
        return NULL;
    }
    return (RING_BUFFER_ELEM_TYPE*)&rb->data[(rb->tail + RING_BUFFER_CAPACITY_IMPL(rb) - 1) % RING_BUFFER_CAPACITY_IMPL(rb)];
}

static size_t CONCAT(RING_BUFFER_TYPEDEF, _size)(const RING_BUFFER_TYPEDEF* rb) {
    if (rb->full) {
        return RING_BUFFER_CAPACITY_IMPL(rb);
    }
    return (rb->tail + RING_BUFFER_CAPACITY_IMPL(rb) - rb->head) % RING_BUFFER_CAPACITY_IMPL(rb);
}

static size_t CONCAT(RING_BUFFER_TYPEDEF, _capacity)(const RING_BUFFER_TYPEDEF* rb) {
    (void)rb;
    return RING_BUFFER_CAPACITY_IMPL(rb);
}

static RING_BUFFER_TYPEDEF* RING_BUFFER_IDEQUE_IMPL(upcast)(RING_BUFFER_IDEQUE* base) {
    return (RING_BUFFER_TYPEDEF*)((char*)base - offsetof(RING_BUFFER_TYPEDEF, base));
}

static const RING_BUFFER_TYPEDEF* RING_BUFFER_IDEQUE_IMPL(upcast_const)(const RING_BUFFER_IDEQUE* base) {
    return (const RING_BUFFER_TYPEDEF*)((const char*)base - offsetof(RING_BUFFER_TYPEDEF, base));
}

static int RING_BUFFER_IDEQUE_IMPL(push_front)(RING_BUFFER_IDEQUE* base, RING_BUFFER_ELEM_TYPE* elem) {
    return CONCAT(RING_BUFFER_TYPEDEF, _push_front)(RING_BUFFER_IDEQUE_IMPL(upcast)(base), elem);
}

static int RING_BUFFER_IDEQUE_IMPL(push_back)(RING_BUFFER_IDEQUE* base, RING_BUFFER_ELEM_TYPE* elem) {
    return CONCAT(RING_BUFFER_TYPEDEF, _push_back)(RING_BUFFER_IDEQUE_IMPL(upcast)(base), elem);
}

static int RING_BUFFER_IDEQUE_IMPL(pop_front)(RING_BUFFER_IDEQUE* base) {
    return CONCAT(RING_BUFFER_TYPEDEF, _pop_front)(RING_BUFFER_IDEQUE_IMPL(upcast)(base));
}

static int RING_BUFFER_IDEQUE_IMPL(pop_back)(RING_BUFFER_IDEQUE* base) {
    return CONCAT(RING_BUFFER_TYPEDEF, _pop_back)(RING_BUFFER_IDEQUE_IMPL(upcast)(base));
}

static RING_BUFFER_ELEM_TYPE* RING_BUFFER_IDEQUE_IMPL(front)(const RING_BUFFER_IDEQUE* base) {
    return CONCAT(RING_BUFFER_TYPEDEF, _front)(RING_BUFFER_IDEQUE_IMPL(upcast_const)(base));
}

static RING_BUFFER_ELEM_TYPE* RING_BUFFER_IDEQUE_IMPL(back)(const RING_BUFFER_IDEQUE* base) {
    return CONCAT(RING_BUFFER_TYPEDEF, _back)(RING_BUFFER_IDEQUE_IMPL(upcast_const)(base));
}

static size_t RING_BUFFER_IDEQUE_IMPL(size)(const RING_BUFFER_IDEQUE* base) {
    return CONCAT(RING_BUFFER_TYPEDEF, _size)(RING_BUFFER_IDEQUE_IMPL(upcast_const)(base));
}

static size_t RING_BUFFER_IDEQUE_IMPL(capacity)(const RING_BUFFER_IDEQUE* base) {
    return CONCAT(RING_BUFFER_TYPEDEF, _capacity)(RING_BUFFER_IDEQUE_IMPL(upcast_const)(base));
}

static const CONCAT(RING_BUFFER_IDEQUE, _vtable) CONCAT(RING_BUFFER_TYPEDEF, _vtable) = {
    .push_front = RING_BUFFER_IDEQUE_IMPL(push_front),
    .push_back = RING_BUFFER_IDEQUE_IMPL(push_back),
    .pop_front = RING_BUFFER_IDEQUE_IMPL(pop_front),
    .pop_back = RING_BUFFER_IDEQUE_IMPL(pop_back),
    .front = RING_BUFFER_IDEQUE_IMPL(front),
    .back = RING_BUFFER_IDEQUE_IMPL(back),
    .size = RING_BUFFER_IDEQUE_IMPL(size),
    .capacity = RING_BUFFER_IDEQUE_IMPL(capacity),
};

#ifdef RING_BUFFER_CAPACITY
static void CONCAT(RING_BUFFER_TYPEDEF, _init)(RING_BUFFER_TYPEDEF* rb) {
    rb->base.vptr = &CONCAT(RING_BUFFER_TYPEDEF, _vtable);
    rb->head = 0;
    rb->tail = 0;
    rb->full = false;
}
#else
static int CONCAT(RING_BUFFER_TYPEDEF, _init)(RING_BUFFER_TYPEDEF* rb, size_t capacity) {
    rb->base.vptr = &CONCAT(RING_BUFFER_TYPEDEF, _vtable);
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

static void CONCAT(RING_BUFFER_TYPEDEF, _free)(RING_BUFFER_TYPEDEF* rb) {
    assert(rb->data != NULL);
    free((void*)rb->data);
    rb->data = NULL;
}

#endif

#undef RING_BUFFER_ELEM_TYPE
#undef RING_BUFFER_CAPACITY
#undef RING_BUFFER_TYPEDEF
#undef RING_BUFFER_IDEQUE
#undef RING_BUFFER_IDEQUE_IMPL
#undef RING_BUFFER_CAPACITY_IMPL

#endif
#endif
