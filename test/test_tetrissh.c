#include "unity.h"
#include "tetrissh.h"
#include "common.h"
#include "wire.h"
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* suite fixtures: a CA, a server cert signed by it, and an unrelated  */
/* second CA for the negative verification cases                       */
/* ------------------------------------------------------------------ */

static EVP_PKEY* ca_key = NULL;
static X509* ca_cert = NULL;
static EVP_PKEY* server_key = NULL;
static X509* server_cert = NULL;
static EVP_PKEY* rogue_ca_key = NULL;
static X509* rogue_ca_cert = NULL;

static char ca_path[128];
static char rogue_ca_path[128];
static char cert_path[128];
static char key_path[128];
static char empty_path[128];

static X509* make_cert(EVP_PKEY* subject_key, const char* cn, X509* issuer_cert, EVP_PKEY* issuer_key, int is_ca) {
    X509* cert = X509_new();
    if (cert == NULL) {
        return NULL;
    }

    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert), -3600);
    X509_gmtime_adj(X509_getm_notAfter(cert), 3600 * 24);
    X509_set_pubkey(cert, subject_key);

    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char*)cn, -1, -1, 0);
    X509_set_issuer_name(cert, (issuer_cert != NULL) ? X509_get_subject_name(issuer_cert) : name);

    if (is_ca) {
        X509V3_CTX v3;
        X509V3_set_ctx_nodb(&v3);
        X509V3_set_ctx(&v3, cert, cert, NULL, NULL, 0);

        X509_EXTENSION* constraints = X509V3_EXT_conf_nid(NULL, &v3, NID_basic_constraints, "critical,CA:TRUE");
        X509_add_ext(cert, constraints, -1);
        X509_EXTENSION_free(constraints);

        X509_EXTENSION* usage = X509V3_EXT_conf_nid(NULL, &v3, NID_key_usage, "critical,keyCertSign,cRLSign");
        X509_add_ext(cert, usage, -1);
        X509_EXTENSION_free(usage);
    }

    if (X509_sign(cert, issuer_key, EVP_sha256()) == 0) {
        X509_free(cert);
        return NULL;
    }
    return cert;
}

static int write_cert_file(const char* path, X509* cert) {
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    int ok = PEM_write_X509(file, cert);
    fclose(file);
    return ok ? 0 : -1;
}

static int write_key_file(const char* path, EVP_PKEY* pkey) {
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    int ok = PEM_write_PrivateKey(file, pkey, NULL, NULL, 0, NULL, NULL);
    fclose(file);
    return ok ? 0 : -1;
}

static int suite_setup(void) {
    int pid = (int)getpid();
    snprintf(ca_path, sizeof(ca_path), "/tmp/corestack-tetrissh-%d-ca.crt", pid);
    snprintf(rogue_ca_path, sizeof(rogue_ca_path), "/tmp/corestack-tetrissh-%d-rogue.crt", pid);
    snprintf(cert_path, sizeof(cert_path), "/tmp/corestack-tetrissh-%d-server.crt", pid);
    snprintf(key_path, sizeof(key_path), "/tmp/corestack-tetrissh-%d-server.key", pid);
    snprintf(empty_path, sizeof(empty_path), "/tmp/corestack-tetrissh-%d-empty.pem", pid);

    ca_key = EVP_RSA_gen(2048);
    server_key = EVP_RSA_gen(2048);
    rogue_ca_key = EVP_RSA_gen(2048);
    if (ca_key == NULL || server_key == NULL || rogue_ca_key == NULL) {
        return -1;
    }

    ca_cert = make_cert(ca_key, "corestack-test-ca", NULL, ca_key, 1);
    rogue_ca_cert = make_cert(rogue_ca_key, "corestack-rogue-ca", NULL, rogue_ca_key, 1);
    server_cert = make_cert(server_key, "corestack-test-server", ca_cert, ca_key, 0);
    if (ca_cert == NULL || rogue_ca_cert == NULL || server_cert == NULL) {
        return -1;
    }

    FILE* empty = fopen(empty_path, "wb");
    if (empty == NULL) {
        return -1;
    }
    fclose(empty);

    return write_cert_file(ca_path, ca_cert) | write_cert_file(rogue_ca_path, rogue_ca_cert) |
           write_cert_file(cert_path, server_cert) | write_key_file(key_path, server_key);
}

