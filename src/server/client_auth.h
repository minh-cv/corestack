#ifndef CORESTACK_AUTH_H
#define CORESTACK_AUTH_H

#include "tetrissh.h"
#include "client_io.h"

typedef struct ClientUnauthed {
    struct ClientIo base;
    enum ClientUnauthedAuthState {
        CLIENT_UNAUTHED_NONCE,
        CLIENT_UNAUTHED_SYMKEY,
    } auth_state;
    TetrishCredential* credential;
    SessionKey key;
} ClientUnauthed;

void client_unauthed_init(ClientUnauthed* c, int client_fd, TetrishCredential *credential);
void client_unauthed_free(ClientUnauthed* c);
ClientIoResult client_unauthed_transist_read(struct ClientIo* c_base);
ClientIoResult client_unauthed_transit_write(struct ClientIo* c_base);

#endif