#ifndef CORESTACK_SERVER_LOG_BUF_H
#define CORESTACK_SERVER_LOG_BUF_H

#include <stddef.h>

#define LOG_BUF_CAPACITY 512

typedef struct LogBuf {
    char* buffer[LOG_BUF_CAPACITY];
    size_t buffer_head;
    size_t buffer_tail;
    size_t buffer_size;
    size_t dropped;
} LogBuf;

void log_buf_init(LogBuf* c);
void log_buf_free(LogBuf* c);
char* log_buf_pop_log(LogBuf* c);
int log_buf_append_log(LogBuf* c, char* string);

#endif