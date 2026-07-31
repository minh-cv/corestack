#ifndef SPAN_ELEM_TYPE
#error "SPAN_ELEM_TYPE not defined"
#else

#ifndef SPAN_TYPEDEF
#error "SPAN_TYPEDEF not defined"
#endif

#include <stddef.h>

typedef struct {
    SPAN_ELEM_TYPE* ptr;
    size_t length;
} SPAN_TYPEDEF;

#undef SPAN_ELEM_TYPE
#undef SPAN_TYPEDEF

#endif
