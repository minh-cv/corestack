#ifndef TETRISH_SSH_H
#define TETRISH_SSH_H

#include "common.h"
#include "wire.h" // IWYU pragma: keep
#include <openssl/types.h>
#include <stdint.h>

#define NONCE_LEN 32

typedef struct TetrishCredential {
    EVP_PKEY* private_key;
    unsigned char* certificate;
    uint32_t certificate_len;
} TetrishCredential;

typedef unsigned char SessionKey[SESSION_KEY_LEN];

int tetrish_credential_init(TetrishCredential* buf, const char* key_path, const char* certificate_path);
void tetrish_credential_free(TetrishCredential* buf);

int tetrish_client_handshake(int sockfd, const char* ca_path, SessionKey* session_key);
unsigned char* tetrish_server_sign_nonce(unsigned char* nonce, uint32_t nonce_length, EVP_PKEY* private_key, uint32_t* response_length);
unsigned char* tetrish_server_decrypt_session_key(const unsigned char* cipherkey, uint32_t cipherkey_len, TetrishCredential* info, uint32_t* response_length);  

unsigned char* tetrish_recv_frame(int fd, uint32_t* plaintext_length, SessionKey* key);
int tetrish_send_frame(int sockfd, const unsigned char* plaintext, uint32_t plaintext_length, SessionKey* key);

unsigned char* tetrish_session_encrypt(SessionKey* key, const unsigned char *plain, uint32_t plain_len, uint32_t *out_len);
unsigned char* tetrish_session_decrypt(SessionKey* key, const unsigned char *token, uint32_t token_len, uint32_t *out_len);

#endif
