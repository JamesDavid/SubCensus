#include "census_spectrum_view.h"

#include <gui/elements.h>
#include <stdio.h>

#include "census_recon.h" /* CENSUS_RSSI_NONE */

#define SPEC_DECAY_DB 3.0f /* peak-hold decay per update */

typedef struct {
    uint8_t seg;
    uint32_t lo, hi;
    uint32_t cursor_freq;
    float bars[CENSUS_SPEC_BARS];
    float peak[CENSUS_SPEC_BARS]; /* peak-hold-with-decay */
    size_t nbars;
    bool show_hits;
    bool paged;
    uint32_t hit_freq[CENSUS_SPEC_HITS];
    float hit_peak[CENSUS_SPEC_HITS];
    size_t hit_n;
    uint32_t hot_bins, pass, elapsed_s;
} CensusSpectrumModel;

struct CensusSpectrumView {
    View* view;
    int paged_seg; /* -1 = follow */
    CensusSpectrumBackCallback back_cb;
    void* back_ctx;
};

static const char* seg_label(uint8_t seg) {
    switch(seg) {
    case 0:
        return "300-348";
    case 1:
        return "387-464";
    default:
        return "779-928";
    }
}

/* map dBm (-100..-40) to a bar height in [0, h] */
static int rssi_px(float dbm, int h) {
    if(dbm <= CENSUS_RSSI_NONE) return 0;
    float t = (dbm + 100.0f) / 60.0f;
    if(t < 0) t = 0;
    if(t > 1) t = 1;
    return (int)(t * h);
}

static void spec_draw(Canvas* canvas, void* model) {
    CensusSpectrumModel* m = model;
    canvas_clear(canvas);
    char buf[40];

    if(m->show_hits) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 8, "Top hits (OK: spectrum)");
        for(size_t i = 0; i < m->hit_n && i < 5; i++) {
            snprintf(
                buf,
                sizeof(buf),
                "%lu.%02lu MHz  %d",
                (unsigned long)(m->hit_freq[i] / 1000000),
                (unsigned long)((m->hit_freq[i] % 1000000) / 10000),
                (int)m->hit_peak[i]);
            canvas_draw_str(canvas, 2, 18 + (int)i * 8, buf);
        }
        if(m->hit_n == 0) canvas_draw_str(canvas, 2, 20, "no activity yet");
    } else {
        /* top pane (~40px): spectrum strip */
        const int top = 2, sh = 38;
        canvas_draw_frame(canvas, 0, top, 128, sh);
        int bw = 124 / (int)(m->nbars ? m->nbars : 1);
        if(bw < 1) bw = 1;
        for(size_t i = 0; i < m->nbars; i++) {
            int x = 2 + (int)i * bw;
            int ph = rssi_px(m->peak[i], sh - 2);
            int bh = rssi_px(m->bars[i], sh - 2);
            if(ph > 0) canvas_draw_line(canvas, x, top + sh - 1 - ph, x, top + sh - 2); /* peak */
            if(bh > 0) canvas_draw_box(canvas, x, top + sh - 1 - bh, bw > 1 ? bw - 1 : 1, bh);
        }
        /* cursor at the current sample freq */
        if(m->hi > m->lo) {
            int cx = 2 + (int)(((uint64_t)(m->cursor_freq - m->lo) * 124) / (m->hi - m->lo));
            if(cx >= 2 && cx <= 126) canvas_draw_line(canvas, cx, top, cx, top + sh - 1);
        }
        canvas_set_font(canvas, FontSecondary);
        snprintf(buf, sizeof(buf), "%s%s MHz", seg_label(m->seg), m->paged ? " <>" : "");
        canvas_draw_str(canvas, 3, top + 8, buf);
    }

    /* bottom pane (~24px): status — always visible */
    canvas_set_font(canvas, FontSecondary);
    snprintf(
        buf,
        sizeof(buf),
        "%lu.%02lu  hot %lu",
        (unsigned long)(m->cursor_freq / 1000000),
        (unsigned long)((m->cursor_freq % 1000000) / 10000),
        (unsigned long)m->hot_bins);
    canvas_draw_str(canvas, 2, 50, buf);
    snprintf(
        buf,
        sizeof(buf),
        "pass %lu  %lus  Back:stop",
        (unsigned long)m->pass,
        (unsigned long)m->elapsed_s);
    canvas_draw_str(canvas, 2, 62, buf);
}

