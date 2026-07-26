#ifndef TUI_TUI_H
#define TUI_TUI_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define TUI_STYLE_NONE      0
#define TUI_STYLE_BOLD      (1u << 0)
#define TUI_STYLE_DIM       (1u << 1)
#define TUI_STYLE_ITALIC    (1u << 2)
#define TUI_STYLE_UNDERLINE (1u << 3)
#define TUI_STYLE_REVERSE   (1u << 4)

#define TUI_COLOR_DEFAULT 0xFFFFFFFFu


typedef enum TgResult {
    TG_OK = 0,
    TG_ERR = -1,
    TG_ERR_INVALID = -2,
    TG_ERR_NOMEM = -3,
    TG_ERR_NOT_FOUND = -4,
    TG_ERR_UNSUPPORTED = -5
} TgResult;

static inline bool tg_result_ok(TgResult result)
{
    return result == TG_OK;
}

static inline bool tg_result_err(TgResult result)
{
    return result != TG_OK;
}


typedef struct TgVec2i {
    int32_t x;
    int32_t y;
} TgVec2i;

typedef struct TgSizei {
    int32_t w;
    int32_t h;
} TgSizei;

typedef struct TgRecti {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} TgRecti;

typedef struct TgStringView {
    const char *data;
    size_t len;
} TgStringView;

typedef struct TgBytes {
    const uint8_t *data;
    size_t len;
} TgBytes;

typedef struct TgMutBytes {
    uint8_t *data;
    size_t len;
} TgMutBytes;

typedef struct TuiCell {
    char ch[8];
    uint8_t width;
    uint32_t fg;
    uint32_t bg;
    uint16_t style;
} TuiCell;

typedef enum TuiKey {
    TUI_KEY_NONE = 0,

    TUI_KEY_ESCAPE,
    TUI_KEY_ENTER,
    TUI_KEY_BACKSPACE,
    TUI_KEY_TAB,
    TUI_KEY_SPACE,

    TUI_KEY_UP,
    TUI_KEY_DOWN,
    TUI_KEY_LEFT,
    TUI_KEY_RIGHT,
    TUI_KEY_HOME,
    TUI_KEY_END,
    TUI_KEY_PAGE_UP,
    TUI_KEY_PAGE_DOWN,
    TUI_KEY_INSERT,
    TUI_KEY_DELETE,

    TUI_KEY_F1,
    TUI_KEY_F2,
    TUI_KEY_F3,
    TUI_KEY_F4,
    TUI_KEY_F5,
    TUI_KEY_F6,
    TUI_KEY_F7,
    TUI_KEY_F8,
    TUI_KEY_F9,
    TUI_KEY_F10,
    TUI_KEY_F11,
    TUI_KEY_F12,

    TUI_KEY_COUNT
} TuiKey;

typedef enum TuiMouseButton {
    TUI_MOUSE_LEFT = 0,
    TUI_MOUSE_MIDDLE,
    TUI_MOUSE_RIGHT,
    TUI_MOUSE_BUTTON_COUNT
} TuiMouseButton;

typedef struct TuiMouseState {
    int x;
    int y;

    bool moved;

    bool down[TUI_MOUSE_BUTTON_COUNT];
    bool clicked[TUI_MOUSE_BUTTON_COUNT];
    bool released[TUI_MOUSE_BUTTON_COUNT];
    bool dragged[TUI_MOUSE_BUTTON_COUNT];

    int wheel_x;
    int wheel_y;
} TuiMouseState;

TgResult tui_init(TgSizei size);
void tui_shutdown(void);

int tui_width(void);
int tui_height(void);
TgSizei tui_size(void);

TuiCell *tui_get_buffer(void);

void tui_clear(void);
TgResult tui_present(void);

bool tui_key_down(TuiKey key);
bool tui_key_clicked(TuiKey key);
bool tui_key_repeated(TuiKey key);
int tui_key_count(TuiKey key);

bool tui_char_down(char ch);
bool tui_char_clicked(char ch);
bool tui_char_repeated(char ch);
int tui_char_count(char ch);

/*
 * Raw bytes copied from STDIN_FILENO before TUI input decoding.
 *
 * The ring persists across frames. Reading consumes bytes in arrival order;
 * clearing discards unread bytes. If the fixed-capacity ring fills, the
 * oldest bytes are overwritten and included in the dropped-byte count.
 */
size_t tui_stdin_available(void);
size_t tui_stdin_read(void *dst, size_t capacity);
void tui_stdin_clear(void);
size_t tui_stdin_dropped(void);

TuiMouseState tui_mouse(void);
TgVec2i tui_mouse_pos(void);

bool tui_mouse_down(TuiMouseButton button);
bool tui_mouse_clicked(TuiMouseButton button);
bool tui_mouse_released(TuiMouseButton button);
bool tui_mouse_dragged(TuiMouseButton button);

#endif
