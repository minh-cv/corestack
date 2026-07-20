#include "tuiui.h"

#include <string.h>

#define TUIUI_UTF8_REPLACEMENT "?"

static TuiUiStyle tuiui_style(uint32_t fg, uint32_t bg, uint16_t style)
{
    return (TuiUiStyle){fg, bg, style};
}

static bool tuiui_rect_valid(TgRecti rect)
{
    return rect.w > 0 && rect.h > 0;
}

static bool tuiui_rect_contains(TgRecti rect, int x, int y)
{
    return rect.w > 0 &&
           rect.h > 0 &&
           x >= rect.x &&
           y >= rect.y &&
           x < rect.x + rect.w &&
           y < rect.y + rect.h;
}

static TgRecti tuiui_rect_intersect(TgRecti a, TgRecti b)
{
    int x1 = a.x > b.x ? a.x : b.x;
    int y1 = a.y > b.y ? a.y : b.y;
    int x2a = a.x + a.w;
    int y2a = a.y + a.h;
    int x2b = b.x + b.w;
    int y2b = b.y + b.h;
    int x2 = x2a < x2b ? x2a : x2b;
    int y2 = y2a < y2b ? y2a : y2b;

    if (x2 <= x1 || y2 <= y1) {
        return (TgRecti){x1, y1, 0, 0};
    }
    return (TgRecti){x1, y1, x2 - x1, y2 - y1};
}

static TgRecti tuiui_screen_rect(void)
{
    return (TgRecti){0, 0, tui_width(), tui_height()};
}

static TgRecti tuiui_current_clip(const TuiUiContext *ui)
{
    if (ui == NULL || ui->clip_depth == 0) {
        return tuiui_screen_rect();
    }
    return ui->clip_stack[ui->clip_depth - 1u];
}

static bool tuiui_same_id(TuiUiId a, TuiUiId b)
{
    return a != TUIUI_ID_NONE && a == b;
}

static bool tuiui_window_on_active_page(const TuiUiContext *ui, const TuiUiWindow *window)
{
    return ui != NULL && window != NULL && tuiui_same_id(window->page_id, ui->active_page);
}

static const TuiUiWindow *tuiui_find_window_const(const TuiUiContext *ui, TuiUiId id)
{
    if (ui == NULL || id == TUIUI_ID_NONE) {
        return NULL;
    }

    for (size_t i = 0; i < ui->window_count; ++i) {
        if (ui->windows[i].id == id) {
            return &ui->windows[i];
        }
    }
    return NULL;
}

static const TuiUiControl *tuiui_find_control_const(const TuiUiContext *ui, TuiUiId id)
{
    if (ui == NULL || id == TUIUI_ID_NONE) {
        return NULL;
    }

    for (size_t i = 0; i < ui->control_count; ++i) {
        if (ui->controls[i].id == id) {
            return &ui->controls[i];
        }
    }
    return NULL;
}

static bool tuiui_control_visible(const TuiUiContext *ui, const TuiUiControl *control)
{
    if (ui == NULL || control == NULL || (control->flags & TUIUI_CONTROL_HIDDEN) != 0) {
        return false;
    }

    const TuiUiWindow *window = tuiui_find_window_const(ui, control->window_id);
    return tuiui_window_on_active_page(ui, window);
}

static bool tuiui_control_focusable(const TuiUiContext *ui, const TuiUiControl *control)
{
    return tuiui_control_visible(ui, control) &&
           (control->flags & TUIUI_CONTROL_FOCUSABLE) != 0 &&
           (control->flags & TUIUI_CONTROL_DISABLED) == 0;
}

static TgRecti tuiui_window_content_rect(const TuiUiWindow *window)
{
    if (window == NULL) {
        return (TgRecti){0, 0, 0, 0};
    }

    int inset = (window->flags & TUIUI_WINDOW_BORDER) != 0 ? 1 : 0;
    TgRecti rect = {
        window->rect.x + inset,
        window->rect.y + inset,
        window->rect.w - inset * 2,
        window->rect.h - inset * 2
    };

    if (rect.w < 0) {
        rect.w = 0;
    }
    if (rect.h < 0) {
        rect.h = 0;
    }
    return rect;
}

static TgRecti tuiui_control_screen_rect(const TuiUiContext *ui, const TuiUiControl *control)
{
    const TuiUiWindow *window = tuiui_find_window_const(ui, control != NULL ? control->window_id : 0u);
    if (window == NULL || control == NULL) {
        return (TgRecti){0, 0, 0, 0};
    }

    TgRecti content = tuiui_window_content_rect(window);
    return (TgRecti){
        content.x + control->rect.x,
        content.y + control->rect.y - window->scroll_y,
        control->rect.w,
        control->rect.h
    };
}

static bool tuiui_window_title_hit(const TuiUiWindow *window, int x, int y)
{
    if (window == NULL || (window->flags & TUIUI_WINDOW_MOVABLE) == 0) {
        return false;
    }
    if (!tuiui_rect_contains(window->rect, x, y)) {
        return false;
    }
    if ((window->flags & TUIUI_WINDOW_BORDER) != 0) {
        return y == window->rect.y;
    }
    return y == window->rect.y && (window->flags & TUIUI_WINDOW_TITLE) != 0;
}

static void tuiui_send_event(TuiUiContext *ui, TuiUiControl *control, TuiUiEventType type)
{
    if (ui == NULL || control == NULL || control->event_fn == NULL) {
        return;
    }

    TuiUiEvent event;
    event.type = type;
    event.input = ui->input;
    control->event_fn(ui, control, &event, control->user_data);
}

