#ifndef CORESTACK_WRITER_H
#define CORESTACK_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SPAN_ELEM_TYPE const unsigned char
#define SPAN_TYPEDEF WriterFrame
#include "collection/span.h"

#define RING_BUFFER_ELEM_TYPE WriterFrame
#define RING_BUFFER_TYPEDEF WriterFrameQueue
#include "collection/ring_buffer.h"

typedef struct FrameWriter {
    WriterFrame frame;
    size_t max_frames_allowed;
    size_t max_frames_left;
    uint32_t used;
    enum {
        WRITER_IDLE,
        WRITER_LEN,
        WRITER_BODY,
    } state;
    uint8_t len_buf[sizeof(uint32_t)];
} Writer;

void writer_init(Writer* writer);
int writer_send(Writer* writer, int fd, WriterFrameQueue* queue);
void writer_free(Writer* writer);

#endif
