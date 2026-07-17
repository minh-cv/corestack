#ifndef TETRISH_TETRISD_CLIENT_IO_H
#define TETRISH_TETRISD_CLIENT_IO_H

#include <stdbool.h>
#include <stdint.h>

typedef enum ClientIoState {
    CLIENT_READING_LEN,
    CLIENT_READING_BODY,
    CLIENT_READ_TRANSIT,
    CLIENT_WRITING,
    CLIENT_WRITE_TRANSIT,
} ClientIoState;

typedef struct ClientIoFrame {
    uint8_t len_buf[4];
    uint32_t len_used;

    unsigned char* buf;
    uint32_t len;
    uint32_t used;

    bool is_heap_allocated;
} ClientIoFrame;

#define CLIENT_IO_MAX_FRAME 5

struct ClientIo;

typedef enum ClientIoResult {
    CLIENT_IO_ERR,
    CLIENT_IO_OK,
    CLIENT_IO_CONTINUE,
    CLIENT_IO_CLOSE, // for manual closing
    CLIENT_IO_YIELD, // for extra operations that needs control from the server
} ClientIoResult;

typedef struct ClientIo {
    int fd;
    enum ClientIoState state;

    //! @brief a stack-like object containing frame, tracked by `frame_count`
    ClientIoFrame frame[CLIENT_IO_MAX_FRAME];

    //! @brief the number of frame objects currently available
    unsigned int frame_count;

    //! @brief the number of frame objects left to read/write from
    unsigned int frame_active;
} ClientIo;

void frame_free(struct ClientIoFrame* frame);
void client_io_pop_frame(struct ClientIo* c, unsigned int count);
void client_io_push_frame(struct ClientIo* c, const struct ClientIoFrame frames[], unsigned int count);
void client_io_transit_state(struct ClientIo* c, enum ClientIoState write_state, unsigned int frame_active);
struct ClientIoFrame* client_io_get_top_frame(struct ClientIo* c);
struct ClientIoFrame* client_io_get_top_unused_frame(struct ClientIo* c);
void client_io_free(struct ClientIo *c);
void client_io_init(struct ClientIo *c, int client_fd);
enum ClientIoResult client_io_generic_entry(int epoll_fd, struct ClientIo* c, ClientIoResult (*transist_read)(int epoll_fd, struct ClientIo* c), ClientIoResult (*transist_write)(int epoll_fd, struct ClientIo* c));
int mod_epoll_events(int epoll_fd, int fd, uint32_t events);
#endif