static void tuiui_sync_focus_state(TuiUiContext *ui)
{
    if (ui == NULL) {
        return;
    }

    for (size_t i = 0; i < ui->control_count; ++i) {
        TuiUiControl *control = &ui->controls[i];
        bool was_focused = (control->state & TUIUI_STATE_FOCUSED) != 0;
        bool is_focused = tuiui_same_id(control->id, ui->focused_control) &&
            tuiui_control_focusable(ui, control);

        if (is_focused) {
            control->state |= TUIUI_STATE_FOCUSED;
        } else {
            control->state &= ~((uint32_t)TUIUI_STATE_FOCUSED);
        }

        if (was_focused && !is_focused) {
            tuiui_send_event(ui, control, TUIUI_EVENT_BLUR);
        } else if (!was_focused && is_focused) {
            tuiui_send_event(ui, control, TUIUI_EVENT_FOCUS);
        }
    }
}

static TgResult tuiui_add_control(
    TuiUiContext *ui,
    TuiUiId window_id,
    TuiUiId id,
    TuiUiControlType type,
    TgRecti rect,
    const char *text,
    const char *value,
    uint32_t flags
)
{
    if (ui == NULL || id == TUIUI_ID_NONE || window_id == TUIUI_ID_NONE || !tuiui_rect_valid(rect)) {
        return TG_ERR_INVALID;
    }
    if (ui->control_count >= TUIUI_MAX_CONTROLS) {
        return TG_ERR_NOMEM;
    }
    if (tuiui_find_window(ui, window_id) == NULL || tuiui_find_control(ui, id) != NULL) {
        return TG_ERR_INVALID;
    }

    TuiUiControl *control = &ui->controls[ui->control_count++];
    memset(control, 0, sizeof(*control));
    control->id = id;
    control->window_id = window_id;
    control->type = type;
    control->rect = rect;
    control->flags = flags;
    control->text = text;
    control->value = value;
    return TG_OK;
}

static TuiUiControl *tuiui_control_at(TuiUiContext *ui, int x, int y)
{
    if (ui == NULL) {
        return NULL;
    }

    for (size_t i = ui->control_count; i > 0; --i) {
        TuiUiControl *control = &ui->controls[i - 1u];
        if (!tuiui_control_focusable(ui, control) && control->type != TUIUI_CONTROL_LABEL &&
            control->type != TUIUI_CONTROL_VALUE && control->type != TUIUI_CONTROL_CUSTOM) {
            continue;
        }
        if ((control->flags & (TUIUI_CONTROL_HIDDEN | TUIUI_CONTROL_DISABLED)) != 0 ||
            !tuiui_control_visible(ui, control)) {
            continue;
        }

        TgRecti rect = tuiui_control_screen_rect(ui, control);
        const TuiUiWindow *window = tuiui_find_window_const(ui, control->window_id);
        TgRecti content = tuiui_window_content_rect(window);
        if (tuiui_rect_contains(tuiui_rect_intersect(rect, content), x, y)) {
            return control;
        }
    }
    return NULL;
}

static TuiUiWindow *tuiui_window_at(TuiUiContext *ui, int x, int y)
{
    if (ui == NULL) {
        return NULL;
    }

    TuiUiWindow *modal = NULL;
    for (size_t i = 0; i < ui->window_count; ++i) {
        TuiUiWindow *window = &ui->windows[i];
        if (tuiui_window_on_active_page(ui, window) && (window->flags & TUIUI_WINDOW_MODAL) != 0) {
            modal = window;
        }
    }

    if (modal != NULL) {
        return tuiui_rect_contains(modal->rect, x, y) ? modal : NULL;
    }

    for (size_t i = ui->window_count; i > 0; --i) {
        TuiUiWindow *window = &ui->windows[i - 1u];
        if (tuiui_window_on_active_page(ui, window) && tuiui_rect_contains(window->rect, x, y)) {
            return window;
        }
    }
    return NULL;
}

static int tuiui_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void tuiui_move_dragged_window(TuiUiContext *ui)
{
    if (ui == NULL || ui->dragging_window == TUIUI_ID_NONE) {
        return;
    }

    TuiUiWindow *window = tuiui_find_window(ui, ui->dragging_window);
    if (window == NULL) {
        ui->dragging_window = TUIUI_ID_NONE;
        return;
    }

    int max_x = tui_width() - window->rect.w;
    int max_y = tui_height() - window->rect.h;
    if (max_x < 0) {
        max_x = 0;
    }
    if (max_y < 0) {
        max_y = 0;
    }

    window->rect.x = tuiui_clamp_int(ui->input.mouse.x - ui->drag_offset_x, 0, max_x);
    window->rect.y = tuiui_clamp_int(ui->input.mouse.y - ui->drag_offset_y, 0, max_y);
}

static void tuiui_activate_control(TuiUiContext *ui, TuiUiControl *control)
{
    if (ui == NULL || control == NULL || (control->flags & TUIUI_CONTROL_DISABLED) != 0) {
        return;
    }

    control->state |= TUIUI_STATE_CLICKED;

    if (control->type == TUIUI_CONTROL_CHECKBOX) {
        control->checked = !control->checked;
        control->state |= TUIUI_STATE_CHANGED;
    }

    tuiui_send_event(ui, control, TUIUI_EVENT_CLICK);
    tuiui_send_event(ui, control, TUIUI_EVENT_ACTION);

    if (control->type == TUIUI_CONTROL_PAGE_BUTTON && control->target_page != TUIUI_ID_NONE) {
        (void)tuiui_set_page(ui, control->target_page);
    }
}

