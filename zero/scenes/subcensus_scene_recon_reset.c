#include <stdio.h>

#include "../subcensuszero_i.h"

/* Reset recon (Zero §6 / System §9): confirm-gated wipe of this place's occupancy + watchlist,
 * prompting keep-or-wipe user pins. Left = Keep pins, Right = Wipe pins, Back = Cancel. Touches
 * recon artifacts only — captures / census_log / global signatures are untouched. */

static void reset_result(DialogExResult result, void* context) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void subcensus_scene_recon_reset_on_enter(void* context) {
    SubCensusApp* app = context;
    DialogEx* d = app->dialog_ex;
    dialog_ex_reset(d);
    dialog_ex_set_context(d, app);
    dialog_ex_set_header(d, "Reset recon?", 64, 4, AlignCenter, AlignTop);
    dialog_ex_set_text(
        d,
        "Clears occupancy +\nwatchlist for this place.\nKeep your pins?",
        64,
        24,
        AlignCenter,
        AlignCenter);
    dialog_ex_set_left_button_text(d, "Keep pins");
    dialog_ex_set_right_button_text(d, "Wipe pins");
    dialog_ex_set_result_callback(d, reset_result);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewDialogEx);
}

bool subcensus_scene_recon_reset_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == DialogExResultLeft || event.event == DialogExResultRight) {
        bool keep_pins = (event.event == DialogExResultLeft);
        census_recon_reset(app->storage, app->settings.place_id, keep_pins);
        FURI_LOG_I(
            "SubCensus",
            "SC scene=recon action=reset keep_pins=%d place=%s",
            keep_pins,
            app->settings.place_id);
        notification_message(app->notifications, &sequence_success);
    }
    /* return to results either way (Center/Back = cancel) */
    scene_manager_search_and_switch_to_previous_scene(
        app->scene_manager, SubCensusSceneReconResults);
    return true;
}

void subcensus_scene_recon_reset_on_exit(void* context) {
    SubCensusApp* app = context;
    dialog_ex_reset(app->dialog_ex);
}
