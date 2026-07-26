# Configuration loader

[`config.c`](config.c) implements the small `name=value` loader declared in
[`config.h`](../../include/config.h). It reads the complete file, copies valid
directives into a fixed-capacity `Config`, and provides string, path, and
integer lookup helpers.

## Accepted syntax

```text
directive=value
```

- Leading whitespace before `directive` is ignored.
- A directive contains letters, digits, or underscores and must be followed
  immediately by `=`.
- The value continues to newline, carriage return, or end of string.
- Whitespace around `=` is not trimmed.
- Blank and malformed lines are skipped.
- At most `CONFIG_MAX_ARGS` entries are stored. Reaching the limit prints a
  warning and ignores the remainder.
- Duplicate directives are retained; lookup returns the first one.

For example:

```text
port=2222
certificate=auth/server.crt
workers =4
```

The first two lines are accepted. The third is skipped because the space
between `workers` and `=` is not valid.

## Loading flow

`config_make` opens the file, obtains its size with `fseek`/`ftell`, reads it
into one temporary buffer, and calls `parse_content`. `parse_content` splits
on newline, `parse_line` inserts terminators around each accepted name/value,
and both strings are copied into separately allocated storage.

Temporary file and buffer cleanup uses the LIFO destructor macros from
[`dtor.h`](../../include/dtor.h). Parsed entries remain owned by `Config`.

## Lookup helpers

- `config_get_arg_idx` returns an array index or `CONFIG_MAX_ARGS` when absent.
- `config_get_path` returns a newly allocated path. Absolute values are copied;
  relative values are joined to `project_dir`.
- `config_get_long_arg` parses with `strtol(..., 0)`, so decimal, octal, and
  hexadecimal prefixes are accepted. It rejects empty, partial, and
  out-of-range values.
- `concat_path` removes one trailing slash from the first component and
  inserts exactly one separator before the second.

## Ownership

`Config.argn[i]` and `Config.argv[i]` are heap allocations owned by the
configuration object. Call `config_free` exactly once after a successful or
partially successful load. It releases all stored pairs and reduces `argc` to
zero.

Strings returned by `config_get_path` and `concat_path` are independent
allocations owned by their callers.

