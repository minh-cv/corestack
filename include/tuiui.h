#ifndef TUIUI_TUIUI_H
#define TUIUI_TUIUI_H

#include "tui.h"

#define TUIUI_ID_NONE 0u

#define TUIUI_MAX_PAGES 16u
#define TUIUI_MAX_WINDOWS 32u
#define TUIUI_MAX_CONTROLS 128u
#define TUIUI_CLIP_STACK_MAX 8u

typedef uint32_t TuiUiId;

typedef enum TuiUiWindowFlags {
    TUIUI_WINDOW_NONE = 0,
    TUIUI_WINDOW_BORDER = 1u << 0,
    TUIUI_WINDOW_TITLE = 1u << 1,
    TUIUI_WINDOW_MODAL = 1u << 2,
    TUIUI_WINDOW_MOVABLE = 1u << 3,
    TUIUI_WINDOW_SCROLL_Y = 1u << 4
} TuiUiWindowFlags;

typedef enum TuiUiControlType {
    TUIUI_CONTROL_LABEL = 0,
    TUIUI_CONTROL_BUTTON,
    TUIUI_CONTROL_PAGE_BUTTON,
    TUIUI_CONTROL_CHECKBOX,
    TUIUI_CONTROL_VALUE,
    TUIUI_CONTROL_CUSTOM
} TuiUiControlType;

typedef enum TuiUiControlFlags {
    TUIUI_CONTROL_NONE = 0,
    TUIUI_CONTROL_FOCUSABLE = 1u << 0,
    TUIUI_CONTROL_DISABLED = 1u << 1,
    TUIUI_CONTROL_HIDDEN = 1u << 2
} TuiUiControlFlags;

typedef enum TuiUiStateFlags {
    TUIUI_STATE_NONE = 0,
    TUIUI_STATE_HOT = 1u << 0,
    TUIUI_STATE_FOCUSED = 1u << 1,
    TUIUI_STATE_ACTIVE = 1u << 2,
    TUIUI_STATE_CLICKED = 1u << 3,
    TUIUI_STATE_CHANGED = 1u << 4
} TuiUiStateFlags;

typedef enum TuiUiEventType {
    TUIUI_EVENT_UPDATE = 0,
    TUIUI_EVENT_MOUSE_DOWN,
    TUIUI_EVENT_MOUSE_UP,
    TUIUI_EVENT_CLICK,
    TUIUI_EVENT_ACTION,
    TUIUI_EVENT_FOCUS,
    TUIUI_EVENT_BLUR
} TuiUiEventType;

typedef struct TuiUiStyle {
    uint32_t fg;
    uint32_t bg;
    uint16_t style;
} TuiUiStyle;

typedef struct TuiUiTheme {
    uint32_t screen_bg;
    uint32_t window_bg;
    uint32_t window_border;
    uint32_t title_fg;
    uint32_t text_fg;
    uint32_t muted_fg;
    uint32_t accent_fg;
    uint32_t accent_bg;
    uint32_t hot_bg;
    uint32_t active_bg;
    uint32_t disabled_fg;
} TuiUiTheme;

typedef struct TuiUiInput {
    TuiMouseState mouse;

    bool tab_clicked;
    bool enter_clicked;
    bool space_clicked;
    bool escape_clicked;
    bool left_clicked;
    bool right_clicked;
    bool page_up_clicked;
    bool page_down_clicked;
} TuiUiInput;

typedef struct TuiUiContext TuiUiContext;
typedef struct TuiUiControl TuiUiControl;

typedef struct TuiUiEvent {
    TuiUiEventType type;
    TuiUiInput input;
} TuiUiEvent;

typedef void (*TuiUiCustomDrawFn)(
    TuiUiContext *ui,
    const TuiUiControl *control,
    TgRecti rect,
    void *user_data
);

typedef void (*TuiUiEventFn)(
    TuiUiContext *ui,
    TuiUiControl *control,
    const TuiUiEvent *event,
    void *user_data
);

typedef struct TuiUiPage {
    TuiUiId id;
    const char *title;
} TuiUiPage;

typedef struct TuiUiWindow {
    TuiUiId id;
    TuiUiId page_id;
    TgRecti rect;
    const char *title;
    uint32_t flags;
    int scroll_y;
} TuiUiWindow;

struct TuiUiControl {
    TuiUiId id;
    TuiUiId window_id;
    TuiUiControlType type;
    TgRecti rect;
    uint32_t flags;
    uint32_t state;

    const char *text;
    const char *value;
    TuiUiId target_page;
    bool checked;

    TuiUiCustomDrawFn draw_fn;
    TuiUiEventFn event_fn;
    void *user_data;
};

