#ifndef CORESTACK_READER_H
#define CORESTACK_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SPAN_ELEM_TYPE unsigned char
#define SPAN_TYPEDEF ReaderFrame
#include "collection/span.h"

#define RING_BUFFER_ELEM_TYPE ReaderFrame
#define RING_BUFFER_TYPEDEF ReaderFrameQueue
#include "collection/ring_buffer.h"

typedef struct FrameReader {
    ReaderFrameQueue queue;
    ReaderFrame frame;
    size_t max_frames_allowed;
    size_t max_frames_left;
    uint32_t used;
    enum {
        READER_IDLE,
        READER_LEN,
        READER_MALLOC,
        READER_BODY,
    } state;
    uint8_t len_buf[sizeof(uint32_t)];
} Reader;

int reader_init(Reader* reader, size_t available);
int reader_recv(Reader* reader, int fd);
void reader_free(Reader* reader);

#endif
