#include "client_io.h"

#include <assert.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>

void frame_free(struct frame* frame) {
    if (frame->is_heap_allocated) {
        #ifndef NDEBUG
        assert(frame->buf != NULL);
        free(frame->buf);
        frame->buf = NULL;
        #else
        free(frame->buf);
        #endif
    }
}

void client_io_pop_frame(struct client_io* c, unsigned int count) {
    assert(count <= c->frame_count);
    for (; count > 0; count--) {
        c->frame_count--;
        frame_free(&c->frame[c->frame_count]);
    }
}

void client_io_push_frame(struct client_io* c, const struct frame frames[], unsigned int count) {
    assert(count <= CLIENT_IO_MAX_FRAME - c->frame_count);
    for (unsigned int i = 0; i < count; i++) {
        c->frame[c->frame_count] = frames[count - i - 1];
        c->frame[c->frame_count].len_used = 0;
        c->frame[c->frame_count].used = 0;
        c->frame_count++;
    }
}

void client_io_transit_state(struct client_io* c, enum client_io_state write_state, unsigned int frame_active) {
    // lift this assert when there is need for extend to, although I can't see the need
    assert(write_state != CLIENT_READING_BODY);
    c->state = write_state;
    c->frame_active = frame_active;

    if (write_state == CLIENT_READING_LEN) {
        assert(frame_active <= CLIENT_IO_MAX_FRAME - c->frame_count);
        memset(c->frame + c->frame_count, 0, sizeof(struct frame)*frame_active);
    }
    else {
        assert(frame_active <= c->frame_count);
    }
}

struct frame* client_io_get_top_frame(struct client_io* c) {
    assert(0 < c->frame_count && c->frame_count <= CLIENT_IO_MAX_FRAME);
    struct frame* frame = &c->frame[c->frame_count - 1];
    assert(frame->buf != NULL);
    return &c->frame[c->frame_count - 1];
}