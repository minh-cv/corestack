#include "log_buf.h"
#include <assert.h>
#include <stdlib.h>

void log_buf_init(LogBuf *c) {
    c->buffer_head = 0;
    c->buffer_tail = 0;
    c->buffer_size = 0;
    c->dropped = 0;
}

void log_buf_free(LogBuf* c) {
    while (c->buffer_size > 0) {
        free(log_buf_pop_log(c));
    }
}

int log_buf_append_log(LogBuf* c, char* string) {
    if (c->buffer_size == LOG_BUF_CAPACITY) {
        free(string);
        c->dropped++;
        return -1;
    }

    c->buffer[c->buffer_tail] = string;
    c->buffer_tail = (c->buffer_tail + 1) % LOG_BUF_CAPACITY;
    c->buffer_size++;
    return 0;
}

char* log_buf_pop_log(LogBuf* c) {
    assert(c->buffer_size > 0);
    char* retval = c->buffer[c->buffer_head];
    c->buffer_head = (c->buffer_head + 1) % LOG_BUF_CAPACITY;
    c->buffer_size--;
    return retval;
}
