#ifndef TETRISH_TETRISD_CLIENT_IO_H
#define TETRISH_TETRISD_CLIENT_IO_H

#include <stdbool.h>
#include <stdint.h>

enum client_io_state {
    CLIENT_READING_LEN,
    CLIENT_READING_BODY,
    CLIENT_WRITING,
};

typedef struct frame {
    uint8_t len_buf[4];
    uint32_t len_used;

    unsigned char* buf;
    uint32_t len;
    uint32_t used;

    bool is_heap_allocated;
} frame;

#define CLIENT_IO_MAX_FRAME 5

struct client_io {
    int fd;
    enum client_io_state state;

    //! @brief a stack-like object containing frame, tracked by `frame_count`
    frame frame[CLIENT_IO_MAX_FRAME];

    //! @brief the number of frame objects currently available
    unsigned int frame_count;

    //! @brief the number of frame objects left to read/write from
    unsigned int frame_active;
};

void frame_free(struct frame* frame);
void client_io_pop_frame(struct client_io* c, unsigned int count);
void client_io_push_frame(struct client_io* c, const struct frame frames[], unsigned int count);
void client_io_transit_state(struct client_io* c, enum client_io_state write_state, unsigned int frame_active);
struct frame* client_io_get_top_frame(struct client_io* c);

#endif