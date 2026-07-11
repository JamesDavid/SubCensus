#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../subcensuszero_i.h"

/* Recon results (Zero §6): the ranked occupancy.csv (freq · peak · occ% · crossings) as
 * non-actionable info rows, then the derived watchlist.csv (freq · modulation · threshold ·
 * source) as actionable entries (Pin / Exclude / Camp-here). A "Reset recon" action (confirm,
 * keep-or-wipe pins) and the empty-state "Run Recon" prompt (§6.1) round it out. */

#define RESULTS_MAX   32
#define RESULTS_RUN   0xFFF0u
#define RESULTS_RESET 0xFFF1u
#define RESULTS_INFO  0xF000u /* occupancy info rows (non-actionable) */

static uint32_t g_freq[RESULTS_MAX];
static char g_src[RESULTS_MAX][12];
static size_t g_n;

static void results_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

/* copy comma-field #idx of a line into dst[cap] */
static void wl_field(const char* line, int idx, char* dst, size_t cap) {
    const char* p = line;
    for(int i = 0; i < idx; i++) {
        while(*p && *p != ',')
            p++;
        if(*p == ',') p++;
    }
    size_t j = 0;
    while(*p && *p != ',' && *p != '\n' && *p != '\r' && j < cap - 1)
        dst[j++] = *p++;
    dst[j] = '\0';
}

/* Add the ranked occupancy.csv as info rows: freq · peak dBm · occ% · crossings (§6). */
static void add_occupancy(SubCensusApp* app, Submenu* menu) {
    char path[160];
    census_place_file(app->settings.place_id, "occupancy.csv", path, sizeof(path));
    File* f = storage_file_alloc(app->storage);
    if(!storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_close(f);
        storage_file_free(f);
        return;
    }
    char line[160];
    size_t li = 0;
    bool header = true;
    char c;
    uint32_t info = RESULTS_INFO;
    int shown = 0;
    while(shown < 6 && storage_file_read(f, &c, 1) == 1) {
        if(c == '\n' || li >= sizeof(line) - 1) {
            line[li] = '\0';
            if(!header && li > 0) {
                /* freq_hz,noise_floor,peak_rssi,occupancy,crossings,last_seen */
                char fs[16], pk[12], occ[12], cx[12];
                wl_field(line, 0, fs, sizeof(fs));
                wl_field(line, 2, pk, sizeof(pk));
                wl_field(line, 3, occ, sizeof(occ));
                wl_field(line, 4, cx, sizeof(cx));
                char mhz[12];
                census_freq_format_mhz((uint32_t)strtoul(fs, NULL, 10), mhz, sizeof(mhz));
                char item[48];
                snprintf(
                    item,
                    sizeof(item),
                    "%s %ddBm %d%% x%s",
                    mhz,
                    (int)strtof(pk, NULL),
                    (int)(strtof(occ, NULL) * 100),
                    cx);
                submenu_add_item(menu, item, info++, results_cb, app);
                shown++;
            }
            header = false;
            li = 0;
        } else if(c != '\r') {
            line[li++] = c;
        }
    }
    storage_file_close(f);
    storage_file_free(f);
}

void subcensus_scene_recon_results_on_enter(void* context) {
    SubCensusApp* app = context;
    subcensus_ensure_recon(app);
    Submenu* menu = app->submenu;
    submenu_reset(menu);
    submenu_set_header(menu, "Recon: occ / watchlist");
    g_n = 0;

    add_occupancy(app, menu); /* ranked occupancy info rows first (§6) */

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
                    /* freq_hz,modulation,threshold_dbm,occupancy,source */
                    char fs[16], mod[8], thr[12], occ[12], src[12];
                    wl_field(line, 0, fs, sizeof(fs));
                    wl_field(line, 1, mod, sizeof(mod));
                    wl_field(line, 2, thr, sizeof(thr));
                    wl_field(line, 3, occ, sizeof(occ));
                    wl_field(line, 4, src, sizeof(src));
                    uint32_t freq = (uint32_t)strtoul(fs, NULL, 10);
                    char mhz[12];
                    census_freq_format_mhz(freq, mhz, sizeof(mhz));
                    const char* tag = (strncmp(src, "user-pin", 8) == 0)      ? "PIN" :
                                      (strncmp(src, "user-exclude", 12) == 0) ? "EXCL" :
                                                                                "";
                    /* freq · modulation · threshold · occ% · source (§6) */
                    char item[48];
                    snprintf(
                        item,
                        sizeof(item),
                        "%s %s %ddBm %d%% %s",
                        mhz,
                        mod,
                        (int)strtof(thr, NULL),
                        (int)(strtof(occ, NULL) * 100),
                        tag);
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
        if(event.event <
           g_n) { /* an actionable watchlist entry (occupancy info rows are >= INFO) */
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
