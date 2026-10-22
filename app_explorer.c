/* app_explorer.c -- File Explorer: browse the FAT16 volume, preview files,
 * play WAVs, delete. Each Explorer window is its own task with its own state,
 * so you can open several at once. */

#include "apps.h"
#include "ui.h"
#include "fat16.h"
#include "audio.h"
#include "heap.h"
#include "task.h"
#include "kstd.h"
#include "keyboard.h"
#include "sb16.h"

#define MAX_FILES 96
#define ROW_H 18
#define TOOL_H 30
#define HEAD_H 18
#define PREVIEW_MAX 2048

enum { X_OPEN = 1, X_DELETE, X_REFRESH, X_COPYNAME };

static const menu_item_t file_items[] = {
    { "Open",        X_OPEN,    0, 0 },
    { "Delete",      X_DELETE,  0, "Del" },
    { "Refresh",     X_REFRESH, 0, "F5" },
};
static const menu_item_t edit_items[] = {
    { "Cut",            0,           MI_DISABLED, 0 },
    { "Copy File Name", X_COPYNAME,  0, 0 },
    { "Paste",          0,           MI_DISABLED, 0 },
};
static const menu_item_t view_items[] = {
    { "Refresh", X_REFRESH, 0, "F5" },
};
const app_def_t explorer_def = { "File Explorer", ICON_FOLDER,
    { 3, file_items }, { 3, edit_items }, { 1, view_items } };

typedef struct {
    window_t *win;
    fat16_entry_t ents[MAX_FILES];
    int n, sel, top;
    unsigned free_bytes;
    char preview[PREVIEW_MAX + 1];
    int preview_len, preview_text;
    char status[72];
    int pressed_btn;                 /* toolbar button held down, -1 none */
    int dragging_scroll;
} ex_t;

static void play_task(void *arg)
{
    char *name = (char *)arg;
    audio_play_file(name, 0, 0);
    kfree(name);
}

static void load_preview(ex_t *e)
{
    e->preview_len = 0;
    e->preview_text = 1;
    if (e->sel < 0 || e->sel >= e->n) return;
    fat16_entry_t *f = &e->ents[e->sel];
    if (f->attr & 0x10) { e->preview_text = 0; ksnprintf(e->preview, sizeof(e->preview), "(directory)"); e->preview_len = 11; return; }
    unsigned n = 0;
    if (!fat16_read_file(f->name, (unsigned char *)e->preview, PREVIEW_MAX, &n)) { e->preview_text = 0; return; }
    e->preview_len = (int)n;
    for (unsigned i = 0; i < n; i++) {
        unsigned char c = (unsigned char)e->preview[i];
        if (!(c == '\n' || c == '\r' || c == '\t' || (c >= 32 && c < 127))) { e->preview_text = 0; break; }
    }
    e->preview[n] = 0;
}

static void refresh(ex_t *e)
{
    char keep[13] = "";
    if (e->sel >= 0 && e->sel < e->n) kstrlcpy(keep, e->ents[e->sel].name, sizeof(keep));
    e->n = fat16_read_dir(e->ents, MAX_FILES);
    e->free_bytes = fat16_free_bytes();
    e->sel = -1;
    for (int i = 0; i < e->n; i++) if (keep[0] && kstrcmp(e->ents[i].name, keep) == 0) e->sel = i;
    if (e->top > e->n) e->top = 0;
    load_preview(e);
    ksnprintf(e->status, sizeof(e->status), "%d item%s, %u KB free", e->n, e->n == 1 ? "" : "s", e->free_bytes / 1024);
}

static int is_wav(const char *name)
{
    int l = (int)kstrlen(name);
    return l > 4 && kstricmp(name + l - 4, ".WAV") == 0;
}

static void layout(window_t *win, int *cw, int *ch, int *list_y, int *list_h, int *prev_y, int *prev_h)
{
    wm_client_size(win, cw, ch);
    int ph = *ch * 35 / 100;
    if (ph < 56) ph = 56;
    if (ph > 200) ph = 200;
    *prev_h = ph;
    *prev_y = *ch - ph - 4;
    *list_y = TOOL_H + HEAD_H;
    *list_h = *prev_y - 22 - *list_y;      /* leave room for the "Preview:" caption */
    if (*list_h < ROW_H) *list_h = ROW_H;
}

static void format_size(char *out, int cap, unsigned size)
{
    if (size >= 10240) ksnprintf(out, (unsigned)cap, "%u KB", size / 1024);
    else ksnprintf(out, (unsigned)cap, "%u bytes", size);
}

