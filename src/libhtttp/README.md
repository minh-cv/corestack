# HTTTP/1.0 messages

[`htttp.c`](htttp.c) implements the bounded message API in
[`htttp.h`](../../include/htttp.h). `HTTTP/1.0` is the literal protocol token;
the extra `T` is intentional.

## Message model

An `htttp_message_t` is either:

```text
METHOD PATH HTTTP/1.0\r\n
Header: value\r\n
\r\n
optional body
```

or:

```text
HTTTP/1.0 STATUS REASON\r\n
Header: value\r\n
\r\n
optional body
```

Methods, paths, status reasons, headers, and total messages are bounded by the
constants in `htttp.h`. Headers live in a fixed 32-entry array. Only the body
is dynamically allocated.

## Parsing

`htttp_parse` performs these checks in order:

1. Reject an empty input or a message larger than `HTTTP_MAX_MESSAGE`.
2. Locate `\r\n\r\n` and parse the request or response start line.
3. Split each header at its first colon, trim spaces/tabs after that colon,
   and copy the bounded name and value.
4. Reject a second `Content-Length` header.
5. Parse `Content-Length` as an unsigned decimal value.
6. Require the bytes after the header terminator to match that length exactly.
7. Allocate and copy the body.

Header lookup is case-insensitive. Duplicate headers other than
`Content-Length` remain in insertion order. On any failure, parsing frees any
body allocated so far and resets the message.

## Building and serialization

`htttp_add_header` copies a validated name and value into the message.
`htttp_set_body` replaces the old body with a new allocation and appends a
convenient NUL byte that is not counted in `body_length`.

`htttp_serialize` allocates a `HTTTP_MAX_MESSAGE + 1` work buffer, writes the
start line and headers, adds `Content-Length` for a non-empty body when it is
not already present, then copies the body bytes. The returned length is
authoritative; bodies may contain NUL bytes.

`htttp_make_response` resets a message, selects response kind, adds a GMT
`Date`, optionally adds `Content-Type`, and copies a non-empty text body. It
does not add `Content-Length` until serialization.

## Ownership

- Initialize stack or embedded messages with `htttp_message_init`.
- `htttp_set_body` and `htttp_parse` make an owned body copy.
- Call `htttp_message_free` to release the body and reset every field.
- `htttp_serialize` returns a buffer that the caller must `free`.
- Header pointers returned by `htttp_get_header` point into the message and
  become invalid when it is reset or destroyed.