static bool spec_input(InputEvent* event, void* context) {
    CensusSpectrumView* v = context;
    if(event->type != InputTypeShort) return false;
    if(event->key == InputKeyOk) {
        with_view_model(v->view, CensusSpectrumModel * m, { m->show_hits = !m->show_hits; }, true);
        return true;
    }
    if(event->key == InputKeyLeft || event->key == InputKeyRight) {
        /* page segments manually (Left/Right); wrap back to follow past the ends */
        if(v->paged_seg < 0) v->paged_seg = 0;
        v->paged_seg += (event->key == InputKeyRight) ? 1 : -1;
        if(v->paged_seg < 0 || v->paged_seg > (int)(CENSUS_RECON_SEGMENTS - 1))
            v->paged_seg = -1; /* release back to auto-follow */
        return true;
    }
    if(event->key == InputKeyBack) {
        if(v->back_cb) v->back_cb(v->back_ctx);
        return false; /* let the scene manager pop */
    }
    return false;
}

CensusSpectrumView* census_spectrum_view_alloc(void) {
    CensusSpectrumView* v = malloc(sizeof(CensusSpectrumView));
    v->view = view_alloc();
    v->paged_seg = -1;
    v->back_cb = NULL;
    v->back_ctx = NULL;
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(CensusSpectrumModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, spec_draw);
    view_set_input_callback(v->view, spec_input);
    return v;
}

void census_spectrum_view_free(CensusSpectrumView* v) {
    view_free(v->view);
    free(v);
}

View* census_spectrum_view_get_view(CensusSpectrumView* v) {
    return v->view;
}

void census_spectrum_view_set_back_callback(
    CensusSpectrumView* v,
    CensusSpectrumBackCallback cb,
    void* ctx) {
    v->back_cb = cb;
    v->back_ctx = ctx;
}

int census_spectrum_view_paged_segment(CensusSpectrumView* v) {
    return v->paged_seg;
}

void census_spectrum_view_update(
    CensusSpectrumView* v,
    uint8_t seg,
    uint32_t lo,
    uint32_t hi,
    uint32_t cursor_freq,
    const float* bars,
    size_t nbars,
    const uint32_t* hit_freqs,
    const float* hit_peaks,
    size_t hit_n,
    uint32_t hot_bins,
    uint32_t pass,
    uint32_t elapsed_s,
    bool paged) {
    with_view_model(
        v->view,
        CensusSpectrumModel * m,
        {
            m->seg = seg;
            m->lo = lo;
            m->hi = hi;
            m->cursor_freq = cursor_freq;
            size_t n = nbars < CENSUS_SPEC_BARS ? nbars : CENSUS_SPEC_BARS;
            m->nbars = n;
            for(size_t i = 0; i < n; i++) {
                m->bars[i] = bars[i];
                float decayed = m->peak[i] - SPEC_DECAY_DB;
                if(bars[i] > decayed)
                    m->peak[i] = bars[i];
                else
                    m->peak[i] = (decayed > CENSUS_RSSI_NONE) ? decayed : CENSUS_RSSI_NONE;
            }
            size_t hn = hit_n < CENSUS_SPEC_HITS ? hit_n : CENSUS_SPEC_HITS;
            for(size_t i = 0; i < hn; i++) {
                m->hit_freq[i] = hit_freqs[i];
                m->hit_peak[i] = hit_peaks[i];
            }
            m->hit_n = hn;
            m->hot_bins = hot_bins;
            m->pass = pass;
            m->elapsed_s = elapsed_s;
            m->paged = paged;
        },
        true);
}