static void tuiui_focus_first_if_needed(TuiUiContext *ui)
{
    if (ui == NULL) {
        return;
    }

    const TuiUiControl *focused = tuiui_find_control_const(ui, ui->focused_control);
    if (tuiui_control_focusable(ui, focused)) {
        tuiui_sync_focus_state(ui);
        return;
    }

    ui->focused_control = TUIUI_ID_NONE;
    for (size_t i = 0; i < ui->control_count; ++i) {
        if (tuiui_control_focusable(ui, &ui->controls[i])) {
            ui->focused_control = ui->controls[i].id;
            break;
        }
    }
    tuiui_sync_focus_state(ui);
}

static bool tuiui_decode_utf8(const char *text, size_t *index, char glyph[8], uint32_t *codepoint)
{
    unsigned char c0 = (unsigned char)text[*index];
    if (c0 == '\0') {
        return false;
    }

    uint32_t cp = 0;
    size_t len = 0;
    if (c0 < 0x80u) {
        cp = c0;
        len = 1u;
    } else if ((c0 & 0xe0u) == 0xc0u) {
        cp = (uint32_t)(c0 & 0x1fu);
        len = 2u;
    } else if ((c0 & 0xf0u) == 0xe0u) {
        cp = (uint32_t)(c0 & 0x0fu);
        len = 3u;
    } else if ((c0 & 0xf8u) == 0xf0u) {
        cp = (uint32_t)(c0 & 0x07u);
        len = 4u;
    } else {
        strcpy(glyph, TUIUI_UTF8_REPLACEMENT);
        *codepoint = '?';
        ++*index;
        return true;
    }

    for (size_t i = 1; i < len; ++i) {
        unsigned char cx = (unsigned char)text[*index + i];
        if ((cx & 0xc0u) != 0x80u) {
            strcpy(glyph, TUIUI_UTF8_REPLACEMENT);
            *codepoint = '?';
            ++*index;
            return true;
        }
        cp = (cp << 6u) | (uint32_t)(cx & 0x3fu);
    }

    memcpy(glyph, text + *index, len);
    glyph[len] = '\0';
    *index += len;
    *codepoint = cp;
    return true;
}

static uint8_t tuiui_codepoint_width(uint32_t cp)
{
    if (cp == 0u) {
        return 0u;
    }
    if (cp < 32u || (cp >= 0x7fu && cp < 0xa0u)) {
        return 0u;
    }
    if ((cp >= 0x1100u && cp <= 0x115fu) ||
        (cp >= 0x2329u && cp <= 0x232au) ||
        (cp >= 0x2e80u && cp <= 0xa4cfu) ||
        (cp >= 0xac00u && cp <= 0xd7a3u) ||
        (cp >= 0xf900u && cp <= 0xfaffu) ||
        (cp >= 0xfe10u && cp <= 0xfe19u) ||
        (cp >= 0xfe30u && cp <= 0xfe6fu) ||
        (cp >= 0xff00u && cp <= 0xff60u) ||
        (cp >= 0xffe0u && cp <= 0xffe6u) ||
        (cp >= 0x1f300u && cp <= 0x1faffu)) {
        return 2u;
    }
    return 1u;
}

static int tuiui_text_width(const char *text)
{
    if (text == NULL) {
        return 0;
    }

    int width = 0;
    size_t index = 0;
    while (text[index] != '\0') {
        char glyph[8] = {0};
        uint32_t cp = 0;
        if (!tuiui_decode_utf8(text, &index, glyph, &cp)) {
            break;
        }
        width += (int)tuiui_codepoint_width(cp);
    }
    return width;
}

static void tuiui_draw_text_clipped(
    TuiUiContext *ui,
    int x,
    int y,
    const char *text,
    TuiUiStyle style,
    int max_width
)
{
    if (text == NULL || max_width <= 0) {
        return;
    }

    int used = 0;
    size_t index = 0;
    while (text[index] != '\0' && used < max_width) {
        char glyph[8] = {0};
        uint32_t cp = 0;
        if (!tuiui_decode_utf8(text, &index, glyph, &cp)) {
            break;
        }

        uint8_t width = tuiui_codepoint_width(cp);
        if (width == 0u) {
            continue;
        }
        if (used + (int)width > max_width) {
            break;
        }

        tuiui_draw_cell(ui, x + used, y, glyph, width, style);
        used += (int)width;
    }
}

static void tuiui_draw_label(TuiUiContext *ui, const TuiUiControl *control, TgRecti rect)
{
    TuiUiStyle style = tuiui_style(ui->theme.text_fg, ui->theme.window_bg, TUI_STYLE_NONE);
    tuiui_draw_text_clipped(ui, rect.x, rect.y, control->text, style, rect.w);
}

static void tuiui_draw_button(TuiUiContext *ui, const TuiUiControl *control, TgRecti rect)
{
    bool hot = (control->state & TUIUI_STATE_HOT) != 0;
    bool focused = (control->state & TUIUI_STATE_FOCUSED) != 0;
    bool active = (control->state & TUIUI_STATE_ACTIVE) != 0 ||
        (control->type == TUIUI_CONTROL_PAGE_BUTTON && control->target_page == ui->active_page);
    bool disabled = (control->flags & TUIUI_CONTROL_DISABLED) != 0;

    uint32_t bg = active ? ui->theme.active_bg : (hot || focused ? ui->theme.hot_bg : ui->theme.window_bg);
    uint32_t fg = disabled ? ui->theme.disabled_fg : ui->theme.text_fg;
    uint32_t border = focused || active ? ui->theme.accent_fg : ui->theme.window_border;
    TuiUiStyle fill = tuiui_style(fg, bg, TUI_STYLE_NONE);
    TuiUiStyle border_style = tuiui_style(border, bg, focused ? TUI_STYLE_BOLD : TUI_STYLE_NONE);

    tuiui_draw_box(ui, rect, border_style, fill);
    if (rect.w <= 2 || rect.h <= 0) {
        return;
    }

    int text_width = tuiui_text_width(control->text);
    int text_x = rect.x + (rect.w - text_width) / 2;
    int text_y = rect.y + rect.h / 2;
    if (text_x < rect.x + 1) {
        text_x = rect.x + 1;
    }
    tuiui_draw_text_clipped(ui, text_x, text_y, control->text, fill, rect.w - 2);
}

