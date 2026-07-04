#include <stdio.h>

#include "../subcensuszero_i.h"

/* Recon results (Zero §6): the accumulated occupancy.csv + derived watchlist.csv. Reachable
 * after a survey or from the main menu. (Per-entry pin/exclude/camp-here + Reset are a later
 * UI pass; this is the ranked review.) Back jumps to the main menu. */

static char g_results[1536];

static void append_file(SubCensusApp* app, const char* file, const char* title, size_t* off) {
    *off += snprintf(g_results + *off, sizeof(g_results) - *off, "%s\n", title);
    char path[160];
    census_place_file(app->settings.place_id, file, path, sizeof(path));
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char c;
        int lines = 0;
        while(*off < sizeof(g_results) - 1 && lines < 12 && storage_file_read(f, &c, 1) == 1) {
            g_results[(*off)++] = c;
            if(c == '\n') lines++;
        }
    } else {
        *off += snprintf(g_results + *off, sizeof(g_results) - *off, "(none - Run Recon)\n");
    }
    storage_file_close(f);
    storage_file_free(f);
    if(*off < sizeof(g_results) - 2) g_results[(*off)++] = '\n';
    g_results[*off] = '\0';
}

void subcensus_scene_recon_results_on_enter(void* context) {
    SubCensusApp* app = context;
    Widget* w = app->widget;
    widget_reset(w);
    size_t off = 0;
    g_results[0] = '\0';
    append_file(app, "occupancy.csv", "OCCUPANCY freq,nf,pk,occ,cx,seen", &off);
    append_file(app, "watchlist.csv", "WATCHLIST freq,mod,thr,occ,src", &off);
    widget_add_text_scroll_element(w, 0, 0, 128, 64, g_results);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewWidget);
}

bool subcensus_scene_recon_results_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SubCensusSceneStart);
        return true;
    }
    return false;
}

void subcensus_scene_recon_results_on_exit(void* context) {
    SubCensusApp* app = context;
    widget_reset(app->widget);
}
