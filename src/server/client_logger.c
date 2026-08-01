#include "client_logger.h"
#include "client_io.h"
#include "log_buf.h"
#include "wire.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

void client_logger_init(ClientLogger* c, LogBuf* buf, int client_fd) {
    c->buf = buf;
    client_io_init(&c->base, client_fd);
    client_io_transit_state(&c->base, CLIENT_WRITE_TRANSIT, 0);
}

void client_logger_free(ClientLogger *c) {
    c->buf->dropped += c->base.frame_count;
    client_io_free(&c->base);
}

ClientIoResult client_logger_transist_write(struct ClientIo *c_base) {
    ClientLogger* c = (ClientLogger*)((char*)c_base - offsetof(ClientLogger, base));

    if (c->buf->buffer_size == 0) {
        client_io_transit_state(c_base, CLIENT_WRITE_TRANSIT, 0);
        return CLIENT_IO_WOULDBLOCK;
    }

    unsigned int count = c->buf->buffer_size < CLIENT_IO_MAX_FRAME - c_base->frame_count ? (unsigned int)c->buf->buffer_size : CLIENT_IO_MAX_FRAME - c_base->frame_count;

    if (count == 0) {
        return CLIENT_IO_WOULDBLOCK;
    }
    
    struct ClientIoFrame frames[CLIENT_IO_MAX_FRAME] = {0};
    for (unsigned int i = 0; i < count; i++) {
        char* line = log_buf_pop_log(c->buf);
        frames[i].buf = (unsigned char*)line;
        frames[i].len = (uint32_t)strlen(line);
        frames[i].is_heap_allocated = true;
        encode_u32_be(frames[i].len_buf, frames[i].len);
    }

    client_io_push_frame(c_base, frames, count);
    client_io_transit_state(c_base, CLIENT_WRITING, count);
    return CLIENT_IO_CONTINUE;
}