static void tuiui_draw_checkbox(TuiUiContext *ui, const TuiUiControl *control, TgRecti rect)
{
    bool hot = (control->state & TUIUI_STATE_HOT) != 0;
    bool focused = (control->state & TUIUI_STATE_FOCUSED) != 0;
    bool disabled = (control->flags & TUIUI_CONTROL_DISABLED) != 0;

    TuiUiStyle style = tuiui_style(
        disabled ? ui->theme.disabled_fg : (hot || focused ? ui->theme.accent_fg : ui->theme.text_fg),
        ui->theme.window_bg,
        focused ? TUI_STYLE_BOLD : TUI_STYLE_NONE
    );
    const char *mark = control->checked ? "[x]" : "[ ]";
    tuiui_draw_text_clipped(ui, rect.x, rect.y, mark, style, rect.w);
    if (rect.w > 4) {
        tuiui_draw_text_clipped(ui, rect.x + 4, rect.y, control->text, style, rect.w - 4);
    }
}

static void tuiui_draw_value(TuiUiContext *ui, const TuiUiControl *control, TgRecti rect)
{
    TuiUiStyle label = tuiui_style(ui->theme.muted_fg, ui->theme.window_bg, TUI_STYLE_NONE);
    TuiUiStyle value = tuiui_style(ui->theme.accent_fg, ui->theme.window_bg, TUI_STYLE_BOLD);
    int value_width = tuiui_text_width(control->value);
    int value_x = rect.x + rect.w - value_width;
    int label_width = rect.w;

    if (value_x < rect.x) {
        value_x = rect.x;
        label_width = 0;
    } else {
        label_width = value_x - rect.x - 1;
    }

    tuiui_draw_text_clipped(ui, rect.x, rect.y, control->text, label, label_width);
    tuiui_draw_text_clipped(ui, value_x, rect.y, control->value, value, rect.w - (value_x - rect.x));
}

static void tuiui_draw_control(TuiUiContext *ui, TuiUiControl *control)
{
    if (!tuiui_control_visible(ui, control)) {
        return;
    }

    const TuiUiWindow *window = tuiui_find_window_const(ui, control->window_id);
    TgRecti rect = tuiui_control_screen_rect(ui, control);
    TgRecti content = tuiui_window_content_rect(window);
    TgRecti clip = tuiui_rect_intersect(tuiui_current_clip(ui), content);
    tuiui_push_clip(ui, clip);

    switch (control->type) {
    case TUIUI_CONTROL_LABEL:
        tuiui_draw_label(ui, control, rect);
        break;
    case TUIUI_CONTROL_BUTTON:
    case TUIUI_CONTROL_PAGE_BUTTON:
        tuiui_draw_button(ui, control, rect);
        break;
    case TUIUI_CONTROL_CHECKBOX:
        tuiui_draw_checkbox(ui, control, rect);
        break;
    case TUIUI_CONTROL_VALUE:
        tuiui_draw_value(ui, control, rect);
        break;
    case TUIUI_CONTROL_CUSTOM:
        if (control->draw_fn != NULL) {
            control->draw_fn(ui, control, rect, control->user_data);
        }
        break;
    default:
        break;
    }

    tuiui_pop_clip(ui);
}

static void tuiui_draw_window(TuiUiContext *ui, TuiUiWindow *window)
{
    TuiUiStyle fill = tuiui_style(ui->theme.text_fg, ui->theme.window_bg, TUI_STYLE_NONE);
    TuiUiStyle border = tuiui_style(ui->theme.window_border, ui->theme.window_bg, TUI_STYLE_NONE);
    TuiUiStyle title = tuiui_style(ui->theme.title_fg, ui->theme.window_bg, TUI_STYLE_BOLD);

    if ((window->flags & TUIUI_WINDOW_BORDER) != 0) {
        tuiui_draw_box(ui, window->rect, border, fill);
    } else {
        tuiui_draw_fill(ui, window->rect, fill);
    }

    if ((window->flags & TUIUI_WINDOW_TITLE) != 0 && window->title != NULL && window->rect.w > 4) {
        int title_x = window->rect.x + 2;
        if ((window->flags & TUIUI_WINDOW_BORDER) == 0) {
            title_x = window->rect.x;
        }
        tuiui_draw_text_clipped(ui, title_x, window->rect.y, window->title, title, window->rect.w - 4);
    }

    TgRecti content = tuiui_window_content_rect(window);
    tuiui_push_clip(ui, content);
    for (size_t i = 0; i < ui->control_count; ++i) {
        if (ui->controls[i].window_id == window->id) {
            tuiui_draw_control(ui, &ui->controls[i]);
        }
    }
    tuiui_pop_clip(ui);
}

TuiUiTheme tuiui_default_theme(void)
{
    TuiUiTheme theme;
    theme.screen_bg = 0x101214u;
    theme.window_bg = 0x181c20u;
    theme.window_border = 0x5b6470u;
    theme.title_fg = 0xf4f7fau;
    theme.text_fg = 0xe5e7ebu;
    theme.muted_fg = 0x9ca3afu;
    theme.accent_fg = 0x67e8f9u;
    theme.accent_bg = 0x155e75u;
    theme.hot_bg = 0x26323au;
    theme.active_bg = 0x7c2d12u;
    theme.disabled_fg = 0x6b7280u;
    return theme;
}

