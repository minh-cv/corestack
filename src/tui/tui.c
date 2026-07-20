#define _POSIX_C_SOURCE 200809L

#include "tui.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define TUI_INPUT_BUFFER_CAP 8192u
#define TUI_KEY_RELEASE_TIMEOUT_NS 180000000ll

typedef struct TuiRenderState {
    int cursor_x;
    int cursor_y;
    bool cursor_valid;

    uint16_t style;
    uint32_t fg;
    uint32_t bg;
    bool attrs_valid;
} TuiRenderState;

static TgSizei g_size;
static TuiCell *g_front_buffer;
static TuiCell *g_back_buffer;

static struct termios g_saved_termios;
static int g_saved_stdin_flags;

static bool g_initialized;
static bool g_force_full_redraw;

static uint8_t g_input_buffer[TUI_INPUT_BUFFER_CAP];
static size_t g_input_len;

static uint8_t g_key_count[TUI_KEY_COUNT];
static bool g_key_down_state[TUI_KEY_COUNT];
static bool g_key_clicked_state[TUI_KEY_COUNT];
static bool g_key_repeated_state[TUI_KEY_COUNT];
static int64_t g_key_last_seen_ns[TUI_KEY_COUNT];

static uint8_t g_char_count[256];
static bool g_char_down_state[256];
static bool g_char_clicked_state[256];
static bool g_char_repeated_state[256];
static int64_t g_char_last_seen_ns[256];

static TuiMouseState g_mouse;

static int64_t tui_now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (int64_t)ts.tv_sec * 1000000000ll + (int64_t)ts.tv_nsec;
}

static bool tui_cell_count(TgSizei size, size_t *out_count)
{
    if (size.w <= 0 || size.h <= 0) {
        return false;
    }

    size_t w = (size_t)size.w;
    size_t h = (size_t)size.h;
    if (w > SIZE_MAX / h) {
        return false;
    }

    *out_count = w * h;
    return true;
}

static TuiCell tui_default_cell(void)
{
    TuiCell cell;
    memset(&cell, 0, sizeof(cell));
    cell.ch[0] = ' ';
    cell.width = 1;
    cell.fg = TUI_COLOR_DEFAULT;
    cell.bg = TUI_COLOR_DEFAULT;
    cell.style = TUI_STYLE_NONE;
    return cell;
}

static void tui_fill_buffer(TuiCell *buffer)
{
    if (buffer == NULL) {
        return;
    }

    size_t count = 0;
    if (!tui_cell_count(g_size, &count)) {
        return;
    }

    TuiCell cell = tui_default_cell();
    for (size_t i = 0; i < count; ++i) {
        buffer[i] = cell;
    }
}

static void tui_reset_input_state(void)
{
    memset(g_input_buffer, 0, sizeof(g_input_buffer));
    g_input_len = 0;

    memset(g_key_count, 0, sizeof(g_key_count));
    memset(g_key_down_state, 0, sizeof(g_key_down_state));
    memset(g_key_clicked_state, 0, sizeof(g_key_clicked_state));
    memset(g_key_repeated_state, 0, sizeof(g_key_repeated_state));
    memset(g_key_last_seen_ns, 0, sizeof(g_key_last_seen_ns));

    memset(g_char_count, 0, sizeof(g_char_count));
    memset(g_char_down_state, 0, sizeof(g_char_down_state));
    memset(g_char_clicked_state, 0, sizeof(g_char_clicked_state));
    memset(g_char_repeated_state, 0, sizeof(g_char_repeated_state));
    memset(g_char_last_seen_ns, 0, sizeof(g_char_last_seen_ns));

    memset(&g_mouse, 0, sizeof(g_mouse));
}

static bool tui_valid_key(TuiKey key)
{
    return key > TUI_KEY_NONE && key < TUI_KEY_COUNT;
}

static bool tui_valid_button(TuiMouseButton button)
{
    return button >= TUI_MOUSE_LEFT && button < TUI_MOUSE_BUTTON_COUNT;
}

