#include "../subcensuszero_i.h"

#include <stdio.h>

typedef enum {
    StartItemPlace,
    StartItemRunRecon,
    StartItemReconResults,
    StartItemSweep,
    StartItemCamp,
    StartItemReview,
    StartItemCompare,
    StartItemSettings,
    StartItemAbout,
} StartItem;

static void start_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void subcensus_scene_start_on_enter(void* context) {
    SubCensusApp* app = context;
    Submenu* menu = app->submenu;
    submenu_reset(menu);

    char place_name[CENSUS_PLACE_NAME_LEN];
    census_place_name(app->storage, app->settings.place_id, place_name, sizeof(place_name));
    char place_label[64];
    snprintf(place_label, sizeof(place_label), "Place: %s", place_name);

    submenu_add_item(menu, place_label, StartItemPlace, start_cb, app);
    submenu_add_item(menu, "Run Recon", StartItemRunRecon, start_cb, app);
    submenu_add_item(menu, "Recon results", StartItemReconResults, start_cb, app);
    submenu_add_item(menu, "Start Sweep", StartItemSweep, start_cb, app);
    submenu_add_item(menu, "Start Camp", StartItemCamp, start_cb, app);
    submenu_add_item(menu, "Review captures", StartItemReview, start_cb, app);
    submenu_add_item(menu, "Compare places", StartItemCompare, start_cb, app);
    submenu_add_item(menu, "Settings", StartItemSettings, start_cb, app);
    submenu_add_item(menu, "About", StartItemAbout, start_cb, app);

    submenu_set_selected_item(
        menu, scene_manager_get_scene_state(app->scene_manager, SubCensusSceneStart));
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_start_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, SubCensusSceneStart, event.event);
        /* monitoring writes to /ext — if the card vanished, block until it's back (§6.1) */
        bool needs_sd = event.event == StartItemRunRecon || event.event == StartItemSweep ||
                        event.event == StartItemCamp;
        if(needs_sd && !census_sd_present(app->storage)) {
            scene_manager_next_scene(app->scene_manager, SubCensusSceneSdRequired);
            return true;
        }
        switch(event.event) {
        case StartItemPlace:
            scene_manager_next_scene(app->scene_manager, SubCensusScenePlaces);
            return true;
        case StartItemSettings:
            scene_manager_next_scene(app->scene_manager, SubCensusSceneSettings);
            return true;
        case StartItemAbout:
            scene_manager_next_scene(app->scene_manager, SubCensusSceneAbout);
            return true;
        case StartItemRunRecon:
            scene_manager_next_scene(app->scene_manager, SubCensusSceneReconRun);
            return true;
        case StartItemReconResults:
            scene_manager_next_scene(app->scene_manager, SubCensusSceneReconResults);
            return true;
        case StartItemSweep:
            scene_manager_next_scene(app->scene_manager, SubCensusSceneSweep);
            return true;
        case StartItemCamp:
            scene_manager_next_scene(app->scene_manager, SubCensusSceneCampPicker);
            return true;
        case StartItemReview:
            scene_manager_next_scene(app->scene_manager, SubCensusSceneReview);
            return true;
        case StartItemCompare:
            scene_manager_next_scene(app->scene_manager, SubCensusSceneCompare);
            return true;
        default:
            break;
        }
    }
    return false;
}

void subcensus_scene_start_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
