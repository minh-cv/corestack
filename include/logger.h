/*!
    @note this should be only under tetrisd but technically any program can use it. not sure worth it though, redirecting stdout/stderr is faster.

    however, this file should be shared so that other programs can use the same logging format.
*/

#ifndef TETRISH_LOGGER_H
#define TETRISH_LOGGER_H

#include <stdio.h>
#include <errno.h>
#include <string.h> // IWYU pragma: export

#include "macro_utility.h"

typedef enum LoggerSeverity {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} LoggerSeverity;

/*!
    @brief set a global file descriptor for logging.
*/
int logger_init_ipc(const char* log_ipc);

/*!
    @brief set an output file for logging, could be stdout.
*/
void logger_init_file(FILE* file);

/*!
    @brief set a handler that does the "logging", taking ownership of the string.
*/
void logger_set_log_handler(int (*fn)(char*));

/*!
    @brief Produce a formatted heap-allocated string, with the current time.
*/
FORMAT_PRINTF(5, 6)
char* _logger_make_log(enum LoggerSeverity severity, const char* group, const char* file, int line, const char* fmt, ...);

/*!
    @brief sends log of a fully formatted string, consuming the string.
    @see logger_set_log_handler
*/
int _logger_log(char* string);

/*!
    @brief free string and return -1. Use with logger_set_log_handler as a sentinel value. 
*/
int logger_log_null(char* string);

int logger_free_ipc();
int logger_free_file();

#define LOGGER_MAKE_LOG(severity, group, ...) \
    _logger_make_log((severity), (group), __FILE__, __LINE__, __VA_ARGS__)

#define LOGGER_LOG(severity, group, ...) \
    _logger_log((LOGGER_MAKE_LOG(severity, group, __VA_ARGS__)))

#define LOGGER_PERROR(group, msg) LOGGER_LOG(LOG_ERROR, group, "%s: %s", (msg), (strerror(errno)))

#endif