static void tui_begin_input_frame(void)
{
    memset(g_key_count, 0, sizeof(g_key_count));
    memset(g_key_clicked_state, 0, sizeof(g_key_clicked_state));
    memset(g_key_repeated_state, 0, sizeof(g_key_repeated_state));

    memset(g_char_count, 0, sizeof(g_char_count));
    memset(g_char_clicked_state, 0, sizeof(g_char_clicked_state));
    memset(g_char_repeated_state, 0, sizeof(g_char_repeated_state));

    g_mouse.moved = false;
    memset(g_mouse.clicked, 0, sizeof(g_mouse.clicked));
    memset(g_mouse.released, 0, sizeof(g_mouse.released));
    memset(g_mouse.dragged, 0, sizeof(g_mouse.dragged));
    g_mouse.wheel_x = 0;
    g_mouse.wheel_y = 0;
}

static void tui_release_idle_keys(int64_t now_ns)
{
    if (now_ns <= 0) {
        return;
    }

    for (int i = 1; i < TUI_KEY_COUNT; ++i) {
        if (g_key_down_state[i] &&
            g_key_last_seen_ns[i] > 0 &&
            now_ns - g_key_last_seen_ns[i] > TUI_KEY_RELEASE_TIMEOUT_NS) {
            g_key_down_state[i] = false;
        }
    }

    for (size_t i = 0; i < 256; ++i) {
        if (g_char_down_state[i] &&
            g_char_last_seen_ns[i] > 0 &&
            now_ns - g_char_last_seen_ns[i] > TUI_KEY_RELEASE_TIMEOUT_NS) {
            g_char_down_state[i] = false;
        }
    }
}

static void tui_record_key(TuiKey key, int64_t now_ns)
{
    if (!tui_valid_key(key)) {
        return;
    }

    if (g_key_count[key] < UINT8_MAX) {
        ++g_key_count[key];
    }

    if (g_key_down_state[key]) {
        g_key_repeated_state[key] = true;
    } else {
        g_key_clicked_state[key] = true;
    }

    g_key_down_state[key] = true;
    g_key_last_seen_ns[key] = now_ns;
}

static void tui_record_char(unsigned char ch, int64_t now_ns)
{
    if (g_char_count[ch] < UINT8_MAX) {
        ++g_char_count[ch];
    }

    if (g_char_down_state[ch]) {
        g_char_repeated_state[ch] = true;
    } else {
        g_char_clicked_state[ch] = true;
    }

    g_char_down_state[ch] = true;
    g_char_last_seen_ns[ch] = now_ns;
}

static void tui_record_mouse_position(int x, int y)
{
    if (g_mouse.x != x || g_mouse.y != y) {
        g_mouse.moved = true;
    }

    g_mouse.x = x;
    g_mouse.y = y;
}

static void tui_record_mouse_event(int code, int x1, int y1, char final)
{
    int x = x1 - 1;
    int y = y1 - 1;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }

    int old_x = g_mouse.x;
    int old_y = g_mouse.y;
    tui_record_mouse_position(x, y);

    bool release = final == 'm';
    bool motion = (code & 32) != 0;
    bool wheel = (code & 64) != 0;
    int button = code & 3;

    if (wheel && !release) {
        switch (button) {
        case 0:
            ++g_mouse.wheel_y;
            break;
        case 1:
            --g_mouse.wheel_y;
            break;
        case 2:
            --g_mouse.wheel_x;
            break;
        case 3:
            ++g_mouse.wheel_x;
            break;
        default:
            break;
        }
        return;
    }

    if (release || button == 3) {
        if (button >= 0 && button < TUI_MOUSE_BUTTON_COUNT) {
            if (g_mouse.down[button]) {
                g_mouse.released[button] = true;
            }
            g_mouse.down[button] = false;
        } else {
            for (int i = 0; i < TUI_MOUSE_BUTTON_COUNT; ++i) {
                if (g_mouse.down[i]) {
                    g_mouse.released[i] = true;
                }
                g_mouse.down[i] = false;
            }
        }
        return;
    }

    if (button < 0 || button >= TUI_MOUSE_BUTTON_COUNT) {
        return;
    }

    if (motion) {
        if (g_mouse.down[button] && (old_x != x || old_y != y)) {
            g_mouse.dragged[button] = true;
        }
        return;
    }

    if (!g_mouse.down[button]) {
        g_mouse.clicked[button] = true;
    }
    g_mouse.down[button] = true;
}