static void suite_teardown(void) {
    unlink(ca_path);
    unlink(rogue_ca_path);
    unlink(cert_path);
    unlink(key_path);
    unlink(empty_path);
    X509_free(ca_cert);
    X509_free(rogue_ca_cert);
    X509_free(server_cert);
    EVP_PKEY_free(ca_key);
    EVP_PKEY_free(rogue_ca_key);
    EVP_PKEY_free(server_key);
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static SessionKey test_key;
static SessionKey other_key;

void setUp(void) {
    memset(test_key, 0xA5, sizeof(test_key));
    memset(other_key, 0x5A, sizeof(other_key));
}

void tearDown(void) {}

// a connected pair with a receive timeout so a protocol bug fails instead of hanging
static void make_socketpair(int fds[2]) {
    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
    for (int i = 0; i < 2; i++) {
        TEST_ASSERT_EQUAL_INT(0, setsockopt(fds[i], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
    }
}

// memmem is a GNU extension; keep the suite portable with a plain scan
static int contains(const unsigned char* haystack, size_t haystack_len,
                    const unsigned char* needle, size_t needle_len) {
    if (needle_len > haystack_len) {
        return 0;
    }
    for (size_t i = 0; i + needle_len <= haystack_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static long file_size(const char* path) {
    FILE* file = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    return size;
}

/* ------------------------------------------------------------------ */
/* tetrish_credential_init / tetrish_credential_free                   */
/* ------------------------------------------------------------------ */

static void test_credential_init_loads_key_and_cert(void) {
    TetrishCredential credential;
    TEST_ASSERT_EQUAL_INT(0, tetrish_credential_init(&credential, key_path, cert_path));
    TEST_ASSERT_NOT_NULL(credential.private_key);
    TEST_ASSERT_NOT_NULL(credential.certificate);
    // the certificate is stored verbatim, PEM bytes and all
    TEST_ASSERT_EQUAL_UINT32((uint32_t)file_size(cert_path), credential.certificate_len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(credential.certificate, "-----BEGIN CERTIFICATE-----", 27));
    tetrish_credential_free(&credential);
}

static void test_credential_init_missing_key(void) {
    TetrishCredential credential;
    TEST_ASSERT_EQUAL_INT(-1, tetrish_credential_init(&credential, "/tmp/corestack-absent.key", cert_path));
}

static void test_credential_init_missing_cert(void) {
    // the private key loaded first must be released before returning
    TetrishCredential credential;
    TEST_ASSERT_EQUAL_INT(-1, tetrish_credential_init(&credential, key_path, "/tmp/corestack-absent.crt"));
}

static void test_credential_init_key_path_is_not_a_key(void) {
    TetrishCredential credential;
    TEST_ASSERT_EQUAL_INT(-1, tetrish_credential_init(&credential, cert_path, cert_path));
}

static void test_credential_init_empty_cert_file(void) {
    // store_file rejects a zero length outright
    TetrishCredential credential;
    TEST_ASSERT_EQUAL_INT(-1, tetrish_credential_init(&credential, key_path, empty_path));
}

static void test_credential_init_cert_path_is_a_directory(void) {
    // ftell reports LONG_MAX on a directory, which trips the UINT32_MAX guard
    TetrishCredential credential;
    TEST_ASSERT_EQUAL_INT(-1, tetrish_credential_init(&credential, key_path, "/tmp"));
}

/* ------------------------------------------------------------------ */
/* tetrish_session_encrypt / tetrish_session_decrypt                   */
/* ------------------------------------------------------------------ */

static void roundtrip(uint32_t length) {
    unsigned char* plain = malloc(length + 1);
    TEST_ASSERT_NOT_NULL(plain);
    for (uint32_t i = 0; i < length; i++) {
        plain[i] = (unsigned char)(i & 0xFF);
    }

    uint32_t cipher_len = 0;
    unsigned char* cipher = tetrish_session_encrypt(&test_key, plain, length, &cipher_len);
    TEST_ASSERT_NOT_NULL(cipher);

    // layout is IV(16) || AES-128-CBC ciphertext || HMAC-SHA256(32)
    uint32_t padded = (length / 16u + 1u) * 16u;
    TEST_ASSERT_EQUAL_UINT32(16u + padded + 32u, cipher_len);

    uint32_t plain_len = 0;
    unsigned char* decrypted = tetrish_session_decrypt(&test_key, cipher, cipher_len, &plain_len);
    TEST_ASSERT_NOT_NULL(decrypted);
    TEST_ASSERT_EQUAL_UINT32(length, plain_len);
    if (length > 0) { // Unity refuses a zero-length comparison
        TEST_ASSERT_EQUAL_MEMORY(plain, decrypted, length);
    }

    free(decrypted);
    free(cipher);
    free(plain);
}

static void test_session_roundtrip_lengths(void) {
    roundtrip(0);
    roundtrip(1);
    roundtrip(15);
    roundtrip(16);
    roundtrip(17);
    roundtrip(1000);
}

static void test_session_encrypt_is_randomised(void) {
    // a fresh IV per call means the same plaintext must not produce the same token
    uint32_t first_len = 0;
    uint32_t second_len = 0;
    unsigned char* first = tetrish_session_encrypt(&test_key, (const unsigned char*)"same", 4, &first_len);
    unsigned char* second = tetrish_session_encrypt(&test_key, (const unsigned char*)"same", 4, &second_len);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL_UINT32(first_len, second_len);
    TEST_ASSERT_TRUE(memcmp(first, second, first_len) != 0);
    free(first);
    free(second);
}

static void test_session_decrypt_rejects_wrong_key(void) {
    uint32_t cipher_len = 0;
    unsigned char* cipher = tetrish_session_encrypt(&test_key, (const unsigned char*)"secret", 6, &cipher_len);
    TEST_ASSERT_NOT_NULL(cipher);

    uint32_t plain_len = 0;
    TEST_ASSERT_NULL(tetrish_session_decrypt(&other_key, cipher, cipher_len, &plain_len));
    free(cipher);
}

static void test_session_decrypt_rejects_tampering(void) {
    uint32_t cipher_len = 0;
    unsigned char* cipher = tetrish_session_encrypt(&test_key, (const unsigned char*)"secret", 6, &cipher_len);
    TEST_ASSERT_NOT_NULL(cipher);

    // flip a bit in the ciphertext body; the HMAC covers IV || ciphertext
    cipher[20] = (unsigned char)(cipher[20] ^ 0x01);
    uint32_t plain_len = 0;
    TEST_ASSERT_NULL(tetrish_session_decrypt(&test_key, cipher, cipher_len, &plain_len));
    free(cipher);
}

static void test_session_decrypt_rejects_truncation(void) {
    uint32_t cipher_len = 0;
    unsigned char* cipher = tetrish_session_encrypt(&test_key, (const unsigned char*)"secret", 6, &cipher_len);
    TEST_ASSERT_NOT_NULL(cipher);

    uint32_t plain_len = 0;
    TEST_ASSERT_NULL(tetrish_session_decrypt(&test_key, cipher, cipher_len - 1, &plain_len));
    TEST_ASSERT_NULL(tetrish_session_decrypt(&test_key, cipher, 0, &plain_len));
    free(cipher);
}

static void test_session_encrypt_frame_max_boundary(void) {
    // padding plus IV plus HMAC must still fit in a frame
    uint32_t largest = 65471;
    uint32_t too_big = 65472;

    unsigned char* plain = calloc(too_big, 1);
    TEST_ASSERT_NOT_NULL(plain);

    uint32_t cipher_len = 0;
    unsigned char* cipher = tetrish_session_encrypt(&test_key, plain, largest, &cipher_len);
    TEST_ASSERT_NOT_NULL(cipher);
    TEST_ASSERT_TRUE(cipher_len <= FRAME_MAX);
    free(cipher);

    TEST_ASSERT_NULL(tetrish_session_encrypt(&test_key, plain, too_big, &cipher_len));
    free(plain);
}

/* ------------------------------------------------------------------ */
/* tetrish_send_frame / tetrish_recv_frame                             */
/* ------------------------------------------------------------------ */

static void test_frame_plain_roundtrip(void) {
    int fds[2];
    make_socketpair(fds);

    const unsigned char payload[] = "hello frame";
    TEST_ASSERT_EQUAL_INT(0, tetrish_send_frame(fds[0], payload, sizeof(payload), NULL));

    uint32_t length = 0;
    unsigned char* received = tetrish_recv_frame(fds[1], &length, NULL);
    TEST_ASSERT_NOT_NULL(received);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), length);
    TEST_ASSERT_EQUAL_MEMORY(payload, received, sizeof(payload));

    free(received);
    close(fds[0]);
    close(fds[1]);
}

static void test_frame_plain_wire_format(void) {
    int fds[2];
    make_socketpair(fds);

    const unsigned char payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_EQUAL_INT(0, tetrish_send_frame(fds[0], payload, sizeof(payload), NULL));

    // a 4-byte big-endian length prefix, then the body verbatim
    unsigned char header[4];
    TEST_ASSERT_EQUAL_INT(4, recv(fds[1], header, sizeof(header), 0));
    TEST_ASSERT_EQUAL_UINT32(4u, decode_u32_be(header));
    TEST_ASSERT_EQUAL_UINT8(0, header[0]);
    TEST_ASSERT_EQUAL_UINT8(4, header[3]);

    unsigned char body[4];
    TEST_ASSERT_EQUAL_INT(4, recv(fds[1], body, sizeof(body), 0));
    TEST_ASSERT_EQUAL_MEMORY(payload, body, sizeof(payload));

    close(fds[0]);
    close(fds[1]);
}

static void test_frame_plain_zero_length(void) {
    int fds[2];
    make_socketpair(fds);

    TEST_ASSERT_EQUAL_INT(0, tetrish_send_frame(fds[0], (const unsigned char*)"", 0, NULL));

    uint32_t length = 12345;
    unsigned char* received = tetrish_recv_frame(fds[1], &length, NULL);
    TEST_ASSERT_NOT_NULL(received);
    TEST_ASSERT_EQUAL_UINT32(0, length);

    free(received);
    close(fds[0]);
    close(fds[1]);
}

static void test_frame_send_rejects_oversized_plaintext(void) {
    int fds[2];
    make_socketpair(fds);

    unsigned char* payload = calloc(FRAME_MAX + 1, 1);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_EQUAL_INT(-1, tetrish_send_frame(fds[0], payload, FRAME_MAX + 1, NULL));
    // the boundary itself is accepted
    TEST_ASSERT_EQUAL_INT(0, tetrish_send_frame(fds[0], payload, FRAME_MAX, NULL));

    free(payload);
    close(fds[0]);
    close(fds[1]);
}

static void test_frame_recv_rejects_oversized_header(void) {
    int fds[2];
    make_socketpair(fds);

    uint8_t header[4];
    encode_u32_be(header, FRAME_MAX + 1);
    TEST_ASSERT_EQUAL_INT(4, send(fds[0], header, sizeof(header), 0));

    uint32_t length = 0;
    TEST_ASSERT_NULL(tetrish_recv_frame(fds[1], &length, NULL));

    close(fds[0]);
    close(fds[1]);
}

static void test_frame_recv_on_closed_peer(void) {
    int fds[2];
    make_socketpair(fds);
    close(fds[0]);

    uint32_t length = 0;
    TEST_ASSERT_NULL(tetrish_recv_frame(fds[1], &length, NULL));

    close(fds[1]);
}

static void test_frame_recv_truncated_body(void) {
    int fds[2];
    make_socketpair(fds);

    // announce 8 bytes but only ever send 3, then hang up
    uint8_t header[4];
    encode_u32_be(header, 8);
    TEST_ASSERT_EQUAL_INT(4, send(fds[0], header, sizeof(header), 0));
    TEST_ASSERT_EQUAL_INT(3, send(fds[0], "abc", 3, 0));
    close(fds[0]);

    uint32_t length = 0;
    TEST_ASSERT_NULL(tetrish_recv_frame(fds[1], &length, NULL));

    close(fds[1]);
}

static void test_frame_encrypted_roundtrip(void) {
    int fds[2];
    make_socketpair(fds);

    const unsigned char payload[] = "encrypted frame";
    TEST_ASSERT_EQUAL_INT(0, tetrish_send_frame(fds[0], payload, sizeof(payload), &test_key));

    uint32_t length = 0;
    unsigned char* received = tetrish_recv_frame(fds[1], &length, &test_key);
    TEST_ASSERT_NOT_NULL(received);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), length);
    TEST_ASSERT_EQUAL_MEMORY(payload, received, sizeof(payload));

    free(received);
    close(fds[0]);
    close(fds[1]);
}

static void test_frame_encrypted_is_not_plaintext_on_the_wire(void) {
    int fds[2];
    make_socketpair(fds);

    const unsigned char payload[] = "encrypted frame";
    TEST_ASSERT_EQUAL_INT(0, tetrish_send_frame(fds[0], payload, sizeof(payload), &test_key));

    unsigned char header[4];
    TEST_ASSERT_EQUAL_INT(4, recv(fds[1], header, sizeof(header), 0));
    uint32_t length = decode_u32_be(header);
    TEST_ASSERT_TRUE(length > sizeof(payload));

    unsigned char* body = malloc(length);
    TEST_ASSERT_NOT_NULL(body);
    uint32_t got = 0;
    while (got < length) {
        ssize_t n = recv(fds[1], body + got, length - got, 0);
        TEST_ASSERT_TRUE(n > 0);
        got += (uint32_t)n;
    }
    TEST_ASSERT_FALSE(contains(body, length, payload, sizeof(payload)));

    free(body);
    close(fds[0]);
    close(fds[1]);
}

static void test_frame_recv_rejects_wrong_key(void) {
    int fds[2];
    make_socketpair(fds);

    const unsigned char payload[] = "encrypted frame";
    TEST_ASSERT_EQUAL_INT(0, tetrish_send_frame(fds[0], payload, sizeof(payload), &test_key));

    uint32_t length = 0;
    TEST_ASSERT_NULL(tetrish_recv_frame(fds[1], &length, &other_key));

    close(fds[0]);
    close(fds[1]);
}

static void test_frame_send_on_closed_peer(void) {
    int fds[2];
    make_socketpair(fds);
    close(fds[1]);

    // SIGPIPE would kill the process; the library relies on the caller ignoring it
    signal(SIGPIPE, SIG_IGN);
    const unsigned char payload[] = "gone";
    TEST_ASSERT_EQUAL_INT(-1, tetrish_send_frame(fds[0], payload, sizeof(payload), NULL));
    signal(SIGPIPE, SIG_DFL);

    close(fds[0]);
}

/* ------------------------------------------------------------------ */
/* tetrish_server_sign_nonce                                           */
/* ------------------------------------------------------------------ */

static void test_sign_nonce_verifies_against_the_cert(void) {
    unsigned char nonce[SESSION_KEY_LEN];
    TEST_ASSERT_EQUAL_INT(1, RAND_bytes(nonce, sizeof(nonce)));

    uint32_t signature_len = 0;
    unsigned char* signature = tetrish_server_sign_nonce(nonce, sizeof(nonce), server_key, &signature_len);
    TEST_ASSERT_NOT_NULL(signature);
    // RSA-PSS over a 2048-bit key
    TEST_ASSERT_EQUAL_UINT32(256, signature_len);
    TEST_ASSERT_EQUAL_INT(1, verify_message_pss(server_cert, signature, signature_len, nonce, sizeof(nonce)));

    free(signature);
}

static void test_sign_nonce_does_not_verify_a_different_nonce(void) {
    unsigned char nonce[SESSION_KEY_LEN];
    memset(nonce, 0x11, sizeof(nonce));

    uint32_t signature_len = 0;
    unsigned char* signature = tetrish_server_sign_nonce(nonce, sizeof(nonce), server_key, &signature_len);
    TEST_ASSERT_NOT_NULL(signature);

    unsigned char other[SESSION_KEY_LEN];
    memset(other, 0x22, sizeof(other));
    TEST_ASSERT_EQUAL_INT(0, verify_message_pss(server_cert, signature, signature_len, other, sizeof(other)));

    free(signature);
}

static void test_sign_nonce_is_randomised(void) {
    unsigned char nonce[SESSION_KEY_LEN];
    memset(nonce, 0x11, sizeof(nonce));

    uint32_t first_len = 0;
    uint32_t second_len = 0;
    unsigned char* first = tetrish_server_sign_nonce(nonce, sizeof(nonce), server_key, &first_len);
    unsigned char* second = tetrish_server_sign_nonce(nonce, sizeof(nonce), server_key, &second_len);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(second);
    // PSS uses a random salt
    TEST_ASSERT_TRUE(memcmp(first, second, first_len) != 0);

    free(first);
    free(second);
}

/* ------------------------------------------------------------------ */
/* tetrish_server_decrypt_session_key                                  */
/* ------------------------------------------------------------------ */

static void test_decrypt_session_key_roundtrip(void) {
    TetrishCredential credential;
    TEST_ASSERT_EQUAL_INT(0, tetrish_credential_init(&credential, key_path, cert_path));

    EVP_PKEY* public_key = X509_get_pubkey(server_cert);
    TEST_ASSERT_NOT_NULL(public_key);

    unsigned char session[SESSION_KEY_LEN];
    TEST_ASSERT_EQUAL_INT(0, generate_session_key(session));

    size_t cipher_len = 0;
    unsigned char* cipher = rsa_encrypt_block(public_key, session, sizeof(session), &cipher_len, 1);
    TEST_ASSERT_NOT_NULL(cipher);

    uint32_t out_len = 0;
    unsigned char* recovered = tetrish_server_decrypt_session_key(cipher, (uint32_t)cipher_len, &credential, &out_len);
    TEST_ASSERT_NOT_NULL(recovered);
    TEST_ASSERT_EQUAL_UINT32(SESSION_KEY_LEN, out_len);
    TEST_ASSERT_EQUAL_MEMORY(session, recovered, SESSION_KEY_LEN);

    free(recovered);
    free(cipher);
    EVP_PKEY_free(public_key);
    tetrish_credential_free(&credential);
}

static void test_decrypt_session_key_rejects_garbage(void) {
    TetrishCredential credential;
    TEST_ASSERT_EQUAL_INT(0, tetrish_credential_init(&credential, key_path, cert_path));

    unsigned char garbage[256];
    memset(garbage, 0x7F, sizeof(garbage));

    uint32_t out_len = 0;
    TEST_ASSERT_NULL(tetrish_server_decrypt_session_key(garbage, sizeof(garbage), &credential, &out_len));

    tetrish_credential_free(&credential);
}

static void test_decrypt_session_key_rejects_wrong_length_payload(void) {
    // decrypts cleanly but is not SESSION_KEY_LEN bytes, so it must be refused
    TetrishCredential credential;
    TEST_ASSERT_EQUAL_INT(0, tetrish_credential_init(&credential, key_path, cert_path));

    EVP_PKEY* public_key = X509_get_pubkey(server_cert);
    TEST_ASSERT_NOT_NULL(public_key);

    unsigned char short_payload[16];
    memset(short_payload, 0x33, sizeof(short_payload));

    size_t cipher_len = 0;
    unsigned char* cipher = rsa_encrypt_block(public_key, short_payload, sizeof(short_payload), &cipher_len, 1);
    TEST_ASSERT_NOT_NULL(cipher);

    uint32_t out_len = 0;
    TEST_ASSERT_NULL(tetrish_server_decrypt_session_key(cipher, (uint32_t)cipher_len, &credential, &out_len));

    free(cipher);
    EVP_PKEY_free(public_key);
    tetrish_credential_free(&credential);
}

/* ------------------------------------------------------------------ */
/* full handshake, client in the parent and server in a forked child   */
/* ------------------------------------------------------------------ */

static int recv_exact(int fd, unsigned char* buf, uint32_t length) {
    uint32_t got = 0;
    while (got < length) {
        ssize_t n = recv(fd, buf + got, length - got, 0);
        if (n <= 0) {
            return -1;
        }
        got += (uint32_t)n;
    }
    return 0;
}

static int recv_length(int fd, uint32_t* out) {
    unsigned char header[4];
    if (recv_exact(fd, header, sizeof(header)) == -1) {
        return -1;
    }
    *out = decode_u32_be(header);
    return 0;
}

static int send_length(int fd, uint32_t value) {
    uint8_t header[4];
    encode_u32_be(header, value);
    return send_all(fd, header, sizeof(header));
}

// the server half of tetrish_client_handshake; returns 0 on success
static int run_server(int fd, int answer_session_key) {
    TetrishCredential credential;
    if (tetrish_credential_init(&credential, key_path, cert_path) == -1) {
        return -1;
    }

    int rc = -1;
    unsigned char* nonce = NULL;
    unsigned char* signature = NULL;
    unsigned char* cipherkey = NULL;
    unsigned char* session = NULL;

    uint32_t nonce_len = 0;
    if (recv_length(fd, &nonce_len) == -1 || nonce_len > FRAME_MAX) {
        goto done;
    }
    nonce = malloc(nonce_len);
    if (nonce == NULL || recv_exact(fd, nonce, nonce_len) == -1) {
        goto done;
    }

    uint32_t signature_len = 0;
    signature = tetrish_server_sign_nonce(nonce, nonce_len, credential.private_key, &signature_len);
    if (signature == NULL) {
        goto done;
    }
    if (send_length(fd, signature_len) == -1 || send_all(fd, signature, signature_len) == -1) {
        goto done;
    }
    if (send_length(fd, credential.certificate_len) == -1 ||
        send_all(fd, credential.certificate, credential.certificate_len) == -1) {
        goto done;
    }

    if (!answer_session_key) {
        rc = 0;
        goto done;
    }

    uint32_t cipherkey_len = 0;
    if (recv_length(fd, &cipherkey_len) == -1 || cipherkey_len > FRAME_MAX) {
        goto done;
    }
    cipherkey = malloc(cipherkey_len);
    if (cipherkey == NULL || recv_exact(fd, cipherkey, cipherkey_len) == -1) {
        goto done;
    }

    uint32_t session_len = 0;
    session = tetrish_server_decrypt_session_key(cipherkey, cipherkey_len, &credential, &session_len);
    if (session == NULL || session_len != SESSION_KEY_LEN) {
        goto done;
    }

    // prove both sides derived the same key by exchanging one encrypted frame
    SessionKey key;
    memcpy(key, session, SESSION_KEY_LEN);

    uint32_t request_len = 0;
    unsigned char* request = tetrish_recv_frame(fd, &request_len, &key);
    if (request == NULL || request_len != 4 || memcmp(request, "ping", 4) != 0) {
        free(request);
        goto done;
    }
    free(request);

    if (tetrish_send_frame(fd, (const unsigned char*)"pong", 4, &key) == -1) {
        goto done;
    }

    rc = 0;

done:
    free(session);
    free(cipherkey);
    free(signature);
    free(nonce);
    tetrish_credential_free(&credential);
    return rc;
}

// fork the server half; returns the child pid
static pid_t fork_server(int fd, int answer_session_key) {
    fflush(NULL);
    pid_t pid = fork();
    TEST_ASSERT_TRUE(pid >= 0);
    if (pid == 0) {
        _exit(run_server(fd, answer_session_key) == 0 ? 0 : 1);
    }
    return pid;
}

static void expect_child_exit(pid_t pid, int expected) {
    int status = 0;
    TEST_ASSERT_EQUAL_INT(pid, waitpid(pid, &status, 0));
    TEST_ASSERT_TRUE_MESSAGE(WIFEXITED(status), "the server child did not exit normally");
    TEST_ASSERT_EQUAL_INT(expected, WEXITSTATUS(status));
}

static void test_handshake_succeeds_and_agrees_on_a_key(void) {
    int fds[2];
    make_socketpair(fds);

    pid_t pid = fork_server(fds[1], 1);
    close(fds[1]);

    SessionKey key;
    memset(key, 0, sizeof(key));
    TEST_ASSERT_EQUAL_INT(0, tetrish_client_handshake(fds[0], ca_path, &key));

    TEST_ASSERT_EQUAL_INT(0, tetrish_send_frame(fds[0], (const unsigned char*)"ping", 4, &key));

    uint32_t length = 0;
    unsigned char* response = tetrish_recv_frame(fds[0], &length, &key);
    TEST_ASSERT_NOT_NULL(response);
    TEST_ASSERT_EQUAL_UINT32(4, length);
    TEST_ASSERT_EQUAL_MEMORY("pong", response, 4);
    free(response);

    close(fds[0]);
    expect_child_exit(pid, 0);
}

static void test_handshake_rejects_an_untrusted_ca(void) {
    int fds[2];
    make_socketpair(fds);

    // the server never gets as far as the session key, so it stops after the cert
    pid_t pid = fork_server(fds[1], 0);
    close(fds[1]);

    SessionKey key;
    memset(key, 0, sizeof(key));
    TEST_ASSERT_EQUAL_INT(-1, tetrish_client_handshake(fds[0], rogue_ca_path, &key));

    close(fds[0]);
    expect_child_exit(pid, 0);
}

static void test_handshake_rejects_a_missing_ca_file(void) {
    int fds[2];
    make_socketpair(fds);

    pid_t pid = fork_server(fds[1], 0);
    close(fds[1]);

    SessionKey key;
    memset(key, 0, sizeof(key));
    TEST_ASSERT_EQUAL_INT(-1, tetrish_client_handshake(fds[0], "/tmp/corestack-absent-ca.crt", &key));

    close(fds[0]);
    expect_child_exit(pid, 0);
}

static void test_handshake_rejects_an_oversized_signature_header(void) {
    int fds[2];
    make_socketpair(fds);

    // consume the client nonce, then claim a signature larger than a frame
    unsigned char scratch[SESSION_KEY_LEN + 4];
    pid_t pid = fork();
    TEST_ASSERT_TRUE(pid >= 0);
    if (pid == 0) {
        int ok = recv_exact(fds[1], scratch, sizeof(scratch)) == 0 &&
                 send_length(fds[1], FRAME_MAX + 1) == 0;
        _exit(ok ? 0 : 1);
    }
    close(fds[1]);

    SessionKey key;
    memset(key, 0, sizeof(key));
    TEST_ASSERT_EQUAL_INT(-1, tetrish_client_handshake(fds[0], ca_path, &key));

    close(fds[0]);
    expect_child_exit(pid, 0);
}

static void test_handshake_rejects_a_dead_peer(void) {
    int fds[2];
    make_socketpair(fds);
    close(fds[1]);

    signal(SIGPIPE, SIG_IGN);
    SessionKey key;
    memset(key, 0, sizeof(key));
    TEST_ASSERT_EQUAL_INT(-1, tetrish_client_handshake(fds[0], ca_path, &key));
    signal(SIGPIPE, SIG_DFL);

    close(fds[0]);
}

/* ------------------------------------------------------------------ */

int main(void) {
    if (suite_setup() != 0) {
        fputs("failed to build the test CA and server certificate\n", stderr);
        return 1;
    }

    UNITY_BEGIN();

    RUN_TEST(test_credential_init_loads_key_and_cert);
    RUN_TEST(test_credential_init_missing_key);
    RUN_TEST(test_credential_init_missing_cert);
    RUN_TEST(test_credential_init_key_path_is_not_a_key);
    RUN_TEST(test_credential_init_empty_cert_file);
    RUN_TEST(test_credential_init_cert_path_is_a_directory);

    RUN_TEST(test_session_roundtrip_lengths);
    RUN_TEST(test_session_encrypt_is_randomised);
    RUN_TEST(test_session_decrypt_rejects_wrong_key);
    RUN_TEST(test_session_decrypt_rejects_tampering);
    RUN_TEST(test_session_decrypt_rejects_truncation);
    RUN_TEST(test_session_encrypt_frame_max_boundary);

    RUN_TEST(test_frame_plain_roundtrip);
    RUN_TEST(test_frame_plain_wire_format);
    RUN_TEST(test_frame_plain_zero_length);
    RUN_TEST(test_frame_send_rejects_oversized_plaintext);
    RUN_TEST(test_frame_recv_rejects_oversized_header);
    RUN_TEST(test_frame_recv_on_closed_peer);
    RUN_TEST(test_frame_recv_truncated_body);
    RUN_TEST(test_frame_encrypted_roundtrip);
    RUN_TEST(test_frame_encrypted_is_not_plaintext_on_the_wire);
    RUN_TEST(test_frame_recv_rejects_wrong_key);
    RUN_TEST(test_frame_send_on_closed_peer);

    RUN_TEST(test_sign_nonce_verifies_against_the_cert);
    RUN_TEST(test_sign_nonce_does_not_verify_a_different_nonce);
    RUN_TEST(test_sign_nonce_is_randomised);

    RUN_TEST(test_decrypt_session_key_roundtrip);
    RUN_TEST(test_decrypt_session_key_rejects_garbage);
    RUN_TEST(test_decrypt_session_key_rejects_wrong_length_payload);

    RUN_TEST(test_handshake_succeeds_and_agrees_on_a_key);
    RUN_TEST(test_handshake_rejects_an_untrusted_ca);
    RUN_TEST(test_handshake_rejects_a_missing_ca_file);
    RUN_TEST(test_handshake_rejects_an_oversized_signature_header);
    RUN_TEST(test_handshake_rejects_a_dead_peer);

    int failures = UNITY_END();
    suite_teardown();
    return failures;
}
