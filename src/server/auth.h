#ifndef CORESTACK_AUTH_H
#define CORESTACK_AUTH_H

#include "tetrissh.h"
#include "client_io.h"

int server_auth_nonce(struct client_io* c, TetrishCredential* credential);
int server_auth_session_key(struct client_io* c, TetrishCredential* credential, SessionKey* session_key);

#endif