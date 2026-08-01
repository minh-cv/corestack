#include "htttp.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ACCESS_MEM(msg, mem) ((msg)->is_request ? &(msg)->request.mem : &(msg)->response.mem)

static int skip_until(unsigned char** buffer, const unsigned char* end, char target) {
    assert(end >= *buffer);
    void* ptr = memchr(*buffer, target, (size_t)(end - *buffer));
    if (ptr == NULL) return -1;
    *buffer = ptr;
    return 0;
}

static int skip_until_str(unsigned char** buffer, const unsigned char* end, const char* target) {
    assert(end >= *buffer);
    assert(*target != '\0');

    size_t len = strlen(target);
    if (skip_until(buffer, end, *target) == -1) {
        return -1;
    }

    while ((size_t)(end - *buffer) >= len) {        
        if (memcmp(*buffer, target, len) == 0) {
            return 0;
        }
        
        (*buffer)++;
        if (skip_until(buffer, end, *target) == -1) {
            return -1;
        }
    }

    return -1;
}

static bool does_start_with_htttp_1_0(const unsigned char* buffer, const unsigned char* end) {
    if (end - buffer < 9) return false;
    return memcmp((const char*)buffer, "HTTTP/1.0", 9) == 0;
}

static int parse_request_line(unsigned char** buffer_ref, const unsigned char* end, HtttpMessage* msg) {
    unsigned char* buffer = *buffer_ref;

    const char* const method_start = (const char*)buffer;
    if (skip_until(&buffer, end, ' ') == -1) {
        return -1;
    }

    *buffer = '\0';
    buffer += 1;
    if (buffer >= end) return -1;

    const char* const path_start = (const char*)buffer;
    if (skip_until(&buffer, end, ' ') == -1) {
        return -1;
    }

    *buffer = '\0';
    buffer += 1;
    if (buffer >= end) return -1;

    if (!does_start_with_htttp_1_0(buffer, end)) {
        return -1;
    }

    buffer += 9;
    if (buffer + 1 >= end || *buffer != '\r' || buffer[1] != '\n') return -1;

    buffer += 2;

    msg->request.method = method_start;
    msg->request.path = path_start;

    *buffer_ref = buffer;
    return 0;
}

static int parse_response_line(unsigned char** buffer_ref, const unsigned char* end, HtttpMessage* msg) {
    unsigned char* buffer = *buffer_ref;
    assert(end - buffer >= 9 && memcmp((const char*)buffer, "HTTTP/1.0", 9) == 0);
    buffer += 9;
    if (buffer >= end) return -1;
    if (*buffer != ' ') return -1;
    buffer++;
    if (buffer >= end) return -1;

    const unsigned char* const status_start = buffer;
    if (skip_until(&buffer, end, ' ') == -1) {
        return -1;
    }
    *buffer = '\0';

    
    errno = 0;
    char* endptr;
    long status = strtol((const char *)status_start, &endptr, 10);
    if (errno == ERANGE || (unsigned char*)endptr != buffer || status < 100 || status >= 600) {
        return -1;
    }

    buffer++;
    if (buffer >= end) return -1;
    
    const unsigned char* const reason_start = buffer;
    if (skip_until_str(&buffer, end, "\r\n") == -1) {
        return -1;
    }
    *buffer = '\0';
    buffer += 2;

    msg->response.status = (int)status;
    msg->response.reason = (const char*)reason_start;

    *buffer_ref = buffer;
    return 0;
}

static int parse_header(unsigned char** buffer_ref, const unsigned char* end, HtttpMessage* msg) {
    unsigned char* buffer = *buffer_ref;

    const char* const key_start = (const char* const)buffer;
    if (skip_until(&buffer, end, ':') == -1) {
        return -1;
    }
    *buffer = '\0';
    buffer += 1;
    if (buffer >= end) return -1;

    const char* const value_start = (const char* const)buffer;
    if (skip_until_str(&buffer, end, "\r\n") == -1) {
        return -1;
    }
    *buffer = '\0';
    buffer += 2;

    size_t* header_count = ACCESS_MEM(msg, header_count);

    HtttpHeader* header = &(*ACCESS_MEM(msg, header))[*header_count];

    header->key = key_start;
    header->value = value_start;
    (*header_count)++;

    *buffer_ref = buffer;
    return 0;
}

static void parse_body(unsigned char** buffer, const unsigned char* end, HtttpMessage* msg) {
    assert(end - *buffer >= 0);

    *ACCESS_MEM(msg, body_len) = (size_t)(end - *buffer);
    *ACCESS_MEM(msg, body) = *buffer;
}

int htttp_parse(unsigned char* buffer, size_t buffer_size, HtttpMessage* msg) {
    const unsigned char* end = buffer + buffer_size;

    msg->is_request = !does_start_with_htttp_1_0(buffer, end);
    
    if (!msg->is_request) {
        if (parse_response_line(&buffer, end, msg) == -1) {
            return -1;
        }
    }
    else {
        if (parse_request_line(&buffer, end, msg) == -1) {
            return -1;
        }
    }

    *ACCESS_MEM(msg, header_count) = 0;
    while (end - buffer > 2 && (buffer[0] != '\r' || buffer[1] != '\n') && *ACCESS_MEM(msg, header_count) < HTTTP_HEADER_MAX) {
        if (parse_header(&buffer, end, msg) == -1) {
            return -1;
        }
    }

    if (end - buffer < 2 || buffer[0] != '\r' || buffer[1] != '\n') {
        return -1;
    }

    buffer += 2;
    parse_body(&buffer, end, msg);
    return 0;
}