static bool tui_is_csi_final(unsigned char ch)
{
    return ch >= 0x40 && ch <= 0x7e;
}

static bool tui_parse_unsigned(const uint8_t *buffer, size_t len, size_t *index, int *out_value)
{
    if (*index >= len || buffer[*index] < '0' || buffer[*index] > '9') {
        return false;
    }

    int value = 0;
    while (*index < len && buffer[*index] >= '0' && buffer[*index] <= '9') {
        int digit = buffer[*index] - '0';
        if (value > (INT_MAX - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
        ++*index;
    }

    *out_value = value;
    return true;
}

static bool tui_parse_sgr_mouse(size_t start, size_t end, int64_t now_ns)
{
    (void)now_ns;

    size_t index = start + 3;
    int code = 0;
    int x = 0;
    int y = 0;

    if (!tui_parse_unsigned(g_input_buffer, end, &index, &code)) {
        return false;
    }
    if (index >= end || g_input_buffer[index] != ';') {
        return false;
    }
    ++index;

    if (!tui_parse_unsigned(g_input_buffer, end, &index, &x)) {
        return false;
    }
    if (index >= end || g_input_buffer[index] != ';') {
        return false;
    }
    ++index;

    if (!tui_parse_unsigned(g_input_buffer, end, &index, &y)) {
        return false;
    }
    if (index != end - 1) {
        return false;
    }

    char final = (char)g_input_buffer[end - 1];
    tui_record_mouse_event(code, x, y, final);
    return true;
}

static void tui_record_csi_key(int number, unsigned char final, int64_t now_ns)
{
    switch (final) {
    case 'A':
        tui_record_key(TUI_KEY_UP, now_ns);
        return;
    case 'B':
        tui_record_key(TUI_KEY_DOWN, now_ns);
        return;
    case 'C':
        tui_record_key(TUI_KEY_RIGHT, now_ns);
        return;
    case 'D':
        tui_record_key(TUI_KEY_LEFT, now_ns);
        return;
    case 'H':
        tui_record_key(TUI_KEY_HOME, now_ns);
        return;
    case 'F':
        tui_record_key(TUI_KEY_END, now_ns);
        return;
    case '~':
        break;
    default:
        return;
    }

    switch (number) {
    case 1:
    case 7:
        tui_record_key(TUI_KEY_HOME, now_ns);
        break;
    case 2:
        tui_record_key(TUI_KEY_INSERT, now_ns);
        break;
    case 3:
        tui_record_key(TUI_KEY_DELETE, now_ns);
        break;
    case 4:
    case 8:
        tui_record_key(TUI_KEY_END, now_ns);
        break;
    case 5:
        tui_record_key(TUI_KEY_PAGE_UP, now_ns);
        break;
    case 6:
        tui_record_key(TUI_KEY_PAGE_DOWN, now_ns);
        break;
    default:
        break;
    }
}

static bool tui_parse_escape(size_t start, size_t *out_consumed, int64_t now_ns)
{
    if (start + 1 >= g_input_len) {
        return false;
    }

    if (g_input_buffer[start + 1] == 'O') {
        if (start + 2 >= g_input_len) {
            return false;
        }

        switch (g_input_buffer[start + 2]) {
        case 'H':
            tui_record_key(TUI_KEY_HOME, now_ns);
            break;
        case 'F':
            tui_record_key(TUI_KEY_END, now_ns);
            break;
        default:
            break;
        }
        *out_consumed = 3;
        return true;
    }

    if (g_input_buffer[start + 1] != '[') {
        tui_record_key(TUI_KEY_ESCAPE, now_ns);
        *out_consumed = 1;
        return true;
    }

    size_t end = start + 2;
    while (end < g_input_len && !tui_is_csi_final(g_input_buffer[end])) {
        ++end;
    }
    if (end >= g_input_len) {
        return false;
    }

    unsigned char final = g_input_buffer[end];
    size_t consumed = end - start + 1;

    if (g_input_buffer[start + 2] == '<' && (final == 'M' || final == 'm')) {
        if (!tui_parse_sgr_mouse(start, end + 1, now_ns)) {
            tui_record_key(TUI_KEY_ESCAPE, now_ns);
        }
        *out_consumed = consumed;
        return true;
    }

    int number = 0;
    size_t index = start + 2;
    if (index < end && g_input_buffer[index] >= '0' && g_input_buffer[index] <= '9') {
        (void)tui_parse_unsigned(g_input_buffer, end, &index, &number);
    }

    tui_record_csi_key(number, final, now_ns);
    *out_consumed = consumed;
    return true;
}

static void tui_parse_input_buffer(int64_t now_ns)
{
    size_t read_index = 0;
    while (read_index < g_input_len) {
        unsigned char ch = g_input_buffer[read_index];

        if (ch == '\x1b') {
            size_t consumed = 0;
            if (tui_parse_escape(read_index, &consumed, now_ns)) {
                read_index += consumed;
                continue;
            }

            tui_record_key(TUI_KEY_ESCAPE, now_ns);
            ++read_index;
            continue;
        }

        switch (ch) {
        case '\r':
        case '\n':
            tui_record_key(TUI_KEY_ENTER, now_ns);
            break;
        case '\t':
            tui_record_key(TUI_KEY_TAB, now_ns);
            break;
        case '\b':
        case 0x7f:
            tui_record_key(TUI_KEY_BACKSPACE, now_ns);
            break;
        case ' ':
            tui_record_key(TUI_KEY_SPACE, now_ns);
            tui_record_char(ch, now_ns);
            break;
        default:
            if (ch >= 32 && ch < 127) {
                tui_record_char(ch, now_ns);
            }
            break;
        }

        ++read_index;
    }

    if (read_index > 0) {
        g_input_len -= read_index;
        memmove(g_input_buffer, g_input_buffer + read_index, g_input_len);
    }
}

static void tui_poll_input(void)
{
    int64_t now_ns = tui_now_ns();
    tui_release_idle_keys(now_ns);
    tui_begin_input_frame();

    for (;;) {
        if (g_input_len >= TUI_INPUT_BUFFER_CAP) {
            break;
        }

        ssize_t nread = read(STDIN_FILENO,
                             g_input_buffer + g_input_len,
                             TUI_INPUT_BUFFER_CAP - g_input_len);
        if (nread > 0) {
            g_input_len += (size_t)nread;
            continue;
        }

        if (nread == 0) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        break;
    }

    tui_parse_input_buffer(now_ns);
    tui_release_idle_keys(now_ns);
}

static bool tui_cell_equal(const TuiCell *a, const TuiCell *b)
{
    return a->width == b->width &&
           a->fg == b->fg &&
           a->bg == b->bg &&
           a->style == b->style &&
           memcmp(a->ch, b->ch, sizeof(a->ch)) == 0;
}

static void tui_emit_move(TuiRenderState *state, int x, int y)
{
    if (state->cursor_valid && state->cursor_x == x && state->cursor_y == y) {
        return;
    }

    fprintf(stdout, "\x1b[%d;%dH", y + 1, x + 1);
    state->cursor_x = x;
    state->cursor_y = y;
    state->cursor_valid = true;
}

static void tui_emit_style(TuiRenderState *state, uint16_t style)
{
    if (state->attrs_valid && state->style == style) {
        return;
    }

    fputs("\x1b[0m", stdout);
    state->style = TUI_STYLE_NONE;
    state->fg = TUI_COLOR_DEFAULT;
    state->bg = TUI_COLOR_DEFAULT;
    state->attrs_valid = true;

    if ((style & TUI_STYLE_BOLD) != 0) {
        fputs("\x1b[1m", stdout);
    }
    if ((style & TUI_STYLE_DIM) != 0) {
        fputs("\x1b[2m", stdout);
    }
    if ((style & TUI_STYLE_ITALIC) != 0) {
        fputs("\x1b[3m", stdout);
    }
    if ((style & TUI_STYLE_UNDERLINE) != 0) {
        fputs("\x1b[4m", stdout);
    }
    if ((style & TUI_STYLE_REVERSE) != 0) {
        fputs("\x1b[7m", stdout);
    }

    state->style = style;
}

static void tui_emit_fg(TuiRenderState *state, uint32_t color)
{
    if (state->attrs_valid && state->fg == color) {
        return;
    }

    if (color == TUI_COLOR_DEFAULT) {
        fputs("\x1b[39m", stdout);
    } else {
        fprintf(stdout,
                "\x1b[38;2;%u;%u;%um",
                (unsigned)((color >> 16) & 0xffu),
                (unsigned)((color >> 8) & 0xffu),
                (unsigned)(color & 0xffu));
    }

    state->fg = color;
    state->attrs_valid = true;
}

static void tui_emit_bg(TuiRenderState *state, uint32_t color)
{
    if (state->attrs_valid && state->bg == color) {
        return;
    }

    if (color == TUI_COLOR_DEFAULT) {
        fputs("\x1b[49m", stdout);
    } else {
        fprintf(stdout,
                "\x1b[48;2;%u;%u;%um",
                (unsigned)((color >> 16) & 0xffu),
                (unsigned)((color >> 8) & 0xffu),
                (unsigned)(color & 0xffu));
    }

    state->bg = color;
    state->attrs_valid = true;
}

static void tui_emit_cell(TuiRenderState *state, const TuiCell *cell, int x, int y)
{
    if (cell->width == 0) {
        return;
    }

    tui_emit_move(state, x, y);
    tui_emit_style(state, cell->style);
    tui_emit_fg(state, cell->fg);
    tui_emit_bg(state, cell->bg);

    const char *glyph = cell->ch[0] == '\0' ? " " : cell->ch;
    fputs(glyph, stdout);

    int width = cell->width == 2 ? 2 : 1;
    if (x + width >= g_size.w) {
        state->cursor_valid = false;
    } else {
        state->cursor_x = x + width;
        state->cursor_y = y;
        state->cursor_valid = true;
    }
}

static TgResult tui_render(void)
{
    TuiRenderState state;
    memset(&state, 0, sizeof(state));
    state.fg = TUI_COLOR_DEFAULT;
    state.bg = TUI_COLOR_DEFAULT;

    for (int y = 0; y < g_size.h; ++y) {
        for (int x = 0; x < g_size.w; ++x) {
            size_t index = (size_t)y * (size_t)g_size.w + (size_t)x;
            const TuiCell *back = &g_back_buffer[index];
            const TuiCell *front = &g_front_buffer[index];

            if (back->width == 0) {
                continue;
            }

            if (g_force_full_redraw || !tui_cell_equal(back, front)) {
                tui_emit_cell(&state, back, x, y);
            }
        }
    }

    fputs("\x1b[0m", stdout);
    fflush(stdout);
    if (ferror(stdout)) {
        clearerr(stdout);
        return TG_ERR;
    }

    size_t count = 0;
    if (!tui_cell_count(g_size, &count)) {
        return TG_ERR_INVALID;
    }
    memcpy(g_front_buffer, g_back_buffer, count * sizeof(TuiCell));
    g_force_full_redraw = false;
    return TG_OK;
}

TgResult tui_init(TgSizei size)
{
    if (g_initialized) {
        return TG_ERR_INVALID;
    }

    size_t count = 0;
    if (!tui_cell_count(size, &count)) {
        return TG_ERR_INVALID;
    }

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        return TG_ERR_UNSUPPORTED;
    }

    TuiCell *front = calloc(count, sizeof(TuiCell));
    TuiCell *back = calloc(count, sizeof(TuiCell));
    if (front == NULL || back == NULL) {
        free(front);
        free(back);
        return TG_ERR_NOMEM;
    }

    if (tcgetattr(STDIN_FILENO, &g_saved_termios) != 0) {
        free(front);
        free(back);
        return TG_ERR;
    }

    g_saved_stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (g_saved_stdin_flags < 0) {
        free(front);
        free(back);
        return TG_ERR;
    }

    struct termios raw = g_saved_termios;
    raw.c_iflag &= (tcflag_t) ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= (tcflag_t) ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        free(front);
        free(back);
        return TG_ERR;
    }

    if (fcntl(STDIN_FILENO, F_SETFL, g_saved_stdin_flags | O_NONBLOCK) != 0) {
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_termios);
        free(front);
        free(back);
        return TG_ERR;
    }

    g_size = size;
    g_front_buffer = front;
    g_back_buffer = back;
    tui_fill_buffer(g_front_buffer);
    tui_fill_buffer(g_back_buffer);
    tui_reset_input_state();

    fputs("\x1b[?1049h", stdout);
    fputs("\x1b[?25l", stdout);
    fputs("\x1b[?1000h", stdout);
    fputs("\x1b[?1002h", stdout);
    fputs("\x1b[?1006h", stdout);
    fputs("\x1b[0m\x1b[2J\x1b[H", stdout);
    fflush(stdout);

    g_initialized = true;
    g_force_full_redraw = true;
    return TG_OK;
}