void tuiui_init(TuiUiContext *ui)
{
    if (ui == NULL) {
        return;
    }

    memset(ui, 0, sizeof(*ui));
    ui->theme = tuiui_default_theme();
}

TgResult tuiui_add_page(TuiUiContext *ui, TuiUiId id, const char *title)
{
    if (ui == NULL || id == TUIUI_ID_NONE || tuiui_find_page(ui, id) != NULL) {
        return TG_ERR_INVALID;
    }
    if (ui->page_count >= TUIUI_MAX_PAGES) {
        return TG_ERR_NOMEM;
    }

    TuiUiPage *page = &ui->pages[ui->page_count++];
    page->id = id;
    page->title = title;
    if (ui->active_page == TUIUI_ID_NONE) {
        ui->active_page = id;
    }
    return TG_OK;
}

TgResult tuiui_add_window(
    TuiUiContext *ui,
    TuiUiId page_id,
    TuiUiId id,
    TgRecti rect,
    const char *title,
    uint32_t flags
)
{
    if (ui == NULL || id == TUIUI_ID_NONE || page_id == TUIUI_ID_NONE || !tuiui_rect_valid(rect)) {
        return TG_ERR_INVALID;
    }
    if (ui->window_count >= TUIUI_MAX_WINDOWS) {
        return TG_ERR_NOMEM;
    }
    if (tuiui_find_page(ui, page_id) == NULL || tuiui_find_window(ui, id) != NULL) {
        return TG_ERR_INVALID;
    }

    TuiUiWindow *window = &ui->windows[ui->window_count++];
    memset(window, 0, sizeof(*window));
    window->id = id;
    window->page_id = page_id;
    window->rect = rect;
    window->title = title;
    window->flags = flags;
    return TG_OK;
}

TgResult tuiui_add_label(TuiUiContext *ui, TuiUiId window_id, TuiUiId id, TgRecti rect, const char *text)
{
    return tuiui_add_control(ui, window_id, id, TUIUI_CONTROL_LABEL, rect, text, NULL, TUIUI_CONTROL_NONE);
}

TgResult tuiui_add_button(TuiUiContext *ui, TuiUiId window_id, TuiUiId id, TgRecti rect, const char *text)
{
    return tuiui_add_control(
        ui,
        window_id,
        id,
        TUIUI_CONTROL_BUTTON,
        rect,
        text,
        NULL,
        TUIUI_CONTROL_FOCUSABLE
    );
}

TgResult tuiui_add_page_button(
    TuiUiContext *ui,
    TuiUiId window_id,
    TuiUiId id,
    TgRecti rect,
    const char *text,
    TuiUiId target_page
)
{
    if (ui == NULL || tuiui_find_page(ui, target_page) == NULL) {
        return TG_ERR_INVALID;
    }

    TgResult result = tuiui_add_control(
        ui,
        window_id,
        id,
        TUIUI_CONTROL_PAGE_BUTTON,
        rect,
        text,
        NULL,
        TUIUI_CONTROL_FOCUSABLE
    );
    if (result != TG_OK) {
        return result;
    }

    TuiUiControl *control = tuiui_find_control(ui, id);
    control->target_page = target_page;
    return TG_OK;
}

TgResult tuiui_add_checkbox(
    TuiUiContext *ui,
    TuiUiId window_id,
    TuiUiId id,
    TgRecti rect,
    const char *text,
    bool checked
)
{
    TgResult result = tuiui_add_control(
        ui,
        window_id,
        id,
        TUIUI_CONTROL_CHECKBOX,
        rect,
        text,
        NULL,
        TUIUI_CONTROL_FOCUSABLE
    );
    if (result != TG_OK) {
        return result;
    }

    TuiUiControl *control = tuiui_find_control(ui, id);
    control->checked = checked;
    return TG_OK;
}

TgResult tuiui_add_value(
    TuiUiContext *ui,
    TuiUiId window_id,
    TuiUiId id,
    TgRecti rect,
    const char *text,
    const char *value
)
{
    return tuiui_add_control(ui, window_id, id, TUIUI_CONTROL_VALUE, rect, text, value, TUIUI_CONTROL_NONE);
}

TgResult tuiui_add_custom(
    TuiUiContext *ui,
    TuiUiId window_id,
    TuiUiId id,
    TgRecti rect,
    uint32_t flags,
    TuiUiCustomDrawFn draw_fn,
    TuiUiEventFn event_fn,
    void *user_data
)
{
    TgResult result = tuiui_add_control(ui, window_id, id, TUIUI_CONTROL_CUSTOM, rect, NULL, NULL, flags);
    if (result != TG_OK) {
        return result;
    }

    TuiUiControl *control = tuiui_find_control(ui, id);
    control->draw_fn = draw_fn;
    control->event_fn = event_fn;
    control->user_data = user_data;
    return TG_OK;
}

TgResult tuiui_set_page(TuiUiContext *ui, TuiUiId page_id)
{
    if (ui == NULL || tuiui_find_page(ui, page_id) == NULL) {
        return TG_ERR_INVALID;
    }

    ui->active_page = page_id;
    ui->captured_control = TUIUI_ID_NONE;
    ui->hot_control = TUIUI_ID_NONE;
    ui->dragging_window = TUIUI_ID_NONE;
    tuiui_focus_first_if_needed(ui);
    return TG_OK;
}

