#include "network/reader.h"
#include "logger.h"
#include "wire.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>

int reader_init(Reader* reader, size_t cap) {
    memset(reader, 0, sizeof(*reader));
    return ReaderFrameQueue_init(&reader->queue, cap);
}

static int reader_recv_frame(Reader* c, ReaderFrame* frame, int fd) {
    for (;;) {
        ssize_t n = recv(fd,
                            (unsigned char*)frame->ptr + c->used,
                            frame->length - c->used,
                            MSG_NOSIGNAL);

        if (n == 0) {
            return -1;
        }
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        c->used += (uint32_t)n;

        assert(c->used <= frame->length);
        if (c->used == frame->length) {
            return 1;
        }
    }
}

int reader_recv(Reader* reader, int fd) {    
    if (reader->state == READER_IDLE) {
        const size_t reader_frame_queue_size = ReaderFrameQueue_size(&reader->queue);
        const size_t reader_frame_queue_capacity = ReaderFrameQueue_capacity(&reader->queue);

        assert(reader_frame_queue_capacity >= reader_frame_queue_size);
        
        const size_t reader_frames_left = reader_frame_queue_capacity - reader_frame_queue_size;

        reader->max_frames_left = reader->max_frames_allowed < reader_frames_left ? reader->max_frames_allowed : reader_frames_left;
    }

    for (;;) {
        switch (reader->state) {
        case READER_IDLE: {
            if (reader->max_frames_left == 0) {
                return 0;
            }
            reader->state = READER_LEN;
            reader->used = 0;
            reader->max_frames_left--;
            break;
        }
        case READER_LEN: {
            ReaderFrame frame = {
                reader->len_buf,
                sizeof(reader->len_buf),
            };
            int val = reader_recv_frame(reader, &frame, fd);
            if (val == 0) {
                return 0;
            }
            if (val == -1) {
                return -1;
            }
            reader->frame.length = decode_u32_be(reader->len_buf);
            if (reader->frame.length == 0 || reader->frame.length > FRAME_MAX) {
                LOGGER_LOG(LOG_INFO, "client", "client fd=%d send message of invalid length %zu, closing", fd, reader->frame.length);
                return -1;
            }
            reader->state = READER_MALLOC;
            reader->used = 0;
            break;
        }
        case READER_MALLOC:
            reader->frame.ptr = malloc(reader->frame.length);
            if (reader->frame.ptr == NULL) {
                return 0;
            }
            reader->state = READER_BODY;
            break;
        case READER_BODY: {
            int val = reader_recv_frame(reader, &reader->frame, fd);
            if (val == 0) {
                return 0;
            }
            if (val == -1) {
                free(reader->frame.ptr);
                reader->frame.ptr = NULL;
                return -1;
            }
            int err = ReaderFrameQueue_push_back(&reader->queue, &reader->frame);
            assert(err != -1);
            reader->frame.ptr = NULL;

            reader->state = READER_IDLE;
            break;
        }
        }
    }
}

void reader_free(Reader *reader) {
    free(reader->frame.ptr);
    reader->frame.ptr = NULL;
    ReaderFrameQueue_free(&reader->queue);
}
