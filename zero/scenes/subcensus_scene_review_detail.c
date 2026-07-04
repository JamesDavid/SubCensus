#include <furi_hal_region.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../shared/core/sc_freq_bands.h"
#include "../shared/core/sc_sub.h"
#include "../subcensuszero_i.h"

/* Capture detail (Zero §6): recompute the feature vector from the .sub, run gated k-NN against
 * the brain (System §6), and offer Label + Replay. Advisory only; never auto-relabels. */

enum {
    DetailLabel = 0,
    DetailReplay = 1
};

static void detail_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void subcensus_scene_review_detail_on_enter(void* context) {
    SubCensusApp* app = context;

    /* load the .sub -> timings -> feature vector (§5.5) */
    char abs[192];
    census_place_file(app->settings.place_id, app->review_subs[app->review_sel], abs, sizeof(abs));
    char* text = malloc(8192);
    int32_t timings[1024];
    size_t tn = 0;
    ScSubMeta meta = {0};
    if(text) {
        File* f = storage_file_alloc(app->storage);
        size_t rd = 0;
        if(storage_file_open(f, abs, FSAM_READ, FSOM_OPEN_EXISTING)) {
            rd = storage_file_read(f, text, 8191);
            text[rd] = '\0';
            sc_sub_parse(text, rd, &meta, timings, 1024, &tn);
        }
        storage_file_close(f);
        storage_file_free(f);
        free(text);
    }
    uint32_t freq = meta.frequency ? (uint32_t)meta.frequency : app->review_freqs[app->review_sel];
    sc_feature_compute(timings, tn, (int32_t)freq, SC_MOD_OOK, &app->review_fv);

    /* classify: gated k-NN against the global brain (System §6) */
    CensusBrain* brain = malloc(sizeof(CensusBrain));
    char header[52];
    char mhz[12];
    census_freq_format_mhz(freq, mhz, sizeof(mhz));
    if(brain) {
        census_brain_load(app->storage, brain);
        ScKnnQuery q;
        memset(&q, 0, sizeof(q));
        q.fv = app->review_fv;
        q.cadence_class = SC_CADENCE_NONE;
        ScKnnMatch m[3];
        size_t k = sc_knn_match(&q, brain->fps, brain->count, m, 3);
        if(k > 0) {
            int idx = m[0].index;
            snprintf(
                header,
                sizeof(header),
                "%s %s %d%%",
                mhz,
                brain->fps[idx].device_name ? brain->fps[idx].device_name : "?",
                (int)(m[0].confidence * 100));
        } else {
            snprintf(header, sizeof(header), "%s unknown", mhz);
        }
        free(brain);
    } else {
        snprintf(header, sizeof(header), "%s", mhz);
    }

    Submenu* menu = app->submenu;
    submenu_reset(menu);
    submenu_set_header(menu, header);
    submenu_add_item(menu, "Label device", DetailLabel, detail_cb, app);
    submenu_add_item(menu, "Replay to identify", DetailReplay, detail_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_review_detail_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == DetailLabel) {
        scene_manager_next_scene(app->scene_manager, SubCensusSceneReviewLabel);
        return true;
    }
    if(event.event == DetailReplay) {
        uint32_t freq = app->review_freqs[app->review_sel];
        /* Replay is the ONLY TX path — gated by the firmware TX allow-list (§6). Live TX needs
         * hardware (TODO(hw)); the guard is enforced here. */
        bool allowed = sc_freq_in_cc1101_band((int32_t)freq) &&
                       furi_hal_region_is_frequency_allowed(freq);
        if(!allowed) {
            notification_message(app->notifications, &sequence_error);
            FURI_LOG_I(
                "SubCensus",
                "SC scene=review action=replay_blocked freq=%lu",
                (unsigned long)freq);
        } else {
            notification_message(app->notifications, &sequence_blink_magenta_10);
            FURI_LOG_I(
                "SubCensus",
                "SC scene=review action=replay freq=%lu (TODO hw async_tx)",
                (unsigned long)freq);
        }
        return true;
    }
    return false;
}

void subcensus_scene_review_detail_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
