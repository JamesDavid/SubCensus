#include "../subcensuszero_i.h"
#include "census_taxonomy.h"

/* Label picker (Zero §6): pick from the shared taxonomy (System §5). On confirm, append the
 * capture's feature vector to the global fingerprints.csv (source=user) — the active-learning
 * loop (System §6). Never auto-relabels; this is the user's explicit confirm. */

static void label_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void subcensus_scene_review_label_on_enter(void* context) {
    SubCensusApp* app = context;
    Submenu* menu = app->submenu;
    submenu_reset(menu);
    submenu_set_header(menu, "Label device");
    for(int i = 0; i < CENSUS_CLASS_COUNT; i++) {
        submenu_add_item(menu, census_class_name((CensusDeviceClass)i), i, label_cb, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_review_label_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event < CENSUS_CLASS_COUNT) {
        const char* cls = census_class_id((CensusDeviceClass)event.event);
        census_brain_confirm_label(app->storage, &app->review_fv, cls, "");
        notification_message(app->notifications, &sequence_success);
        FURI_LOG_I("SubCensus", "SC scene=review action=label label=%s", cls);
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, SubCensusSceneReview);
        return true;
    }
    return false;
}

void subcensus_scene_review_label_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