struct TuiUiContext {
    TuiUiPage pages[TUIUI_MAX_PAGES];
    size_t page_count;

    TuiUiWindow windows[TUIUI_MAX_WINDOWS];
    size_t window_count;

    TuiUiControl controls[TUIUI_MAX_CONTROLS];
    size_t control_count;

    TuiUiTheme theme;
    TuiUiInput input;

    TuiUiId active_page;
    TuiUiId focused_control;
    TuiUiId hot_control;
    TuiUiId captured_control;
    TuiUiId dragging_window;
    int drag_offset_x;
    int drag_offset_y;

    TgRecti clip_stack[TUIUI_CLIP_STACK_MAX];
    size_t clip_depth;
};

TuiUiTheme tuiui_default_theme(void);
void tuiui_init(TuiUiContext *ui);

TgResult tuiui_add_page(TuiUiContext *ui, TuiUiId id, const char *title);
TgResult tuiui_add_window(
    TuiUiContext *ui,
    TuiUiId page_id,
    TuiUiId id,
    TgRecti rect,
    const char *title,
    uint32_t flags
);
TgResult tuiui_add_label(
    TuiUiContext *ui,
    TuiUiId window_id,
    TuiUiId id,
    TgRecti rect,
    const char *text
);
TgResult tuiui_add_button(
    TuiUiContext *ui,
    TuiUiId window_id,
    TuiUiId id,
    TgRecti rect,
    const char *text
);
TgResult tuiui_add_page_button(
    TuiUiContext *ui,
    TuiUiId window_id,
    TuiUiId id,
    TgRecti rect,
    const char *text,
    TuiUiId target_page
);
TgResult tuiui_add_checkbox(
    TuiUiContext *ui,
    TuiUiId window_id,
    TuiUiId id,
    TgRecti rect,
    const char *text,
    bool checked
);
TgResult tuiui_add_value(
    TuiUiContext *ui,
    TuiUiId window_id,
    TuiUiId id,
    TgRecti rect,
    const char *text,
    const char *value
);
TgResult tuiui_add_custom(
    TuiUiContext *ui,
    TuiUiId window_id,
    TuiUiId id,
    TgRecti rect,
    uint32_t flags,
    TuiUiCustomDrawFn draw_fn,
    TuiUiEventFn event_fn,
    void *user_data
);

TgResult tuiui_set_page(TuiUiContext *ui, TuiUiId page_id);
void tuiui_next_page(TuiUiContext *ui);
void tuiui_prev_page(TuiUiContext *ui);
TuiUiId tuiui_active_page(const TuiUiContext *ui);

void tuiui_begin_frame(TuiUiContext *ui);
void tuiui_update(TuiUiContext *ui);
TgResult tuiui_draw(TuiUiContext *ui);
void tuiui_end_frame(TuiUiContext *ui);

const TuiUiInput *tuiui_input(const TuiUiContext *ui);

TuiUiPage *tuiui_find_page(TuiUiContext *ui, TuiUiId id);
TuiUiWindow *tuiui_find_window(TuiUiContext *ui, TuiUiId id);
TuiUiControl *tuiui_find_control(TuiUiContext *ui, TuiUiId id);

TgResult tuiui_set_control_text(TuiUiContext *ui, TuiUiId id, const char *text);
TgResult tuiui_set_control_value(TuiUiContext *ui, TuiUiId id, const char *value);
TgResult tuiui_set_checkbox(TuiUiContext *ui, TuiUiId id, bool checked);

bool tuiui_clicked(const TuiUiContext *ui, TuiUiId id);
bool tuiui_changed(const TuiUiContext *ui, TuiUiId id);
bool tuiui_checked(const TuiUiContext *ui, TuiUiId id);
bool tuiui_focused(const TuiUiContext *ui, TuiUiId id);
bool tuiui_hot(const TuiUiContext *ui, TuiUiId id);
bool tuiui_active(const TuiUiContext *ui, TuiUiId id);

TgResult tuiui_focus(TuiUiContext *ui, TuiUiId control_id);
void tuiui_focus_next(TuiUiContext *ui);
void tuiui_focus_prev(TuiUiContext *ui);

void tuiui_push_clip(TuiUiContext *ui, TgRecti rect);
void tuiui_pop_clip(TuiUiContext *ui);
void tuiui_draw_cell(TuiUiContext *ui, int x, int y, const char *glyph, uint8_t width, TuiUiStyle style);
void tuiui_draw_fill(TuiUiContext *ui, TgRecti rect, TuiUiStyle style);
void tuiui_draw_text(TuiUiContext *ui, int x, int y, const char *text, TuiUiStyle style);
void tuiui_draw_box(TuiUiContext *ui, TgRecti rect, TuiUiStyle border, TuiUiStyle fill);

#endif