void tui_shutdown(void)
{
    if (!g_initialized) {
        return;
    }

    fputs("\x1b[?1006l", stdout);
    fputs("\x1b[?1002l", stdout);
    fputs("\x1b[?1000l", stdout);
    fputs("\x1b[0m", stdout);
    fputs("\x1b[?25h", stdout);
    fputs("\x1b[?1049l", stdout);
    fflush(stdout);

    (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_termios);
    (void)fcntl(STDIN_FILENO, F_SETFL, g_saved_stdin_flags);

    free(g_front_buffer);
    free(g_back_buffer);

    g_size = (TgSizei){0, 0};
    g_front_buffer = NULL;
    g_back_buffer = NULL;
    g_initialized = false;
    g_force_full_redraw = false;
    tui_reset_input_state();
}

int tui_width(void)
{
    return g_size.w;
}

int tui_height(void)
{
    return g_size.h;
}

TgSizei tui_size(void)
{
    return g_size;
}

TuiCell *tui_get_buffer(void)
{
    return g_back_buffer;
}

void tui_clear(void)
{
    tui_fill_buffer(g_back_buffer);
}

TgResult tui_present(void)
{
    if (!g_initialized) {
        return TG_ERR_INVALID;
    }

    tui_poll_input();
    return tui_render();
}

