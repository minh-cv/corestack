#ifndef TETRISH_TETRISD_CLIENT_LOGGER_H
#define TETRISH_TETRISD_CLIENT_LOGGER_H

#include "client_io.h"
#include "log_buf.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct ClientLogger {
    ClientIo base;
    LogBuf* buf;
} ClientLogger;

void client_logger_init(ClientLogger* c, LogBuf* buf, int client_fd);
void client_logger_free(ClientLogger* c);
ClientIoResult client_logger_transist_write(struct ClientIo* c_base);

#endif