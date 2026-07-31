#ifndef IDEQUE_ELEM_TYPE
#error "IDEQUE_ELEM_TYPE not defined"
#else

#ifndef IDEQUE_TYPEDEF
#error "IDEQUE_TYPEDEF not defined"
#else

#include "../macro_utility.h"
#include <stddef.h>

/* IDEQUE_TYPEDEF is used verbatim as the whole struct/typedef name (no derived prefix). Every
 * other generated symbol is named mechanically as <IDEQUE_TYPEDEF>_<info>, e.g. the vtable type
 * is <IDEQUE_TYPEDEF>_vtable and the push_back function is <IDEQUE_TYPEDEF>_push_back - callers
 * spell these names out directly, there's no access-macro layer. */

typedef struct IDEQUE_TYPEDEF IDEQUE_TYPEDEF;

typedef struct CONCAT(IDEQUE_TYPEDEF, _vtable) {
    int (*push_front)(IDEQUE_TYPEDEF*, IDEQUE_ELEM_TYPE*);
    int (*push_back)(IDEQUE_TYPEDEF*, IDEQUE_ELEM_TYPE*);
    int (*pop_front)(IDEQUE_TYPEDEF*);
    int (*pop_back)(IDEQUE_TYPEDEF*);
    IDEQUE_ELEM_TYPE* (*front)(const IDEQUE_TYPEDEF*);
    IDEQUE_ELEM_TYPE* (*back)(const IDEQUE_TYPEDEF*);
    size_t (*size)(const IDEQUE_TYPEDEF*);
    size_t (*capacity)(const IDEQUE_TYPEDEF*);
} CONCAT(IDEQUE_TYPEDEF, _vtable);

struct IDEQUE_TYPEDEF {
    const CONCAT(IDEQUE_TYPEDEF, _vtable)* vptr;
};

static int CONCAT(IDEQUE_TYPEDEF, _push_front)(IDEQUE_TYPEDEF* base, IDEQUE_ELEM_TYPE* elem_type) {
    return base->vptr->push_front(base, elem_type);
}

static int CONCAT(IDEQUE_TYPEDEF, _push_back)(IDEQUE_TYPEDEF* base, IDEQUE_ELEM_TYPE* elem_type) {
    return base->vptr->push_back(base, elem_type);
}

static int CONCAT(IDEQUE_TYPEDEF, _pop_front)(IDEQUE_TYPEDEF* base) {
    return base->vptr->pop_front(base);
}

static int CONCAT(IDEQUE_TYPEDEF, _pop_back)(IDEQUE_TYPEDEF* base) {
    return base->vptr->pop_back(base);
}

static IDEQUE_ELEM_TYPE* CONCAT(IDEQUE_TYPEDEF, _front)(const IDEQUE_TYPEDEF* base) {
    return base->vptr->front(base);
}

static IDEQUE_ELEM_TYPE* CONCAT(IDEQUE_TYPEDEF, _back)(const IDEQUE_TYPEDEF* base) {
    return base->vptr->back(base);
}

static size_t CONCAT(IDEQUE_TYPEDEF, _size)(const IDEQUE_TYPEDEF* base) {
    return base->vptr->size(base);
}

static size_t CONCAT(IDEQUE_TYPEDEF, _capacity)(const IDEQUE_TYPEDEF* base) {
    return base->vptr->capacity(base);
}

#undef IDEQUE_ELEM_TYPE
#undef IDEQUE_TYPEDEF

#endif
#endif