void tuiui_next_page(TuiUiContext *ui)
{
    if (ui == NULL || ui->page_count == 0u) {
        return;
    }

    for (size_t i = 0; i < ui->page_count; ++i) {
        if (ui->pages[i].id == ui->active_page) {
            size_t next = (i + 1u) % ui->page_count;
            (void)tuiui_set_page(ui, ui->pages[next].id);
            return;
        }
    }
    (void)tuiui_set_page(ui, ui->pages[0].id);
}

void tuiui_prev_page(TuiUiContext *ui)
{
    if (ui == NULL || ui->page_count == 0u) {
        return;
    }

    for (size_t i = 0; i < ui->page_count; ++i) {
        if (ui->pages[i].id == ui->active_page) {
            size_t prev = i == 0u ? ui->page_count - 1u : i - 1u;
            (void)tuiui_set_page(ui, ui->pages[prev].id);
            return;
        }
    }
    (void)tuiui_set_page(ui, ui->pages[0].id);
}

TuiUiId tuiui_active_page(const TuiUiContext *ui)
{
    return ui != NULL ? ui->active_page : TUIUI_ID_NONE;
}

void tuiui_begin_frame(TuiUiContext *ui)
{
    if (ui == NULL) {
        return;
    }

    ui->input.mouse = tui_mouse();
    ui->input.tab_clicked = tui_key_clicked(TUI_KEY_TAB);
    ui->input.enter_clicked = tui_key_clicked(TUI_KEY_ENTER);
    ui->input.space_clicked = tui_key_clicked(TUI_KEY_SPACE);
    ui->input.escape_clicked = tui_key_clicked(TUI_KEY_ESCAPE);
    ui->input.left_clicked = tui_key_clicked(TUI_KEY_LEFT);
    ui->input.right_clicked = tui_key_clicked(TUI_KEY_RIGHT);
    ui->input.page_up_clicked = tui_key_clicked(TUI_KEY_PAGE_UP);
    ui->input.page_down_clicked = tui_key_clicked(TUI_KEY_PAGE_DOWN);

    ui->hot_control = TUIUI_ID_NONE;
    ui->clip_depth = 0;

    for (size_t i = 0; i < ui->control_count; ++i) {
        ui->controls[i].state &= (uint32_t)TUIUI_STATE_FOCUSED;
    }
    tuiui_focus_first_if_needed(ui);
}

void tuiui_update(TuiUiContext *ui)
{
    if (ui == NULL) {
        return;
    }

    if (ui->input.page_down_clicked || ui->input.right_clicked) {
        tuiui_next_page(ui);
    } else if (ui->input.page_up_clicked || ui->input.left_clicked) {
        tuiui_prev_page(ui);
    }

    TuiUiWindow *window_under_mouse = tuiui_window_at(ui, ui->input.mouse.x, ui->input.mouse.y);
    if (window_under_mouse != NULL &&
        (window_under_mouse->flags & TUIUI_WINDOW_SCROLL_Y) != 0 &&
        ui->input.mouse.wheel_y != 0) {
        window_under_mouse->scroll_y -= ui->input.mouse.wheel_y;
        if (window_under_mouse->scroll_y < 0) {
            window_under_mouse->scroll_y = 0;
        }
    }

    if (ui->dragging_window != TUIUI_ID_NONE) {
        if (ui->input.mouse.down[TUI_MOUSE_LEFT]) {
            tuiui_move_dragged_window(ui);
        }
        if (ui->input.mouse.released[TUI_MOUSE_LEFT]) {
            ui->dragging_window = TUIUI_ID_NONE;
        }
        return;
    }

    TuiUiControl *hover = tuiui_control_at(ui, ui->input.mouse.x, ui->input.mouse.y);
    if (hover != NULL) {
        ui->hot_control = hover->id;
        hover->state |= TUIUI_STATE_HOT;
    }

    if (ui->input.mouse.clicked[TUI_MOUSE_LEFT]) {
        if (window_under_mouse != NULL &&
            hover == NULL &&
            tuiui_window_title_hit(window_under_mouse, ui->input.mouse.x, ui->input.mouse.y)) {
            ui->dragging_window = window_under_mouse->id;
            ui->drag_offset_x = ui->input.mouse.x - window_under_mouse->rect.x;
            ui->drag_offset_y = ui->input.mouse.y - window_under_mouse->rect.y;
            tuiui_move_dragged_window(ui);
            return;
        }

        if (hover != NULL && tuiui_control_focusable(ui, hover)) {
            (void)tuiui_focus(ui, hover->id);
            ui->captured_control = hover->id;
            hover->state |= TUIUI_STATE_ACTIVE;
            tuiui_send_event(ui, hover, TUIUI_EVENT_MOUSE_DOWN);
        }
    }

    if (ui->captured_control != TUIUI_ID_NONE) {
        TuiUiControl *captured = tuiui_find_control(ui, ui->captured_control);
        if (captured != NULL) {
            captured->state |= TUIUI_STATE_ACTIVE;
            if (ui->input.mouse.released[TUI_MOUSE_LEFT]) {
                tuiui_send_event(ui, captured, TUIUI_EVENT_MOUSE_UP);
                if (hover != NULL && hover->id == captured->id) {
                    tuiui_activate_control(ui, captured);
                }
                ui->captured_control = TUIUI_ID_NONE;
            }
        } else {
            ui->captured_control = TUIUI_ID_NONE;
        }
    }

    if (ui->input.tab_clicked) {
        tuiui_focus_next(ui);
    }

    if (ui->input.enter_clicked || ui->input.space_clicked) {
        TuiUiControl *focused = tuiui_find_control(ui, ui->focused_control);
        if (tuiui_control_focusable(ui, focused)) {
            tuiui_activate_control(ui, focused);
        }
    }

    for (size_t i = 0; i < ui->control_count; ++i) {
        if (ui->controls[i].type == TUIUI_CONTROL_CUSTOM && tuiui_control_visible(ui, &ui->controls[i])) {
            tuiui_send_event(ui, &ui->controls[i], TUIUI_EVENT_UPDATE);
        }
    }
}