bool tui_key_down(TuiKey key)
{
    return tui_valid_key(key) && g_key_down_state[key];
}

bool tui_key_clicked(TuiKey key)
{
    return tui_valid_key(key) && g_key_clicked_state[key];
}

bool tui_key_repeated(TuiKey key)
{
    return tui_valid_key(key) && g_key_repeated_state[key];
}

int tui_key_count(TuiKey key)
{
    if (!tui_valid_key(key)) {
        return 0;
    }
    return g_key_count[key];
}

bool tui_char_down(char ch)
{
    return g_char_down_state[(unsigned char)ch];
}

bool tui_char_clicked(char ch)
{
    return g_char_clicked_state[(unsigned char)ch];
}

bool tui_char_repeated(char ch)
{
    return g_char_repeated_state[(unsigned char)ch];
}

int tui_char_count(char ch)
{
    return g_char_count[(unsigned char)ch];
}

TuiMouseState tui_mouse(void)
{
    return g_mouse;
}

TgVec2i tui_mouse_pos(void)
{
    return (TgVec2i){g_mouse.x, g_mouse.y};
}

bool tui_mouse_down(TuiMouseButton button)
{
    return tui_valid_button(button) && g_mouse.down[button];
}

bool tui_mouse_clicked(TuiMouseButton button)
{
    return tui_valid_button(button) && g_mouse.clicked[button];
}

bool tui_mouse_released(TuiMouseButton button)
{
    return tui_valid_button(button) && g_mouse.released[button];
}

bool tui_mouse_dragged(TuiMouseButton button)
{
    return tui_valid_button(button) && g_mouse.dragged[button];
}
