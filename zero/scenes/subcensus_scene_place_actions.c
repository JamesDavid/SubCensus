#include "../subcensuszero_i.h"

typedef enum {
    PlaceActionSetActive,
    PlaceActionRename,
    PlaceActionSetLocation,
    PlaceActionDelete,
} PlaceAction;

static void pa_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void subcensus_scene_place_actions_on_enter(void* context) {
    SubCensusApp* app = context;
    Submenu* menu = app->submenu;
    submenu_reset(menu);

    char name[CENSUS_PLACE_NAME_LEN];
    census_place_name(app->storage, app->selected_place, name, sizeof(name));
    submenu_set_header(menu, name);
    submenu_add_item(menu, "Set active", PlaceActionSetActive, pa_cb, app);
    submenu_add_item(menu, "Rename", PlaceActionRename, pa_cb, app);
    submenu_add_item(menu, "Set location", PlaceActionSetLocation, pa_cb, app);
    submenu_add_item(menu, "Delete", PlaceActionDelete, pa_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_place_actions_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case PlaceActionSetActive:
            strncpy(app->settings.place_id, app->selected_place, CENSUS_PLACE_ID_LEN - 1);
            app->settings.place_id[CENSUS_PLACE_ID_LEN - 1] = '\0';
            census_settings_save(app->storage, &app->settings);
            FURI_LOG_I(
                "SubCensus", "SC scene=places action=set_active place=%s", app->settings.place_id);
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, SubCensusSceneStart);
            return true;
        case PlaceActionRename:
            app->text_mode = SubCensusTextModeRenamePlace;
            census_place_name(
                app->storage, app->selected_place, app->text_buf, CENSUS_PLACE_NAME_LEN);
            scene_manager_next_scene(app->scene_manager, SubCensusScenePlaceText);
            return true;
        case PlaceActionSetLocation:
            app->text_mode = SubCensusTextModeSetLocation;
            census_place_location(
                app->storage, app->selected_place, app->text_buf, CENSUS_PLACE_NAME_LEN);
            scene_manager_next_scene(app->scene_manager, SubCensusScenePlaceText);
            return true;
        case PlaceActionDelete:
            scene_manager_next_scene(app->scene_manager, SubCensusSceneConfirmDelete);
            return true;
        default:
            break;
        }
    }
    return false;
}

void subcensus_scene_place_actions_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