TgResult tuiui_draw(TuiUiContext *ui)
{
    if (ui == NULL || tui_get_buffer() == NULL || tui_width() <= 0 || tui_height() <= 0) {
        return TG_ERR_INVALID;
    }

    ui->clip_depth = 0;
    tuiui_push_clip(ui, tuiui_screen_rect());
    tuiui_draw_fill(
        ui,
        tuiui_screen_rect(),
        tuiui_style(ui->theme.text_fg, ui->theme.screen_bg, TUI_STYLE_NONE)
    );

    for (size_t i = 0; i < ui->window_count; ++i) {
        if (tuiui_window_on_active_page(ui, &ui->windows[i])) {
            tuiui_draw_window(ui, &ui->windows[i]);
        }
    }

    ui->clip_depth = 0;
    return TG_OK;
}

void tuiui_end_frame(TuiUiContext *ui)
{
    (void)ui;
}

const TuiUiInput *tuiui_input(const TuiUiContext *ui)
{
    return ui != NULL ? &ui->input : NULL;
}

TuiUiPage *tuiui_find_page(TuiUiContext *ui, TuiUiId id)
{
    if (ui == NULL || id == TUIUI_ID_NONE) {
        return NULL;
    }

    for (size_t i = 0; i < ui->page_count; ++i) {
        if (ui->pages[i].id == id) {
            return &ui->pages[i];
        }
    }
    return NULL;
}

TuiUiWindow *tuiui_find_window(TuiUiContext *ui, TuiUiId id)
{
    if (ui == NULL || id == TUIUI_ID_NONE) {
        return NULL;
    }

    for (size_t i = 0; i < ui->window_count; ++i) {
        if (ui->windows[i].id == id) {
            return &ui->windows[i];
        }
    }
    return NULL;
}

TuiUiControl *tuiui_find_control(TuiUiContext *ui, TuiUiId id)
{
    if (ui == NULL || id == TUIUI_ID_NONE) {
        return NULL;
    }

    for (size_t i = 0; i < ui->control_count; ++i) {
        if (ui->controls[i].id == id) {
            return &ui->controls[i];
        }
    }
    return NULL;
}

TgResult tuiui_set_control_text(TuiUiContext *ui, TuiUiId id, const char *text)
{
    TuiUiControl *control = tuiui_find_control(ui, id);
    if (control == NULL) {
        return TG_ERR_NOT_FOUND;
    }

    control->text = text;
    return TG_OK;
}

TgResult tuiui_set_control_value(TuiUiContext *ui, TuiUiId id, const char *value)
{
    TuiUiControl *control = tuiui_find_control(ui, id);
    if (control == NULL) {
        return TG_ERR_NOT_FOUND;
    }

    control->value = value;
    return TG_OK;
}

TgResult tuiui_set_checkbox(TuiUiContext *ui, TuiUiId id, bool checked)
{
    TuiUiControl *control = tuiui_find_control(ui, id);
    if (control == NULL || control->type != TUIUI_CONTROL_CHECKBOX) {
        return TG_ERR_NOT_FOUND;
    }

    if (control->checked != checked) {
        control->checked = checked;
        control->state |= TUIUI_STATE_CHANGED;
    }
    return TG_OK;
}

bool tuiui_clicked(const TuiUiContext *ui, TuiUiId id)
{
    const TuiUiControl *control = tuiui_find_control_const(ui, id);
    return control != NULL && (control->state & TUIUI_STATE_CLICKED) != 0;
}

bool tuiui_changed(const TuiUiContext *ui, TuiUiId id)
{
    const TuiUiControl *control = tuiui_find_control_const(ui, id);
    return control != NULL && (control->state & TUIUI_STATE_CHANGED) != 0;
}

bool tuiui_checked(const TuiUiContext *ui, TuiUiId id)
{
    const TuiUiControl *control = tuiui_find_control_const(ui, id);
    return control != NULL && control->type == TUIUI_CONTROL_CHECKBOX && control->checked;
}

bool tuiui_focused(const TuiUiContext *ui, TuiUiId id)
{
    const TuiUiControl *control = tuiui_find_control_const(ui, id);
    return control != NULL && (control->state & TUIUI_STATE_FOCUSED) != 0;
}

bool tuiui_hot(const TuiUiContext *ui, TuiUiId id)
{
    const TuiUiControl *control = tuiui_find_control_const(ui, id);
    return control != NULL && (control->state & TUIUI_STATE_HOT) != 0;
}

bool tuiui_active(const TuiUiContext *ui, TuiUiId id)
{
    const TuiUiControl *control = tuiui_find_control_const(ui, id);
    return control != NULL && (control->state & TUIUI_STATE_ACTIVE) != 0;
}

TgResult tuiui_focus(TuiUiContext *ui, TuiUiId control_id)
{
    TuiUiControl *control = tuiui_find_control(ui, control_id);
    if (!tuiui_control_focusable(ui, control)) {
        return TG_ERR_INVALID;
    }

    ui->focused_control = control_id;
    tuiui_sync_focus_state(ui);
    return TG_OK;
}

