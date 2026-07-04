#include "../subcensuszero_i.h"

#include <stdio.h>

#define PLACES_ITEM_NEW CENSUS_MAX_PLACES

static void places_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void subcensus_scene_places_on_enter(void* context) {
    SubCensusApp* app = context;
    Submenu* menu = app->submenu;
    submenu_reset(menu);

    app->place_count = census_place_list(app->storage, app->place_ids, CENSUS_MAX_PLACES);
    for(size_t i = 0; i < app->place_count; i++) {
        char name[CENSUS_PLACE_NAME_LEN];
        census_place_name(app->storage, app->place_ids[i], name, sizeof(name));
        char label[64];
        bool active = strcmp(app->place_ids[i], app->settings.place_id) == 0;
        snprintf(label, sizeof(label), "%s%s", active ? "* " : "  ", name);
        submenu_add_item(menu, label, i, places_cb, app);
    }
    submenu_add_item(menu, "+ New place", PLACES_ITEM_NEW, places_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_places_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == PLACES_ITEM_NEW) {
            app->text_mode = SubCensusTextModeNewPlace;
            app->text_buf[0] = '\0';
            scene_manager_next_scene(app->scene_manager, SubCensusScenePlaceText);
            return true;
        }
        if(event.event < app->place_count) {
            strncpy(app->selected_place, app->place_ids[event.event], CENSUS_PLACE_ID_LEN - 1);
            app->selected_place[CENSUS_PLACE_ID_LEN - 1] = '\0';
            scene_manager_next_scene(app->scene_manager, SubCensusScenePlaceActions);
            return true;
        }
    }
    return false;
}

void subcensus_scene_places_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
