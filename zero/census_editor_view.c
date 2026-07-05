#include "census_editor_view.h"

#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t mode;
    uint8_t* frame;
    size_t nbits;
    ScFieldMap* map;
    bool* dirty;
    size_t sel;
} CensusEditorModel;

struct CensusEditorView {
    View* view;
};

static size_t item_count(const CensusEditorModel* m) {
    if(m->mode == CensusEditorRaw) return (m->nbits + 7) / 8;
    return m->map ? m->map->n_fields : 0;
}

static void editor_draw(Canvas* canvas, void* model) {
    CensusEditorModel* m = model;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    char buf[48];

    if(m->mode == CensusEditorRaw) {
        canvas_draw_str(canvas, 2, 8, "Raw hex  Up/Dn:byte L/R:+-");
        size_t nbytes = (m->nbits + 7) / 8;
        /* 8 bytes per row */
        for(size_t i = 0; i < nbytes && i < 32; i++) {
            int col = (int)(i % 8), rowi = (int)(i / 8);
            int x = 2 + col * 15, y = 22 + rowi * 10;
            snprintf(buf, sizeof(buf), "%02X", m->frame[i]);
            if(i == m->sel) {
                canvas_draw_box(canvas, x - 1, y - 8, 14, 10);
                canvas_set_color(canvas, ColorWhite);
                canvas_draw_str(canvas, x, y, buf);
                canvas_set_color(canvas, ColorBlack);
            } else {
                canvas_draw_str(canvas, x, y, buf);
            }
        }
        snprintf(buf, sizeof(buf), "byte %u = %u", (unsigned)m->sel, m->frame[m->sel]);
        canvas_draw_str(canvas, 2, 62, buf);
        return;
    }

    /* FIELDS / DISCOVERY: labeled segment list */
    const char* title = (m->mode == CensusEditorFields) ? "Fields L/R:value" :
                                                          "Segments L/R:class";
    canvas_draw_str(canvas, 2, 8, title);
    size_t n = m->map ? m->map->n_fields : 0;
    size_t start = 0;
    if(m->sel >= 5) start = m->sel - 4;
    for(size_t r = 0; r < 5 && start + r < n; r++) {
        size_t i = start + r;
        int y = 18 + (int)r * 9;
        const ScField* f = &m->map->fields[i];
        if(m->mode == CensusEditorFields) {
            uint32_t nbytes = (uint32_t)((m->nbits + 7) / 8);
            uint32_t val = sc_field_get(m->frame, nbytes, f->start_bit, f->length);
            snprintf(
                buf,
                sizeof(buf),
                "%s=%lu %s",
                f->name,
                (unsigned long)val,
                sc_field_class_str(f->cls));
        } else {
            snprintf(
                buf,
                sizeof(buf),
                "%s [%u:%u] %s",
                f->name,
                f->start_bit,
                f->length,
                sc_field_class_str(f->cls));
        }
        if(i == m->sel) {
            canvas_draw_box(canvas, 0, y - 8, 128, 9);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 2, y, buf);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, 2, y, buf);
        }
    }
    if(m->mode == CensusEditorFields && m->map && m->map->has_checksum)
        canvas_draw_str(canvas, 2, 62, "CRC auto-recomputed");
}

static bool editor_input(InputEvent* event, void* context) {
    CensusEditorView* v = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;
    if(event->key == InputKeyBack) return false; /* pop to the Edit menu */

    bool handled = false;
    with_view_model(
        v->view,
        CensusEditorModel * m,
        {
            size_t n = item_count(m);
            if(n == 0) {
                handled = false;
            } else if(event->key == InputKeyUp) {
                if(m->sel > 0) m->sel--;
                handled = true;
            } else if(event->key == InputKeyDown) {
                if(m->sel + 1 < n) m->sel++;
                handled = true;
            } else if(event->key == InputKeyLeft || event->key == InputKeyRight) {
                int delta = (event->key == InputKeyRight) ? 1 : -1;
                if(m->mode == CensusEditorRaw) {
                    m->frame[m->sel] = (uint8_t)(m->frame[m->sel] + delta);
                } else if(m->mode == CensusEditorFields && m->map) {
                    ScField* f = &m->map->fields[m->sel];
                    size_t nbytes = (m->nbits + 7) / 8;
                    uint32_t maxv = (f->length >= 32) ? 0xFFFFFFFFu : ((1u << f->length) - 1u);
                    uint32_t val = sc_field_get(m->frame, nbytes, f->start_bit, f->length);
                    val = (uint32_t)((val + (uint32_t)delta) & maxv);
                    sc_field_set(m->frame, nbytes, f->start_bit, f->length, val);
                    /* keep the frame self-consistent: re-sign after a non-checksum edit */
                    if(m->map->has_checksum && f->cls != SC_FIELD_CHECKSUM)
                        sc_fieldmap_resign(m->map, m->frame, nbytes);
                } else if(m->mode == CensusEditorDiscovery && m->map) {
                    ScField* f = &m->map->fields[m->sel];
                    int c = (int)f->cls + delta;
                    if(c < 0) c = SC_FIELD_DATA;
                    if(c > SC_FIELD_DATA) c = 0;
                    f->cls = (uint8_t)c;
                }
                if(m->dirty) *m->dirty = true;
                handled = true;
            }
        },
        true);
    return handled;
}

CensusEditorView* census_editor_view_alloc(void) {
    CensusEditorView* v = malloc(sizeof(CensusEditorView));
    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(CensusEditorModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, editor_draw);
    view_set_input_callback(v->view, editor_input);
    return v;
}

void census_editor_view_free(CensusEditorView* v) {
    view_free(v->view);
    free(v);
}

View* census_editor_view_get_view(CensusEditorView* v) {
    return v->view;
}

void census_editor_view_configure(
    CensusEditorView* v,
    CensusEditorMode mode,
    uint8_t* frame,
    size_t nbits,
    ScFieldMap* map,
    bool* dirty) {
    with_view_model(
        v->view,
        CensusEditorModel * m,
        {
            m->mode = mode;
            m->frame = frame;
            m->nbits = nbits;
            m->map = map;
            m->dirty = dirty;
            m->sel = 0;
        },
        true);
}