void tuiui_focus_next(TuiUiContext *ui)
{
    if (ui == NULL) {
        return;
    }

    TuiUiId first = TUIUI_ID_NONE;
    bool choose_next = ui->focused_control == TUIUI_ID_NONE;
    for (size_t i = 0; i < ui->control_count; ++i) {
        TuiUiControl *control = &ui->controls[i];
        if (!tuiui_control_focusable(ui, control)) {
            continue;
        }
        if (first == TUIUI_ID_NONE) {
            first = control->id;
        }
        if (choose_next) {
            ui->focused_control = control->id;
            tuiui_sync_focus_state(ui);
            return;
        }
        if (control->id == ui->focused_control) {
            choose_next = true;
        }
    }

    ui->focused_control = first;
    tuiui_sync_focus_state(ui);
}

void tuiui_focus_prev(TuiUiContext *ui)
{
    if (ui == NULL) {
        return;
    }

    TuiUiId last = TUIUI_ID_NONE;
    TuiUiId previous = TUIUI_ID_NONE;
    for (size_t i = 0; i < ui->control_count; ++i) {
        TuiUiControl *control = &ui->controls[i];
        if (!tuiui_control_focusable(ui, control)) {
            continue;
        }
        if (control->id == ui->focused_control && previous != TUIUI_ID_NONE) {
            ui->focused_control = previous;
            tuiui_sync_focus_state(ui);
            return;
        }
        previous = control->id;
        last = control->id;
    }

    ui->focused_control = last;
    tuiui_sync_focus_state(ui);
}

void tuiui_push_clip(TuiUiContext *ui, TgRecti rect)
{
    if (ui == NULL || ui->clip_depth >= TUIUI_CLIP_STACK_MAX) {
        return;
    }

    TgRecti clip = tuiui_rect_intersect(rect, tuiui_screen_rect());
    if (ui->clip_depth > 0u) {
        clip = tuiui_rect_intersect(clip, ui->clip_stack[ui->clip_depth - 1u]);
    }
    ui->clip_stack[ui->clip_depth++] = clip;
}

void tuiui_pop_clip(TuiUiContext *ui)
{
    if (ui != NULL && ui->clip_depth > 0u) {
        --ui->clip_depth;
    }
}

void tuiui_draw_cell(TuiUiContext *ui, int x, int y, const char *glyph, uint8_t width, TuiUiStyle style)
{
    if (ui == NULL || width == 0u || tui_get_buffer() == NULL) {
        return;
    }

    TgRecti clip = tuiui_current_clip(ui);
    if (!tuiui_rect_contains(clip, x, y)) {
        return;
    }
    if (width == 2u && !tuiui_rect_contains(clip, x + 1, y)) {
        return;
    }
    if (x < 0 || y < 0 || x >= tui_width() || y >= tui_height()) {
        return;
    }

    TuiCell *buffer = tui_get_buffer();
    size_t index = (size_t)y * (size_t)tui_width() + (size_t)x;
    TuiCell *cell = &buffer[index];
    memset(cell, 0, sizeof(*cell));
    if (glyph == NULL || glyph[0] == '\0') {
        cell->ch[0] = ' ';
    } else {
        strncpy(cell->ch, glyph, sizeof(cell->ch) - 1u);
        cell->ch[sizeof(cell->ch) - 1u] = '\0';
    }
    cell->width = width == 2u ? 2u : 1u;
    cell->fg = style.fg;
    cell->bg = style.bg;
    cell->style = style.style;

    if (cell->width == 2u && x + 1 < tui_width()) {
        TuiCell *tail = &buffer[index + 1u];
        memset(tail, 0, sizeof(*tail));
        tail->width = 0u;
        tail->fg = style.fg;
        tail->bg = style.bg;
        tail->style = style.style;
    }
}

void tuiui_draw_fill(TuiUiContext *ui, TgRecti rect, TuiUiStyle style)
{
    if (ui == NULL || tui_get_buffer() == NULL) {
        return;
    }

    TgRecti clip = tuiui_rect_intersect(rect, tuiui_current_clip(ui));
    for (int y = clip.y; y < clip.y + clip.h; ++y) {
        for (int x = clip.x; x < clip.x + clip.w; ++x) {
            tuiui_draw_cell(ui, x, y, " ", 1u, style);
        }
    }
}

void tuiui_draw_text(TuiUiContext *ui, int x, int y, const char *text, TuiUiStyle style)
{
    tuiui_draw_text_clipped(ui, x, y, text, style, tui_width() - x);
}

void tuiui_draw_box(TuiUiContext *ui, TgRecti rect, TuiUiStyle border, TuiUiStyle fill)
{
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }

    tuiui_draw_fill(ui, rect, fill);

    if (rect.w == 1 && rect.h == 1) {
        tuiui_draw_cell(ui, rect.x, rect.y, "+", 1u, border);
        return;
    }

    for (int x = rect.x; x < rect.x + rect.w; ++x) {
        tuiui_draw_cell(ui, x, rect.y, "-", 1u, border);
        tuiui_draw_cell(ui, x, rect.y + rect.h - 1, "-", 1u, border);
    }
    for (int y = rect.y; y < rect.y + rect.h; ++y) {
        tuiui_draw_cell(ui, rect.x, y, "|", 1u, border);
        tuiui_draw_cell(ui, rect.x + rect.w - 1, y, "|", 1u, border);
    }

    tuiui_draw_cell(ui, rect.x, rect.y, "+", 1u, border);
    tuiui_draw_cell(ui, rect.x + rect.w - 1, rect.y, "+", 1u, border);
    tuiui_draw_cell(ui, rect.x, rect.y + rect.h - 1, "+", 1u, border);
    tuiui_draw_cell(ui, rect.x + rect.w - 1, rect.y + rect.h - 1, "+", 1u, border);
}
