#include <stdio.h>
#include <string.h>

#include "../subcensuszero_i.h"
#include "census_taxonomy.h"

/* Label picker (Zero §6): accept the top k-NN candidate (when present) or pick from the shared
 * taxonomy (System §5). On confirm: write the `label` column in place in census_log.csv AND
 * append the capture's feature vector to the global fingerprints.csv (source=user) — the
 * active-learning loop (System §6). Never auto-relabels; this is the user's explicit confirm. */

#define LABEL_ACCEPT 0xFFFEu

static void label_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void confirm(SubCensusApp* app, const char* cls) {
    /* 1) rewrite the census_log label in place */
    census_log_set_label(
        app->storage, app->settings.place_id, app->review_subs[app->review_sel], cls);
    /* 2) append the fingerprint to the global brain (source=user) */
    census_brain_confirm_label(app->storage, &app->review_fv, cls, app->review_cand_name);
    notification_message(app->notifications, &sequence_success);
    FURI_LOG_I("SubCensus", "SC scene=review action=label label=%s", cls);
    scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SubCensusSceneReview);
}

void subcensus_scene_review_label_on_enter(void* context) {
    SubCensusApp* app = context;
    Submenu* menu = app->submenu;
    submenu_reset(menu);
    submenu_set_header(menu, "Label device");
    if(app->review_cand_class[0]) {
        char accept[40];
        snprintf(accept, sizeof(accept), "Accept: %s", app->review_cand_class);
        submenu_add_item(menu, accept, LABEL_ACCEPT, label_cb, app);
    }
    for(int i = 0; i < CENSUS_CLASS_COUNT; i++) {
        submenu_add_item(menu, census_class_name((CensusDeviceClass)i), i, label_cb, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_review_label_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == LABEL_ACCEPT && app->review_cand_class[0]) {
        confirm(app, app->review_cand_class);
        return true;
    }
    if(event.event < CENSUS_CLASS_COUNT) {
        confirm(app, census_class_id((CensusDeviceClass)event.event));
        return true;
    }
    return false;
}

void subcensus_scene_review_label_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
