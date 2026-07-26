# Terminal backend

[`tui.c`](tui.c) implements the low-level API in
[`tui.h`](../../include/tui.h): raw terminal setup, input decoding, a cell
back buffer, and ANSI diff rendering. The retained widget layer is documented
in [`tuiui`](../tuiui/README.md).

## Lifetime

The backend is a global singleton. `tui_init`:

1. Validates a positive, non-overflowing cell size and requires TTY
   stdin/stdout.
2. Allocates front and back `TuiCell` buffers.
3. Saves terminal attributes and stdin flags.
4. Enters raw, nonblocking input mode.
5. Switches to the alternate screen, hides the cursor, and enables basic,
   drag, and SGR mouse reporting.

Always call `tui_shutdown` after successful initialization. It disables mouse
reporting, restores the cursor, screen, terminal attributes, and descriptor
flags, then frees both buffers. Arrange cleanup for errors and termination
signals so the user's terminal is not left in raw mode.

## Input

`tui_present` reads currently available stdin bytes into an 8192-byte buffer
and parses:

- Enter, tab, backspace, space, and escape.
- Arrow, home, end, insert, delete, page-up, and page-down CSI sequences.
- Printable ASCII character presses.
- SGR mouse position, press, release, drag, and horizontal/vertical wheel
  events.

Per-frame `clicked`, `repeated`, counts, mouse movement, wheel, and release
fields are reset before polling. Terminals do not normally send key-up events,
so `down` state expires when a key has not been observed for 180 ms.

Incomplete escape sequences are not retained for a later poll; a trailing
unmatched escape is recorded as Escape during the current poll.

## Cell buffers and rendering

`tui_get_buffer` returns the back buffer. A `TuiCell` holds up to seven UTF-8
bytes plus a terminator, display width, foreground/background RGB colors, and
style bits. `TUI_COLOR_DEFAULT` selects the terminal default color.

```mermaid
flowchart LR
  Draw["Caller writes back buffer"] --> Present["tui_present"]
  Present --> Poll["Poll and decode input"]
  Poll --> Diff["Compare back and front cells"]
  Diff --> Emit["Emit changed ANSI cells"]
  Emit --> Copy["Copy back buffer to front buffer"]
  Copy --> Draw
```

The renderer tracks cursor position and active styles/colors to avoid
redundant escape sequences. It skips unchanged cells unless a full redraw is
forced. After a successful flush, the complete back buffer becomes the new
front buffer.

A cell with `width=2` consumes two columns; the following cell is marked with
`width=0` and is not emitted independently. `tui_clear` fills the back buffer
with default single-width spaces.

## Frame timing

Input polling and terminal output both occur inside `tui_present`. Higher
layers that update and draw before calling `tui_present` consume the newly
polled input on their next loop iteration. Call `tui_present` once before the
first UI update if the application needs to prime input immediately.
