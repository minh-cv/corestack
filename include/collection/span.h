#ifndef SPAN_ELEM_TYPE
#error "SPAN_ELEM_TYPE not defined"
#else

#ifndef SPAN_TYPEDEF
#error "SPAN_TYPEDEF not defined"
#endif

#include <stddef.h>
#include <assert.h>
#include "macro_utility.h"
#include <stdlib.h>

typedef struct {
    SPAN_ELEM_TYPE* ptr;
    size_t length;
} SPAN_TYPEDEF;

MAYBE_UNUSED static SPAN_ELEM_TYPE* CONCAT(SPAN_TYPEDEF, _at)(SPAN_TYPEDEF* span, size_t idx) {
    assert(idx < span->length);
    return span->ptr + idx;
}

MAYBE_UNUSED static int CONCAT(SPAN_TYPEDEF, _calloc)(SPAN_TYPEDEF* span, size_t count) {
    SPAN_ELEM_TYPE* ptr = calloc(count, sizeof(*span->ptr));
    if (ptr == NULL) {
        return -1;
    }
    span->ptr = ptr;
    span->length = count;
    return 0;
}

#undef SPAN_ELEM_TYPE
#undef SPAN_TYPEDEF

#endif
