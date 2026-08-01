#include "network/writer.h"
#include "logger.h"
#include "wire.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>

void writer_init(Writer* writer) {
    memset(writer, 0, sizeof(*writer));
}

int writer_write_frame(Writer* writer, const WriterFrame* frame, int fd) {
    while (writer->used < frame->length) {
        ssize_t n = send(fd,
                            (const unsigned char*)frame->ptr + writer->used,
                            frame->length - writer->used,
                            MSG_NOSIGNAL);

        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        writer->used += (uint32_t)n;
    }
    return 1;
}

int writer_send(Writer* writer, int fd, WriterFrameQueue* queue) {
    assert(fd != -1);
    
    if (writer->state == WRITER_IDLE) {        
        const size_t writer_frames_left = WriterFrameQueue_size(queue);

        writer->max_frames_left = writer->max_frames_allowed < writer_frames_left ? writer->max_frames_allowed : writer_frames_left;
    }

    for (;;) {
        switch (writer->state) {
        case WRITER_IDLE: {
            if (writer->max_frames_left == 0) {
                return 0;
            }
            {
                assert(WriterFrameQueue_size(queue) != 0);
                writer->frame = *WriterFrameQueue_front(queue);
                writer->max_frames_left--;
                int val = WriterFrameQueue_pop_front(queue);
                assert(val != -1);
            }
            if (writer->frame.length == 0 || writer->frame.length > FRAME_MAX) {
                assert(false && "Pushing side must validate length beforehand");
                if (writer->frame.length > FRAME_MAX) {
                    LOGGER_LOG(LOG_ERROR, "writer", "Message of length %zu discarded.", writer->frame.length);
                }
                else {
                    LOGGER_LOG(LOG_ERROR, "writer", "Empty message discarded");
                }

                free((void *)writer->frame.ptr);
                writer->frame.ptr = NULL;
                continue;
            }
            writer->state = WRITER_LEN;
            writer->used = 0;
            encode_u32_be(writer->len_buf, (uint32_t)writer->frame.length);
            
            break;
        }
        case WRITER_LEN: {
            const WriterFrame frame = {
                writer->len_buf,
                sizeof(writer->len_buf),
            };

            int val = writer_write_frame(writer, &frame, fd);
            if (val == 0) {
                return 0;
            }
            if (val == -1) {
                free((void*)writer->frame.ptr);
                writer->frame.ptr = NULL;
                return -1;
            }
            writer->state = WRITER_BODY;
            writer->used = 0;
            break;
        }
        case WRITER_BODY: {
            int val = writer_write_frame(writer, &writer->frame, fd);
            if (val == 0) {
                return 0;
            }
            free((void*)writer->frame.ptr);
            writer->frame.ptr = NULL;
            if (val == -1) {                
                return -1;
            }
            writer->state = WRITER_IDLE;
            break;
        }
        }
    }
}

void writer_free(Writer *writer) {
    free((void*)writer->frame.ptr);
    writer->frame.ptr = NULL;
}
