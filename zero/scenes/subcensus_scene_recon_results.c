#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../subcensuszero_i.h"

/* Recon results (Zero §6): the derived watchlist.csv as a ranked, actionable list. Each entry
 * opens Pin / Exclude / Camp-here (§6). A "Reset recon" action (confirm, keep-or-wipe pins) and
 * the empty-state "Run Recon" prompt (§6.1) round it out. Back returns to the main menu. */

#define RESULTS_MAX   32
#define RESULTS_RUN   0xFFF0u
#define RESULTS_RESET 0xFFF1u

static uint32_t g_freq[RESULTS_MAX];
static char g_src[RESULTS_MAX][12];
static size_t g_n;

static void results_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

/* parse "freq_hz,mod,thr,occ,source" */
static void parse_wl(const char* line, uint32_t* freq, float* occ, char* src, size_t scap) {
    const char* p = line;
    *freq = (uint32_t)strtoul(p, NULL, 10);
    for(int col = 0; col < 3 && *p; col++) {
        while(*p && *p != ',')
            p++;
        if(*p == ',') p++;
    }
    *occ = strtof(p, NULL);
    while(*p && *p != ',')
        p++;
    if(*p == ',') p++;
    size_t j = 0;
    while(*p && *p != ',' && *p != '\n' && *p != '\r' && j < scap - 1)
        src[j++] = *p++;
    src[j] = '\0';
}

void subcensus_scene_recon_results_on_enter(void* context) {
    SubCensusApp* app = context;
    Submenu* menu = app->submenu;
    submenu_reset(menu);
    submenu_set_header(menu, "Recon results");
    g_n = 0;

    char path[160];
    census_place_file(app->settings.place_id, "watchlist.csv", path, sizeof(path));
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[160];
        size_t li = 0;
        bool header = true;
        char c;
        while(g_n < RESULTS_MAX && storage_file_read(f, &c, 1) == 1) {
            if(c == '\n' || li >= sizeof(line) - 1) {
                line[li] = '\0';
                if(!header && li > 0) {
                    uint32_t freq;
                    float occ;
                    char src[12];
                    parse_wl(line, &freq, &occ, src, sizeof(src));
                    char mhz[12];
                    census_freq_format_mhz(freq, mhz, sizeof(mhz));
                    const char* tag = (strncmp(src, "user-pin", 8) == 0)      ? "PIN" :
                                      (strncmp(src, "user-exclude", 12) == 0) ? "EXCL" :
                                                                                "";
                    char item[40];
                    snprintf(item, sizeof(item), "%s %d%% %s", mhz, (int)(occ * 100), tag);
                    g_freq[g_n] = freq;
                    strncpy(g_src[g_n], src, sizeof(g_src[g_n]) - 1);
                    g_src[g_n][sizeof(g_src[g_n]) - 1] = '\0';
                    submenu_add_item(menu, item, g_n, results_cb, app);
                    g_n++;
                }
                header = false;
                li = 0;
            } else if(c != '\r') {
                line[li++] = c;
            }
        }
    }
    storage_file_close(f);
    storage_file_free(f);

    if(g_n == 0) {
        /* empty-state (§6.1): direct Run-Recon action */
        submenu_add_item(menu, "No recon - Run Recon", RESULTS_RUN, results_cb, app);
    } else {
        submenu_add_item(menu, "Reset recon", RESULTS_RESET, results_cb, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_recon_results_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SubCensusSceneStart);
        return true;
    }
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == RESULTS_RUN) {
            app->recon_fresh = false;
            scene_manager_next_scene(app->scene_manager, SubCensusSceneRecon);
            return true;
        }
        if(event.event == RESULTS_RESET) {
            scene_manager_next_scene(app->scene_manager, SubCensusSceneReconReset);
            return true;
        }
        if(event.event < g_n) {
            app->recon_sel_freq = g_freq[event.event];
            strncpy(app->recon_sel_mod, g_src[event.event], sizeof(app->recon_sel_mod) - 1);
            app->recon_sel_mod[sizeof(app->recon_sel_mod) - 1] = '\0';
            scene_manager_next_scene(app->scene_manager, SubCensusSceneReconEntry);
            return true;
        }
    }
    return false;
}

void subcensus_scene_recon_results_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
