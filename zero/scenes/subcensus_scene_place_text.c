#include "../subcensuszero_i.h"

static void place_text_done(void* context) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, 0);
}

void subcensus_scene_place_text_on_enter(void* context) {
    SubCensusApp* app = context;
    TextInput* ti = app->text_input;
    text_input_reset(ti);
    bool rename = app->text_mode == SubCensusTextModeRenamePlace;
    text_input_set_header_text(ti, rename ? "Rename place" : "New place name");
    text_input_set_result_callback(
        ti, place_text_done, app, app->text_buf, CENSUS_PLACE_NAME_LEN, !rename);
    text_input_set_minimum_length(ti, 1);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewTextInput);
}

bool subcensus_scene_place_text_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(app->text_mode == SubCensusTextModeRenamePlace) {
            census_place_rename(app->storage, app->selected_place, app->text_buf);
            FURI_LOG_I("SubCensus", "SC scene=places action=rename id=%s", app->selected_place);
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, SubCensusScenePlaces);
        } else {
            char id[CENSUS_PLACE_ID_LEN];
            if(census_place_create(app->storage, app->text_buf, id, sizeof(id))) {
                strncpy(app->settings.place_id, id, CENSUS_PLACE_ID_LEN - 1);
                app->settings.place_id[CENSUS_PLACE_ID_LEN - 1] = '\0';
                census_settings_save(app->storage, &app->settings);
                FURI_LOG_I("SubCensus", "SC scene=places action=new id=%s", id);
            }
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, SubCensusSceneStart);
        }
        return true;
    }
    return false;
}

void subcensus_scene_place_text_on_exit(void* context) {
    SubCensusApp* app = context;
    text_input_reset(app->text_input);
}
