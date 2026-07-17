#include "client_auth.h"
#include "client_io.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/epoll.h>

static int server_auth_nonce(struct ClientIo* c, TetrishCredential* credential) {
    assert(c->frame_count == 1);

    struct ClientIoFrame frames[2] = {0};
    struct ClientIoFrame* frame_nonce = &frames[0];

    frame_nonce->is_heap_allocated = true;
    struct ClientIoFrame* f = client_io_get_top_frame(c);
    if ((frame_nonce->buf = tetrish_server_sign_nonce(f->buf, f->len, credential->private_key, &frame_nonce->len)) == NULL) {
        return -1;
    }
    client_io_pop_frame(c, 1);
    encode_u32_be(frame_nonce->len_buf, frame_nonce->len);

    struct ClientIoFrame frame_auth = {0};
    frame_auth.is_heap_allocated = false;
    frame_auth.buf = credential->certificate;
    frame_auth.len = credential->certificate_len;
    encode_u32_be(frame_auth.len_buf, frame_auth.len);

    frames[1] = frame_auth;

    client_io_push_frame(c, frames, sizeof(frames)/sizeof(struct ClientIoFrame));
    return 0;
}

static int server_auth_session_key(struct ClientIo* c, TetrishCredential* credential, SessionKey* session_key) {
    assert(c->frame_count == 1);

    struct ClientIoFrame* f = client_io_get_top_frame(c);
    unsigned char* buf;
    uint32_t len;
    if ((buf = tetrish_server_decrypt_session_key(f->buf, f->len, credential, &len)) == NULL) {
        return -1;
    }
    client_io_pop_frame(c, 1);
    assert(len == SESSION_KEY_LEN);
    memcpy(*session_key, buf, SESSION_KEY_LEN);
    free(buf);

    client_io_transit_state(c, CLIENT_READING_LEN, 1);
    return 0;
}

ClientIoResult client_unauthed_transist_read(int epoll_fd, struct ClientIo* c_base) {
    size_t diff = offsetof(ClientUnauthed, base);
    ClientUnauthed* c = (ClientUnauthed*)((char*)c_base - diff);
    if (c->auth_state == CLIENT_UNAUTHED_NONCE) {
        if (server_auth_nonce(c_base, c->credential) == -1 || mod_epoll_events(epoll_fd, c_base->fd, EPOLLOUT | EPOLLRDHUP) == -1) {
            return CLIENT_IO_ERR;
        }

        client_io_transit_state(c_base, CLIENT_WRITING, 2);
        return CLIENT_IO_OK;
    }
    if (c->auth_state == CLIENT_UNAUTHED_SYMKEY) {
        if (server_auth_session_key(c_base, c->credential, &c->key) == -1) {
            return CLIENT_IO_ERR;
        }
        return CLIENT_IO_DONE;
    }
    assert(false);
    return CLIENT_IO_ERR;
}

ClientIoResult client_unauthed_transit_write(int epoll_fd, struct ClientIo* c_base) {
    size_t diff = offsetof(ClientUnauthed, base);
    ClientUnauthed* c = (ClientUnauthed*)((char*)c_base - diff);
    assert(c->auth_state == CLIENT_UNAUTHED_NONCE);

    if (mod_epoll_events(epoll_fd, c_base->fd, EPOLLIN | EPOLLRDHUP) == -1) {
        return CLIENT_IO_ERR;
    }

    client_io_transit_state(c_base, CLIENT_READING_LEN, 1);
    return CLIENT_IO_OK;
}

static ClientIoVtable CLIENT_UNAUTHED_VTABLE = {
    client_unauthed_transist_read,
    client_unauthed_transit_write,
};

void client_unauthed_init(ClientUnauthed* c, int client_fd, TetrishCredential *credential) {
    client_io_init(&c->base, client_fd);
    client_io_transit_state(&c->base, CLIENT_READING_LEN, 1);
    c->base.vptr = &CLIENT_UNAUTHED_VTABLE;
    c->credential = credential;
    c->auth_state = CLIENT_UNAUTHED_NONCE;
}

void client_unauthed_free(ClientUnauthed *c) {
    client_io_free(&c->base);
}