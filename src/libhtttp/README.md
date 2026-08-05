# HTTTP/1.0 messages

[`htttp.c`](htttp.c) implements the bounded, zero-copy message API in
[`htttp.h`](../../include/htttp.h). `HTTTP/1.0` is the literal protocol token;
the extra `T` is intentional. This is message (de)serialization only — nothing
here touches a socket.

## Message model

```c
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
    union { HtttpRequest request; HtttpResponse response; };
    bool is_request;
} HtttpMessage;
```

`HtttpMessage` is a tagged union: `is_request` selects which of `request`/
`response` is live. `HTTTP_HEADER_MAX` (32) bounds the header array; there is
no message-size constant — the caller's buffer length is the only limit.

On the wire, a message is either:

```text
METHOD PATH HTTTP/1.0\r\n
key:value\r\n
\r\n
optional body
```

or:

```text
HTTTP/1.0 STATUS REASON\r\n
key:value\r\n
\r\n
optional body
```

`is_request` is decided purely by whether the buffer starts with the 9 bytes
`HTTTP/1.0` — there is no separate framing signal.

## Parsing is zero-copy and destructive

`htttp_parse(unsigned char* buffer, size_t buffer_size, HtttpMessage* msg)`
does **not** allocate or copy anything. It scans `buffer` in place and:

- Overwrites each delimiter (the space after `METHOD`/`PATH`, the `:` after a
  header key, the `\r` of each line's `\r\n`) with `'\0'`, turning slices of
  the input into ordinary C strings.
- Points every field in `msg` (`method`, `path`, `reason`, header keys/values)
  directly into `buffer`.
- Sets `body`/`body_len` to whatever bytes remain after the terminating
  `\r\n\r\n` — everything to the end of the buffer, verbatim, including
  embedded NULs.

Consequences:

- **`buffer` must be mutable and must outlive `msg`.** A string literal will
  fault on the in-place write; a stack/heap buffer that's freed or reused
  invalidates every pointer in `msg`.
- There is no `Content-Length` handling, no duplicate-header rejection, and
  no header value trimming — none of that exists in this implementation.
  Anything after the blank line is the body, full stop.
- Header lookup (`htttp_get_header`) uses `strcmp`, **not** `strcasecmp` — key
  matching is case-sensitive, unlike ordinary HTTP semantics.

### Parsing failure modes

`htttp_parse` returns `0` on success, `-1` on any of:

- Missing space(s) in the request line, or the literal `HTTTP/1.0` token not
  found where expected.
- A response status that isn't a valid, in-range (`100`–`599`) decimal
  integer via `strtol`.
- A header line missing its `:`.
- No `\r\n\r\n` terminator before the buffer ends.
- More than `HTTTP_HEADER_MAX` (32) headers: the header-collection loop stops
  once 32 are parsed, and then requires the very next bytes to be the
  terminator. A 33rd header there fails that check, so the whole message is
  rejected — headers are never silently truncated to 32.

There's no partial-result cleanup to do on failure: since nothing is
allocated, an early return just leaves `*msg` however far parsing got.

## Serialization

`unsigned char* htttp_serialize(const HtttpMessage* msg, size_t* buffer_size)`
computes the exact serialized length first (start line + `key:value\r\n` per
header + `\r\n` + body), checking for `size_t` overflow at each accumulation
step, then allocates exactly that many bytes and writes into it. Returns
`NULL` on a formatting error or overflow — the caller does not need to guess
a buffer size up front.

`htttp_serialize` does **not** add `Content-Length` or any other header
automatically; callers who need it must add it to the message's `header`
array themselves before calling.

`char* htttp_make_rfc_1123_date(void)` returns a newly `malloc`'d,
locale-independent RFC 1123 date string (e.g. `Wed, 05 Aug 2026 12:00:00
GMT`) using internal day/month name tables rather than `strftime`, so its
output doesn't depend on the process locale.

## Ownership

`HtttpMessage` itself owns nothing — every pointer in it is either borrowed
(pointing into a caller buffer, e.g. right after `htttp_parse`) or something
the caller allocated by hand (e.g. when building a message to serialize).

`void htttp_message_free(HtttpMessage* message, const HtttpMessageOwnership* ownership)`
frees only the fields the caller marks as owned:

```c
typedef struct {
    bool is_method_owned;
    bool is_path_owned;
    bool is_reason_owned;
    bool is_key_owned[HTTTP_HEADER_MAX];
    bool is_value_owned[HTTTP_HEADER_MAX];
    bool is_body_owned;
} HtttpMessageOwnership;
```

There is no automatic tracking of what's owned — `ownership` must accurately
mirror how each field was populated, one flag per field/header slot. A stale
or all-`false` `ownership` after building a message with heap-allocated
fields leaks; a stale `true` on a borrowed/literal field is a wild free.
`htttp_message_free` zeroes the whole `HtttpMessage` afterward regardless.

## What changed from the previous API

This is a from-scratch rewrite (`feat!: rewrite htttp to be a view over a
buffer`, 2026-07-31) of an earlier copying/owning design. If you see
references elsewhere to `htttp_message_init`, `htttp_add_header`,
`htttp_set_body`, `htttp_message_t`, `HTTTP_MAX_MESSAGE`, or
`htttp_make_response` — those are gone; they belonged to the old API.
