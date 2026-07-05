#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../subcensuszero_i.h"

/* Review captures (Zero §6): list census_log rows (freq · match/label). Select -> detail. */

static bool review_field(const char* line, int idx, char* dst, size_t cap) {
    const char* p = line;
    for(int i = 0; i < idx; i++) {
        while(*p && *p != ',' && *p != '\n')
            p++;
        if(*p != ',') return false;
        p++;
    }
    const char* e = p;
    while(*e && *e != ',' && *e != '\n' && *e != '\r')
        e++;
    size_t n = (size_t)(e - p);
    if(n >= cap) n = cap - 1;
    memcpy(dst, p, n);
    dst[n] = '\0';
    return true;
}

static void review_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void subcensus_scene_review_on_enter(void* context) {
    SubCensusApp* app = context;
    Submenu* menu = app->submenu;
    submenu_reset(menu);
    submenu_set_header(menu, "Review captures");
    app->review_count = 0;

    char path[160];
    census_place_file(app->settings.place_id, "census_log.csv", path, sizeof(path));
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[256];
        size_t li = 0;
        bool header = true;
        char c;
        while(app->review_count < CENSUS_REVIEW_MAX && storage_file_read(f, &c, 1) == 1) {
            if(c == '\n' || li >= sizeof(line) - 1) {
                line[li] = '\0';
                if(!header && li > 5) {
                    char freq[16], match[32], sub[80], label[24];
                    review_field(line, 1, freq, sizeof(freq));
                    review_field(line, 8, match, sizeof(match));
                    review_field(line, 12, sub, sizeof(sub));
                    review_field(line, 13, label, sizeof(label));
                    uint32_t fhz = (uint32_t)strtoul(freq, NULL, 10);
                    char mhz[12];
                    census_freq_format_mhz(fhz, mhz, sizeof(mhz));
                    const char* tag = label[0] ? label : (match[0] ? match : "unknown");
                    char item[52];
                    snprintf(item, sizeof(item), "%s %s", mhz, tag);
                    strncpy(app->review_subs[app->review_count], sub, 79);
                    app->review_subs[app->review_count][79] = '\0';
                    app->review_freqs[app->review_count] = fhz;
                    submenu_add_item(menu, item, app->review_count, review_cb, app);
                    app->review_count++;
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

    if(app->review_count == 0) {
        submenu_add_item(menu, "No captures yet", 999, review_cb, app);
    }

    /* live-list jump (§6): preselect the newest row at the jumped-to frequency */
    if(app->review_jump_freq) {
        for(size_t i = app->review_count; i > 0; i--) {
            if(app->review_freqs[i - 1] == app->review_jump_freq) {
                submenu_set_selected_item(menu, (uint32_t)(i - 1));
                break;
            }
        }
        app->review_jump_freq = 0;
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_review_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event < app->review_count) {
        app->review_sel = event.event;
        scene_manager_next_scene(app->scene_manager, SubCensusSceneReviewDetail);
        return true;
    }
    return false;
}

void subcensus_scene_review_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
