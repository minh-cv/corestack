#ifndef CORESTACK_HTTTP_H
#define CORESTACK_HTTTP_H

#include <stdbool.h>
#include <stddef.h>
#define HTTTP_HEADER_MAX 32

typedef struct {
    const char* key;
    const char* value;
} HtttpHeader;

typedef struct {
    const char* method;
    const char* path;
    HtttpHeader header[HTTTP_HEADER_MAX];
    size_t header_count;
    const unsigned char* body;
    size_t body_len;
} HtttpRequest;

typedef struct {
    int status;
    const char* reason;
    HtttpHeader header[HTTTP_HEADER_MAX];
    size_t header_count;
    const unsigned char* body;
    size_t body_len;
} HtttpResponse;

typedef struct {
    union {
        HtttpRequest request;
        HtttpResponse response;
    };
    bool is_request;
} HtttpMessage;

int htttp_parse(unsigned char* buffer, size_t buffer_size, HtttpMessage* msg);
unsigned char* htttp_serialize(const HtttpMessage* msg, size_t* buffer_size);
int htttp_make_rfc_1123_date(char (*buf)[60]);
const char* htttp_get_header(const HtttpMessage* msg, const char* key);

#endif
