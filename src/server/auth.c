#include "auth.h"
#include <assert.h>

int server_auth_nonce(struct client_io* c, TetrishCredential* credential) {
    assert(c->frame_count == 1);

    struct frame frames[2] = {0};
    struct frame* frame_nonce = &frames[0];

    frame_nonce->is_heap_allocated = true;
    struct frame* f = client_io_get_top_frame(c);
    if ((frame_nonce->buf = tetrish_server_sign_nonce(f->buf, f->len, credential->private_key, &frame_nonce->len)) == NULL) {
        return -1;
    }
    client_io_pop_frame(c, 1);
    encode_u32_be(frame_nonce->len_buf, frame_nonce->len);

    struct frame frame_auth = {0};
    frame_auth.is_heap_allocated = false;
    frame_auth.buf = credential->certificate;
    frame_auth.len = credential->certificate_len;
    encode_u32_be(frame_auth.len_buf, frame_auth.len);

    frames[1] = frame_auth;

    client_io_push_frame(c, frames, sizeof(frames)/sizeof(struct frame));
    client_io_transit_state(c, CLIENT_WRITING, 2);
    return 0;
}

int server_auth_session_key(struct client_io* c, TetrishCredential* credential, SessionKey* session_key) {
    assert(c->frame_count == 1);

    struct frame* f = client_io_get_top_frame(c);
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
