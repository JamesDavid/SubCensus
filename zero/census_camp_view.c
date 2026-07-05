#include "census_camp_view.h"

#include <gui/elements.h>
#include <stdio.h>

#define CAMP_RECENT 8

typedef struct {
    bool sweep;
    bool show_list;
    uint32_t freq_hz;
    float rssi;
    uint32_t hits;
    CensusHit recent[CAMP_RECENT];
    size_t recent_len;
    size_t sel; /* selected row in the recent-hits list */
    bool sd_low; /* SD full banner (§6.1) */
    uint32_t elapsed_s; /* elapsed monitor time (§6) */
    bool rec; /* "REC" overlay just after a capture (§6) */
    bool paused; /* paused via long-press OK (§6) */
    uint8_t pos, count; /* sweep position in the list (§6 cursor); count 0 = camp */
} CensusCampModel;

struct CensusCampView {
    View* view;
    CensusCampViewBackCallback back_cb;
    void* back_ctx;
    CensusCampViewJumpCallback jump_cb;
    void* jump_ctx;
    CensusCampViewPauseCallback pause_cb;
    void* pause_ctx;
};

static void census_freq_mhz(uint32_t hz, char* buf, size_t cap) {
    snprintf(
        buf,
        cap,
        "%lu.%02lu",
        (unsigned long)(hz / 1000000),
        (unsigned long)((hz % 1000000) / 10000));
}

static void census_camp_draw(Canvas* canvas, void* model) {
    CensusCampModel* m = model;
    canvas_clear(canvas);
    char buf[32];

    if(m->show_list) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 8, "Hits OK:Review Back:live");
        /* window of up to 5 rows around the selection */
        size_t start = 0;
        if(m->sel >= 5) start = m->sel - 4;
        for(size_t r = 0; r < 5 && start + r < m->recent_len; r++) {
            size_t i = start + r;
            int y = 18 + (int)r * 9;
            census_freq_mhz(m->recent[i].freq_hz, buf, sizeof(buf));
            char line[72];
            snprintf(
                line,
                sizeof(line),
                "%s %d %s",
                buf,
                (int)m->recent[i].rssi_dbm,
                m->recent[i].match);
            if(i == m->sel) {
                canvas_draw_box(canvas, 0, y - 8, 128, 9);
                canvas_set_color(canvas, ColorWhite);
                canvas_draw_str(canvas, 2, y, line);
                canvas_set_color(canvas, ColorBlack);
            } else {
                canvas_draw_str(canvas, 2, y, line);
            }
        }
        if(m->recent_len == 0) canvas_draw_str(canvas, 2, 20, "none yet");
        return;
    }

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, m->sweep ? "SWEEP" : "CAMP");
    census_freq_mhz(m->freq_hz, buf, sizeof(buf));
    char freq_line[56];
    /* sweep: cursor shows the active freq's position in the list (§6) */
    if(m->sweep && m->count)
        snprintf(freq_line, sizeof(freq_line), "%s MHz %u/%u", buf, m->pos, m->count);
    else
        snprintf(freq_line, sizeof(freq_line), "%s MHz", buf);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 44, 11, freq_line);

    /* "● REC" overlay during a capture window; "PAUSE" when held (§6) */
    if(m->paused) {
        canvas_draw_str(canvas, 98, 11, "PAUSE");
    } else if(m->rec) {
        canvas_draw_disc(canvas, 122, 8, 3);
        canvas_draw_str(canvas, 104, 11, "REC");
    }

    /* RSSI bar: map -100..-40 dBm to 0..124 px */
    int pct = (int)((m->rssi + 100.0f) * 124.0f / 60.0f);
    if(pct < 0) pct = 0;
    if(pct > 124) pct = 124;
    canvas_draw_frame(canvas, 2, 18, 124, 9);
    canvas_draw_box(canvas, 3, 19, pct, 7);
    snprintf(buf, sizeof(buf), "RSSI %d dBm", (int)m->rssi);
    canvas_draw_str(canvas, 2, 37, buf);
    snprintf(buf, sizeof(buf), "%lus", (unsigned long)m->elapsed_s);
    canvas_draw_str(canvas, 92, 37, buf);

    /* last match/unknown tag (§6) */
    snprintf(buf, sizeof(buf), "Last: %s", m->recent_len ? m->recent[0].match : "-");
    canvas_draw_str(canvas, 2, 47, buf);

    snprintf(buf, sizeof(buf), "Hits: %lu", (unsigned long)m->hits);
    canvas_draw_str(canvas, 2, 57, buf);
    canvas_draw_str(canvas, 70, 57, "OK: hits");
    if(m->sd_low)
        canvas_draw_str(canvas, 2, 64, "SD LOW: blips only");
    else
        canvas_draw_str(canvas, 2, 64, "Back: stop");
}

