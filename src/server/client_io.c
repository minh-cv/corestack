#include "client_io.h"
#include "tetrissh.h"

#include <assert.h>
#include <sched.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

void frame_free(struct ClientIoFrame* frame) {
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

void client_io_pop_frame(struct ClientIo* c, unsigned int count) {
    assert(count <= c->frame_count);
    for (; count > 0; count--) {
        c->frame_count--;
        frame_free(&c->frame[c->frame_count]);
    }
}

void client_io_push_frame(struct ClientIo* c, const struct ClientIoFrame frames[], unsigned int count) {
    assert(count <= CLIENT_IO_MAX_FRAME - c->frame_count);
    for (unsigned int i = 0; i < count; i++) {
        c->frame[c->frame_count] = frames[count - i - 1];
        c->frame[c->frame_count].len_used = 0;
        c->frame[c->frame_count].used = 0;
        c->frame_count++;
    }
}

void client_io_transit_state(struct ClientIo* c, enum ClientIoState write_state, unsigned int frame_active) {
    c->state = write_state;
    c->frame_active = frame_active;

    if (write_state == CLIENT_READING_LEN) {
        assert(frame_active <= CLIENT_IO_MAX_FRAME - c->frame_count);
        // not very relevant but eh
        memset(c->frame + c->frame_count, 0, sizeof(struct ClientIoFrame)*frame_active);
    }
    else if (write_state == CLIENT_WRITING) {
        assert(frame_active <= c->frame_count);
    }
    else if (write_state == CLIENT_READING_BODY) {

    }
    else {
        assert(frame_active == 0);
    }
}

struct ClientIoFrame* client_io_get_top_frame(struct ClientIo* c) {
    assert(0 < c->frame_count && c->frame_count <= CLIENT_IO_MAX_FRAME);
    struct ClientIoFrame* frame = &c->frame[c->frame_count - 1];
    assert(frame->buf != NULL);
    return &c->frame[c->frame_count - 1];
}

struct ClientIoFrame* client_io_get_top_unused_frame(struct ClientIo* c) {
    assert(c->frame_count < CLIENT_IO_MAX_FRAME);
    return &c->frame[c->frame_count];
}

int mod_epoll_events(int epoll_fd, int fd, uint32_t events) {
    struct epoll_event ev = {0};
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
}

void client_io_free(struct ClientIo *c) {
    assert(c != NULL);
    client_io_pop_frame(c, c->frame_count);
}

void client_io_init(struct ClientIo *c, int client_fd) {
    memset(c, 0, sizeof(*c));
    c->fd = client_fd;
}

static enum ClientIoResult handle_read_len(struct ClientIo *c) {
    struct ClientIoFrame *f = client_io_get_top_unused_frame(c);
    for (;;) {
        ssize_t n = recv(c->fd,
                            f->len_buf + f->len_used,
                            sizeof(f->len_buf) - f->len_used,
                            0);

        if (n == 0) {
            return CLIENT_IO_ERR;
        }
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return CLIENT_IO_OK;
            }
            if (errno == EINTR) {
                continue;
            }
            return CLIENT_IO_ERR;
        }

        f->len_used += (uint32_t)n;

        if (f->len_used == sizeof(uint32_t)) {
            break;
        }
    }
    struct ClientIoFrame new_frame = *f;
    f = &new_frame;
    f->is_heap_allocated = true;

    f->len = decode_u32_be(f->len_buf);

    if (f->len == 0 || f->len > FRAME_MAX) {
        fprintf(stderr, "invalid message length from fd %d: %u\n",
                c->fd, f->len);
        return CLIENT_IO_ERR;
    }
    
    f->buf = malloc(f->len);
    if (f->buf == NULL) {
        return CLIENT_IO_ERR;
    }

    client_io_push_frame(c, &new_frame, 1);
    client_io_transit_state(c, CLIENT_READING_BODY, c->frame_active);

    return CLIENT_IO_CONTINUE;
}

static enum ClientIoResult handle_read_body(struct ClientIo* c) {
    struct ClientIoFrame* f = client_io_get_top_frame(c);
    for (;;) {
        ssize_t n = recv(c->fd,
                            f->buf + f->used,
                            f->len - f->used,
                            0);

        if (n == 0) {
            return CLIENT_IO_ERR;
        }
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return CLIENT_IO_OK;
            }
            if (errno == EINTR) {
                continue;
            }
            return CLIENT_IO_ERR;
        }

        f->used += (uint32_t)n;

        assert(f->used <= f->len);
        if (f->used == f->len) {
            break;
        }
    }

    c->frame_active--;
    if (0 != c->frame_active) {
        client_io_transit_state(c, CLIENT_READING_LEN, c->frame_active);
    }
    else {
        c->state = CLIENT_READ_TRANSIT;
    }

    return CLIENT_IO_CONTINUE;
}

enum ClientIoResult handle_write(struct ClientIo *c) {
    while (c->frame_active > 0) {
        struct ClientIoFrame *f = client_io_get_top_frame(c);

        while (f->len_used < sizeof(f->len_buf)) {
            ssize_t n = send(c->fd,
                             f->len_buf + f->len_used,
                             sizeof(f->len_buf) - f->len_used,
                             0);

            if (n == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return CLIENT_IO_OK;
                }
                if (errno == EINTR) {
                    continue;
                }
                return CLIENT_IO_ERR;
            }

            f->len_used += (uint32_t)n;
        }

        while (f->used < f->len) {
            ssize_t n = send(c->fd,
                             f->buf + f->used,
                             f->len - f->used,
                             0);

            if (n == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return CLIENT_IO_OK;
                }
                if (errno == EINTR) {
                    continue;
                }
                return CLIENT_IO_ERR;
            }

            f->used += (uint32_t)n;
        }
        client_io_pop_frame(c, 1);
        c->frame_active--;
    }
    
    c->state = CLIENT_WRITE_TRANSIT;
    return CLIENT_IO_CONTINUE;
}

enum ClientIoResult client_io_generic_entry(int epoll_fd, struct ClientIo* c, ClientIoResult (*transist_read)(int epoll_fd, struct ClientIo* c), ClientIoResult (*transist_write)(int epoll_fd, struct ClientIo* c)) {
    switch (c->state) {
    case CLIENT_READING_LEN:
        return handle_read_len(c);
    case CLIENT_READING_BODY:
        return handle_read_body(c);
    case CLIENT_READ_TRANSIT:
        return transist_read(epoll_fd, c);
    case CLIENT_WRITING:
        return handle_write(c);
    case CLIENT_WRITE_TRANSIT:
        return transist_write(epoll_fd, c);
    default:
        assert(false);
        return CLIENT_IO_ERR;
    }
}