// TODO: risk integer overflow
unsigned char* htttp_serialize(const HtttpMessage* msg, size_t* buffer_size) {
    const bool is_request = msg->is_request;
    size_t total_len = 0;
    int first_len = -1;
    if (is_request) {
        first_len = snprintf(NULL, 0, "%s %s HTTTP/1.0\r\n", msg->request.method, msg->request.path);
    }
    else {
        first_len = snprintf(NULL, 0, "HTTTP/1.0 %d %s\r\n", msg->response.status, msg->response.reason);
    }

    assert(first_len != 0);
    if (first_len <= 0) {
        return NULL;
    }

    total_len += (size_t)first_len;

    for (size_t i = 0; i < *ACCESS_MEM(msg, header_count); i++) {
        const HtttpHeader* header = &(*ACCESS_MEM(msg, header))[i];
        int len = snprintf(NULL, 0, "%s:%s\r\n", header->key, header->value);

        if (len < 0) {
            return NULL;
        }

        size_t would_be = total_len + (size_t)len;
        if (would_be <= total_len) {
            return NULL;
        }
        total_len = would_be;
    }

    {
        size_t would_be = total_len + 2;
        if (would_be <= total_len) {
            return NULL;
        }
        total_len = would_be;
        would_be += *ACCESS_MEM(msg, body_len);
        if (would_be < total_len) {
            return NULL;
        }
        total_len = would_be;
    }

    unsigned char* dest = malloc(total_len);
    if (dest == NULL) {
        return NULL;
    }
    char* current = (char*)dest;
    
    if (is_request) {
        first_len = sprintf(current, "%s %s HTTTP/1.0\r\n", msg->request.method, msg->request.path);
    }
    else {
        first_len = sprintf(current, "HTTTP/1.0 %d %s\r\n", msg->response.status, msg->response.reason);
    }

    assert(first_len >= 0);
    if (first_len < 0) {
        free(dest);
        return NULL;
    }

    current += first_len;

    for (size_t i = 0; i < *ACCESS_MEM(msg, header_count); i++) {
        const HtttpHeader* header = &(*ACCESS_MEM(msg, header))[i];
        int len = sprintf(current, "%s:%s\r\n", header->key, header->value);

        assert(len >= 0);
        if (len < 0) {
            free(dest);
            return NULL;
        }

        current += len;
    }

    current[0] = '\r';
    current[1] = '\n';
    current += 2;

    if (*ACCESS_MEM(msg, body_len) > 0) {
        memcpy(current, *ACCESS_MEM(msg, body), *ACCESS_MEM(msg, body_len));
    }
    assert((size_t)(current + *ACCESS_MEM(msg, body_len) - (char*)dest) == total_len);

    *buffer_size = total_len;

    return dest;
}

char* htttp_make_rfc_1123_date(void) {
    static const size_t BUF_SIZE = 30;

    char* buf = malloc(BUF_SIZE);
    if (buf == NULL) {
        return NULL;
    }
    
    time_t now = time(NULL);
    struct tm tp;
    if (gmtime_r(&now, &tp) == NULL) {
        free(buf);
        return NULL;
    }

    static const char* const DAY[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char* const MON[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    int n = snprintf(
        buf, 
        BUF_SIZE, 
        "%s, %02d %s %04d %02d:%02d:%02d GMT",
        DAY[tp.tm_wday], 
        tp.tm_mday, 
        MON[tp.tm_mon], 
        tp.tm_year + 1900, 
        tp.tm_hour, 
        tp.tm_min, 
        tp.tm_sec);

    if (n < 0) { free(buf); return NULL; }
    if ((size_t)n >= BUF_SIZE) {
        assert(false);
        free(buf);
        return NULL;
    }
    
    return buf;
}

const char* htttp_get_header(const HtttpMessage* msg, const char* key) {
    for (size_t i = 0; i < *ACCESS_MEM(msg, header_count); i++) {
        const char* header_key = (*ACCESS_MEM(msg, header))[i].key;
        const char* header_value = (*ACCESS_MEM(msg, header))[i].value;
        if (strcmp(header_key, key) == 0) {
            return header_value;
        }
    }

    return NULL;
}

void htttp_message_free(HtttpMessage* message, const HtttpMessageOwnership* ownership) {
    if (message->is_request) {
        if (ownership->is_method_owned) 
            free((void*)message->request.method);
        if (ownership->is_path_owned) 
            free((void*)message->request.path);
        for (size_t i = 0; i < message->request.header_count; i++) {
            if (ownership->is_key_owned[i])
                free((void *)message->request.header[i].key);
            if (ownership->is_value_owned[i])
                free((void *)message->request.header[i].value);
        }
        if (ownership->is_body_owned)
            free((void *)message->request.body);
    }
    else {
        if (ownership->is_reason_owned)
            free((void *)message->response.reason);
        for (size_t i = 0; i < message->response.header_count; i++) {
            if (ownership->is_key_owned[i])
                free((void *)message->response.header[i].key);
            if (ownership->is_value_owned[i])
                free((void *)message->response.header[i].value);
        }
        if (ownership->is_body_owned)
            free((void *)message->response.body);
    }

    memset(message, 0, sizeof(*message));
}
