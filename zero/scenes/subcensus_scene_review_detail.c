#include <furi_hal_region.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../shared/core/sc_freq_bands.h"
#include "../shared/core/sc_sub.h"
#include "../subcensuszero_i.h"
#include "census_taxonomy.h"

/* Capture detail (Zero §6): recompute the feature vector from the .sub, run gated k-NN against
 * the brain (System §6), and offer Label + Replay. Advisory only; never auto-relabels. */

enum {
    DetailLabel = 0,
    DetailReplay = 1,
    DetailEdit = 2,
    DetailInfo = 100 /* non-actionable info rows (metadata + extra candidates) */
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

    /* metadata line (§6): preset + edge count from the .sub */
    char meta_line[96];
    snprintf(
        meta_line,
        sizeof(meta_line),
        "%s  %u edges",
        meta.preset[0] ? meta.preset : "?",
        (unsigned)tn);

    /* classify: gated k-NN against the global brain (System §6) -> top-N candidates + source */
    CensusBrain* brain = malloc(sizeof(CensusBrain));
    char header[52];
    char cand_lines[3][44];
    size_t cand_n = 0;
    char mhz[12];
    census_freq_format_mhz(freq, mhz, sizeof(mhz));
    app->review_cand_class[0] = '\0';
    app->review_cand_name[0] = '\0';
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
            const char* dn = brain->fps[idx].device_name ? brain->fps[idx].device_name : "?";
            /* top candidate in the header, with confidence + source (fingerprint) */
            snprintf(
                header, sizeof(header), "%s %s %d%% fp", mhz, dn, (int)(m[0].confidence * 100));
            const char* cls = census_class_id(brain->fps[idx].device_class);
            strncpy(app->review_cand_class, cls, sizeof(app->review_cand_class) - 1);
            app->review_cand_class[sizeof(app->review_cand_class) - 1] = '\0';
            strncpy(app->review_cand_name, dn, sizeof(app->review_cand_name) - 1);
            app->review_cand_name[sizeof(app->review_cand_name) - 1] = '\0';
            /* the remaining candidates (2..N) as info rows with confidence + source */
            for(size_t i = 1; i < k && cand_n < 3; i++) {
                const char* nm =
                    brain->fps[m[i].index].device_name ? brain->fps[m[i].index].device_name : "?";
                snprintf(
                    cand_lines[cand_n++], 44, "  %s %d%% fp", nm, (int)(m[i].confidence * 100));
            }
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
    /* metadata + extra candidates as non-actionable info rows (indices >= DetailInfo) */
    submenu_add_item(menu, meta_line, DetailInfo, detail_cb, app);
    for(size_t i = 0; i < cand_n; i++)
        submenu_add_item(menu, cand_lines[i], DetailInfo + 1 + (uint32_t)i, detail_cb, app);
    submenu_add_item(menu, "Label device", DetailLabel, detail_cb, app);
    submenu_add_item(menu, "Replay to identify", DetailReplay, detail_cb, app);
    submenu_add_item(menu, "Edit / analyze", DetailEdit, detail_cb, app);
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
        /* Replay-to-identify -> the confirm-gated TX flow (§6, No defaulted). */
        app->edit_from_tx = false; /* replay the stored .sub, not an edited frame */
        app->replay_repeat = 1; /* once by default; the confirm lets you pick up to 10 (§6) */
        scene_manager_next_scene(app->scene_manager, SubCensusSceneReplayConfirm);
        return true;
    }
    if(event.event == DetailEdit) {
        /* Edit-before-transmit / field-map discovery (§6, System §7b). */
        app->edit_nbits = 0; /* force a reload of the selected capture */
        scene_manager_next_scene(app->scene_manager, SubCensusSceneEdit);
        return true;
    }
    return false;
}

void subcensus_scene_review_detail_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
