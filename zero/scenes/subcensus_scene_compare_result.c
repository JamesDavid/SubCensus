#include <stdio.h>

#include "../subcensuszero_i.h"

/* Compare result (Zero §5.6 stretch): the set difference of two places' watchlists — freqs
 * active-only vs other-only — so you can see "what's here that isn't at home". */

static char g_text[1024];

static bool in_list(const uint32_t* list, size_t n, uint32_t f) {
    for(size_t i = 0; i < n; i++)
        if(list[i] == f) return true;
    return false;
}

static size_t append_diff(
    size_t off,
    const char* title,
    const uint32_t* a,
    size_t na,
    const uint32_t* b,
    size_t nb) {
    off += (size_t)snprintf(g_text + off, sizeof(g_text) - off, "%s\n", title);
    int any = 0;
    for(size_t i = 0; i < na && off < sizeof(g_text) - 16; i++) {
        if(!in_list(b, nb, a[i])) {
            char mhz[12];
            census_freq_format_mhz(a[i], mhz, sizeof(mhz));
            off += (size_t)snprintf(g_text + off, sizeof(g_text) - off, "  %s MHz\n", mhz);
            any = 1;
        }
    }
    if(!any) off += (size_t)snprintf(g_text + off, sizeof(g_text) - off, "  (none)\n");
    return off;
}

void subcensus_scene_compare_result_on_enter(void* context) {
    SubCensusApp* app = context;
    Widget* w = app->widget;
    widget_reset(w);

    uint32_t a[32], b[32];
    size_t na = census_watchlist_freqs(app->storage, app->settings.place_id, a, 32);
    size_t nb = census_watchlist_freqs(app->storage, app->selected_place, b, 32);

    char an[CENSUS_PLACE_NAME_LEN], bn[CENSUS_PLACE_NAME_LEN];
    census_place_name(app->storage, app->settings.place_id, an, sizeof(an));
    census_place_name(app->storage, app->selected_place, bn, sizeof(bn));

    size_t off = 0;
    char t1[48], t2[48];
    snprintf(t1, sizeof(t1), "Only in %s:", an);
    snprintf(t2, sizeof(t2), "Only in %s:", bn);
    off = append_diff(off, t1, a, na, b, nb);
    if(off < sizeof(g_text) - 2) g_text[off++] = '\n';
    off = append_diff(off, t2, b, nb, a, na);
    g_text[off < sizeof(g_text) ? off : sizeof(g_text) - 1] = '\0';

    widget_add_text_scroll_element(w, 0, 0, 128, 64, g_text);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewWidget);
}

bool subcensus_scene_compare_result_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void subcensus_scene_compare_result_on_exit(void* context) {
    SubCensusApp* app = context;
    widget_reset(app->widget);
}