static void paint(ex_t *e)
{
    window_t *win = e->win;
    surface_t *s = wm_surface(win);
    int cw, ch, list_y, list_h, prev_y, prev_h;
    layout(win, &cw, &ch, &list_y, &list_h, &prev_y, &prev_h);
    if (s->w < cw || s->h < ch) return;

    s_fill(s, 0, 0, cw, ch, COL_LIGHT_GREY);

    /* toolbar */
    static const char *tb[3] = { "Refresh", "Open", "Delete" };
    int has_sel = e->sel >= 0 && e->sel < e->n;
    for (int i = 0; i < 3; i++)
        ui_button(s, 6 + i * 78, 3, 74, 24, tb[i], e->pressed_btn == i, i > 0 && !has_sel);
    s_text(s, 6 + 3 * 78 + 6, 7, e->status, COL_DARK_GREY, COL_NONE);

    /* list */
    s_fill(s, 4, TOOL_H, cw - 8, list_y - TOOL_H + list_h + 4, COL_WHITE);
    s_bevel(s, 4, TOOL_H, cw - 8, list_y - TOOL_H + list_h + 4, 1);
    s_fill(s, 6, TOOL_H + 2, cw - 12 - UI_SCROLL_W, HEAD_H - 2, COL_LIGHT_GREY);
    s_text(s, 30, TOOL_H + 1, "Name", COL_BLACK, COL_NONE);
    s_text(s, cw - 250, TOOL_H + 1, "Size", COL_BLACK, COL_NONE);
    s_text(s, cw - 160, TOOL_H + 1, "Modified", COL_BLACK, COL_NONE);
    int rows = list_h / ROW_H;
    for (int i = 0; i < rows && e->top + i < e->n; i++) {
        fat16_entry_t *f = &e->ents[e->top + i];
        int y = list_y + 2 + i * ROW_H;
        int hot = (e->top + i == e->sel);
        if (hot) s_fill(s, 6, y, cw - 12 - UI_SCROLL_W, ROW_H, COL_BLUE);
        int fg = hot ? COL_WHITE : COL_BLACK;
        icon_draw_small(s, 8, y + 1, (f->attr & 0x10) ? ICON_FOLDER : (is_wav(f->name) ? ICON_DISC : ICON_FILE));
        s_text(s, 30, y + 1, f->name, fg, COL_NONE);
        char b[24];
        if (f->attr & 0x10) kstrlcpy(b, "<DIR>", sizeof(b)); else format_size(b, sizeof(b), f->size);
        s_text(s, cw - 250, y + 1, b, fg, COL_NONE);
        if (f->date) {
            ksnprintf(b, sizeof(b), "%04d-%02d-%02d", 1980 + (f->date >> 9), (f->date >> 5) & 15, f->date & 31);
            s_text(s, cw - 160, y + 1, b, fg, COL_NONE);
        }
    }
    ui_vscroll(s, cw - 6 - UI_SCROLL_W, list_y, list_h + 2, e->n, rows, e->top);

    /* preview */
    s_text(s, 6, prev_y - 20, has_sel ? "Preview:" : "Click a file to preview it:", COL_BLACK, COL_NONE);
    s_fill(s, 4, prev_y, cw - 8, prev_h, COL_BLACK);
    s_bevel(s, 4, prev_y, cw - 8, prev_h, 1);
    if (has_sel) {
        if (!e->preview_text) {
            char m[64];
            ksnprintf(m, sizeof(m), "(binary file, %u bytes)", e->ents[e->sel].size);
            s_text(s, 10, prev_y + 6, m, COL_LIGHT_GREEN, COL_NONE);
            if (is_wav(e->ents[e->sel].name)) s_text(s, 10, prev_y + 26, "Double-click to play.", COL_LIGHT_GREEN, COL_NONE);
        } else {
            int cols = (cw - 20) / FONT_WIDTH, prow = (prev_h - 8) / 16;
            int col = 0, row = 0;
            for (int i = 0; i < e->preview_len && row < prow; i++) {
                char c = e->preview[i];
                if (c == '\r') continue;
                if (c == '\n' || col >= cols) { row++; col = 0; if (c == '\n') continue; if (row >= prow) break; }
                if (c == '\t') c = ' ';
                s_char(s, 10 + col * FONT_WIDTH, prev_y + 4 + row * 16, c, COL_LIGHT_GREEN, COL_NONE);
                col++;
            }
        }
    }
    wm_invalidate(win);
}

static void open_selected(ex_t *e)
{
    if (e->sel < 0 || e->sel >= e->n) return;
    fat16_entry_t *f = &e->ents[e->sel];
    if (is_wav(f->name)) {
        if (!sb16_present()) { kstrlcpy(e->status, "No sound card detected.", sizeof(e->status)); return; }
        char *copy = (char *)kmalloc(16);
        if (copy) {
            kstrlcpy(copy, f->name, 16);
            if (task_create("play", play_task, copy, 0) < 0) kfree(copy);
            else ksnprintf(e->status, sizeof(e->status), "Playing %s", f->name);
        }
    } else {
        load_preview(e);
    }
}

