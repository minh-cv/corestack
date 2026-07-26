# Logger

[`logger.c`](logger.c) implements the process-global logging API declared in
[`logger.h`](../../include/logger.h). It formats each record once, transfers
the resulting string to the selected handler, and supports file, Unix-domain
socket, or custom destinations.

## Record creation

`LOGGER_LOG` expands to `_logger_make_log` followed by `_logger_log`. The
allocated record has this shape:

```text
[UTC time][source-file:line][SEVERITY][group] formatted message\n
```

`LOGGER_PERROR` appends the current `errno` string at error severity. A null
group is rendered as `-`.

The handler always owns the string, including on failure. Custom handlers
installed with `logger_set_log_handler` must therefore free or otherwise
consume the supplied pointer on every path.

## Destinations

### Null

The initial handler is `logger_log_null`. It frees the record, emits nothing,
and returns `-1`. Freeing a file or IPC destination restores this handler.

### File

`logger_init_file` selects the given `FILE *`. Each log writes and flushes the
complete string synchronously. `logger_free_file` closes the file, clears the
global pointer, and restores the null handler.

### IPC

`logger_init_ipc` connects an `AF_UNIX` stream socket at the configured path.
Each record is sent using the common four-byte framing:

```text
u32 big-endian byte length | log bytes
```

The implementation uses [`wire.h`](../../include/wire.h) for byte order.
`logger_free_ipc` closes the socket and restores the null handler.

The receiving side can combine [`LogBuf` and `ClientLogger`](../server/README.md)
to queue and forward records through nonblocking server connections.

## Global-state constraints

The active handler, file, and socket are static process-global variables:

- Only one destination is active at a time.
- Initialization does not automatically close a previously active
  destination.
- The implementation has no locking, so changing destinations or logging from
  multiple threads requires external synchronization.
- File writes, socket connection, and socket sends are synchronous.