static bool census_camp_input(InputEvent* event, void* context) {
    CensusCampView* v = context;

    bool in_list = false;
    with_view_model(v->view, CensusCampModel * m, { in_list = m->show_list; }, false);

    /* long-press OK toggles pause/resume in the live view (§6, optional) */
    if(event->type == InputTypeLong && event->key == InputKeyOk && !in_list) {
        bool paused = false;
        with_view_model(
            v->view,
            CensusCampModel * m,
            {
                m->paused = !m->paused;
                paused = m->paused;
            },
            true);
        if(v->pause_cb) v->pause_cb(v->pause_ctx, paused);
        return true;
    }
    if(event->type != InputTypeShort) return false;

    if(!in_list) {
        if(event->key == InputKeyOk) {
            /* enter the recent-hits list (§6) */
            with_view_model(
                v->view,
                CensusCampModel * m,
                {
                    m->show_list = true;
                    m->sel = 0;
                },
                true);
            return true;
        }
        if(event->key == InputKeyBack) {
            if(v->back_cb) v->back_cb(v->back_ctx);
            return false; /* let the scene manager pop the scene (stop) */
        }
        return false;
    }

    /* in the list: Up/Down scroll, OK jumps to Review, Back returns to the live view */
    if(event->key == InputKeyUp || event->key == InputKeyDown) {
        with_view_model(
            v->view,
            CensusCampModel * m,
            {
                if(m->recent_len > 0) {
                    if(event->key == InputKeyUp && m->sel > 0)
                        m->sel--;
                    else if(event->key == InputKeyDown && m->sel + 1 < m->recent_len)
                        m->sel++;
                }
            },
            true);
        return true;
    }
    if(event->key == InputKeyOk) {
        uint32_t freq = 0;
        with_view_model(
            v->view,
            CensusCampModel * m,
            {
                if(m->sel < m->recent_len) freq = m->recent[m->sel].freq_hz;
            },
            false);
        if(freq && v->jump_cb) v->jump_cb(v->jump_ctx, freq);
        return true;
    }
    if(event->key == InputKeyBack) {
        with_view_model(v->view, CensusCampModel * m, { m->show_list = false; }, true);
        return true; /* consume — stay in the live view, don't pop */
    }
    return false;
}

CensusCampView* census_camp_view_alloc(void) {
    CensusCampView* v = malloc(sizeof(CensusCampView));
    v->view = view_alloc();
    v->back_cb = NULL;
    v->back_ctx = NULL;
    v->jump_cb = NULL;
    v->jump_ctx = NULL;
    v->pause_cb = NULL;
    v->pause_ctx = NULL;
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(CensusCampModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, census_camp_draw);
    view_set_input_callback(v->view, census_camp_input);
    return v;
}

void census_camp_view_free(CensusCampView* v) {
    view_free(v->view);
    free(v);
}

View* census_camp_view_get_view(CensusCampView* v) {
    return v->view;
}

void census_camp_view_set_back_callback(
    CensusCampView* v,
    CensusCampViewBackCallback cb,
    void* ctx) {
    v->back_cb = cb;
    v->back_ctx = ctx;
}

void census_camp_view_set_jump_callback(
    CensusCampView* v,
    CensusCampViewJumpCallback cb,
    void* ctx) {
    v->jump_cb = cb;
    v->jump_ctx = ctx;
}

void census_camp_view_set_pause_callback(
    CensusCampView* v,
    CensusCampViewPauseCallback cb,
    void* ctx) {
    v->pause_cb = cb;
    v->pause_ctx = ctx;
}

void census_camp_view_set_low(CensusCampView* v, bool low) {
    with_view_model(v->view, CensusCampModel * m, { m->sd_low = low; }, true);
}

void census_camp_view_set_status(
    CensusCampView* v,
    uint32_t elapsed_s,
    bool rec,
    uint8_t pos,
    uint8_t count) {
    with_view_model(
        v->view,
        CensusCampModel * m,
        {
            m->elapsed_s = elapsed_s;
            m->rec = rec;
            m->pos = pos;
            m->count = count;
        },
        true);
}

void census_camp_view_update(
    CensusCampView* v,
    bool sweep,
    uint32_t freq_hz,
    float rssi,
    uint32_t hits,
    const CensusHit* recent,
    size_t recent_len) {
    with_view_model(
        v->view,
        CensusCampModel * m,
        {
            m->sweep = sweep;
            m->freq_hz = freq_hz;
            m->rssi = rssi;
            m->hits = hits;
            size_t n = recent_len < CAMP_RECENT ? recent_len : CAMP_RECENT;
            for(size_t i = 0; i < n; i++)
                m->recent[i] = recent[i];
            m->recent_len = n;
        },
        true);
}