static void delete_selected(ex_t *e)
{
    if (e->sel < 0 || e->sel >= e->n) return;
    char name[13], msg[96];
    kstrlcpy(name, e->ents[e->sel].name, sizeof(name));
    ksnprintf(msg, sizeof(msg), "Delete %s?\nThis cannot be undone.", name);
    if (dlg_message("Delete File", msg, 1, 0) == 0) {
        if (fat16_delete_file(name)) { e->sel = -1; refresh(e); }
        else kstrlcpy(e->status, "Could not delete the file.", sizeof(e->status));
    }
}

static void ensure_visible(ex_t *e, int rows)
{
    if (e->sel < 0) return;
    if (e->sel < e->top) e->top = e->sel;
    if (e->sel >= e->top + rows) e->top = e->sel - rows + 1;
}

void app_explorer_main(void *arg)
{
    ex_t *e = (ex_t *)kzalloc(sizeof(ex_t));
    if (!e) return;
    window_t *win = wm_create("File Explorer", 500, 300, WF_RESIZABLE, &explorer_def);
    if (!win) { kfree(e); return; }
    wm_set_min_size(win, 380, 220);
    e->win = win;
    e->sel = -1;
    e->pressed_btn = -1;
    refresh(e);
    paint(e);

    event_t ev;
    for (;;) {
        wm_next_event(win, &ev, -1);
        int cw, ch, list_y, list_h, prev_y, prev_h;
        layout(win, &cw, &ch, &list_y, &list_h, &prev_y, &prev_h);
        int rows = list_h / ROW_H;
        int maxtop = e->n > rows ? e->n - rows : 0;

        switch (ev.type) {
        case EV_CLOSE:
            wm_destroy(win);
            kfree(e);
            return;
        case EV_RESIZE:
            wm_resize_client(win, ev.a, ev.b);
            break;
        case EV_MOUSE_DOWN:
            if (ev.c != 1) break;
            e->pressed_btn = -1;
            e->dragging_scroll = 0;
            if (ev.b < TOOL_H) {
                for (int i = 0; i < 3; i++)
                    if (ui_inside(ev.a, ev.b, 6 + i * 78, 3, 74, 24)) e->pressed_btn = i;
            } else {
                int np = ui_vscroll_click(cw - 6 - UI_SCROLL_W, list_y, list_h + 2, e->n, rows, e->top, ev.a, ev.b);
                if (np >= 0) { e->top = np; e->dragging_scroll = 1; }
                else if (ev.b >= list_y && ev.b < list_y + list_h && ev.a < cw - 6 - UI_SCROLL_W) {
                    int idx = e->top + (ev.b - list_y - 2) / ROW_H;
                    if (idx >= 0 && idx < e->n) {
                        int same = (idx == e->sel);
                        e->sel = idx;
                        load_preview(e);
                        if (ev.d && same) open_selected(e);
                    }
                }
            }
            break;
        case EV_MOUSE_MOVE:
            if (e->dragging_scroll) {
                int np = ui_vscroll_click(cw - 6 - UI_SCROLL_W, list_y, list_h + 2, e->n, rows, e->top, cw - 6 - UI_SCROLL_W + 4, ev.b);
                if (np >= 0) e->top = np;
            }
            break;
        case EV_MOUSE_UP:
            e->dragging_scroll = 0;
            if (e->pressed_btn >= 0) {
                int b = e->pressed_btn;
                e->pressed_btn = -1;
                if (ui_inside(ev.a, ev.b, 6 + b * 78, 3, 74, 24)) {
                    if (b == 0) refresh(e);
                    else if (b == 1) open_selected(e);
                    else delete_selected(e);
                }
            }
            break;
        case EV_KEY:
            if (ev.a == KEY_DOWN && e->sel < e->n - 1) { e->sel++; ensure_visible(e, rows); load_preview(e); }
            else if (ev.a == KEY_UP && e->sel > 0) { e->sel--; ensure_visible(e, rows); load_preview(e); }
            else if (ev.a == KEY_UP && e->sel < 0 && e->n) { e->sel = 0; load_preview(e); }
            else if (ev.a == KEY_PGDN) e->top += rows;
            else if (ev.a == KEY_PGUP) e->top -= rows;
            else if (ev.a == '\n') open_selected(e);
            else if (ev.a == KEY_DEL) delete_selected(e);
            else if (ev.a == KEY_F5) refresh(e);
            break;
        case EV_MENU:
            if (ev.a == X_OPEN) open_selected(e);
            else if (ev.a == X_DELETE) delete_selected(e);
            else if (ev.a == X_REFRESH) refresh(e);
            else if (ev.a == X_COPYNAME && e->sel >= 0 && e->sel < e->n) clipboard_set(e->ents[e->sel].name);
            break;
        default: break;
        }
        if (e->top < 0) e->top = 0;
        if (e->top > maxtop) e->top = maxtop;
        paint(e);
    }
}
