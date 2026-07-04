#include "../subcensuszero_i.h"

#include <stdio.h>

/* Destructive-action confirm with No/Cancel defaulted (Zero §6). Delete removes the place
 * folder only — never signatures/ (Zero §5.6). */

static void confirm_result(DialogExResult result, void* context) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void subcensus_scene_confirm_delete_on_enter(void* context) {
    SubCensusApp* app = context;
    DialogEx* d = app->dialog_ex;
    char name[CENSUS_PLACE_NAME_LEN];
    census_place_name(app->storage, app->selected_place, name, sizeof(name));
    static char text[96];
    snprintf(text, sizeof(text), "Delete place\n\"%s\"?\nCaptures + recon lost.", name);

    dialog_ex_reset(d);
    dialog_ex_set_context(d, app);
    dialog_ex_set_header(d, "Delete place", 64, 4, AlignCenter, AlignTop);
    dialog_ex_set_text(d, text, 64, 24, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(d, "Cancel");
    dialog_ex_set_right_button_text(d, "Delete");
    dialog_ex_set_result_callback(d, confirm_result);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewDialogEx);
}

bool subcensus_scene_confirm_delete_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == DialogExResultRight) {
            bool was_active = strcmp(app->selected_place, app->settings.place_id) == 0;
            census_place_delete(app->storage, app->selected_place);
            FURI_LOG_I("SubCensus", "SC scene=places action=delete id=%s", app->selected_place);
            if(was_active) {
                /* pick a new active place (or recreate default) */
                census_storage_init(app->storage, &app->settings);
            }
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, SubCensusSceneStart);
            return true;
        }
        /* Cancel -> back to place actions */
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void subcensus_scene_confirm_delete_on_exit(void* context) {
    SubCensusApp* app = context;
    dialog_ex_reset(app->dialog_ex);
}
