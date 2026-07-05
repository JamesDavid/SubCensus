#include "../subcensuszero_i.h"

/* Structured field editor (Zero §6): edit the frame as labeled fields from the protocol's
 * field-map; each edit recomputes the checksum (sc_fieldmap_resign) so the frame stays
 * self-consistent. Requires a known protocol with a field-map — otherwise route to Discovery. */

void subcensus_scene_edit_fields_on_enter(void* context) {
    SubCensusApp* app = context;
    if(!app->edit_has_map || app->edit_map.n_fields == 0) {
        /* no field-map for this protocol — the discovery path builds one first */
        notification_message(app->notifications, &sequence_blink_yellow_100);
        FURI_LOG_I("SubCensus", "SC scene=edit_fields action=no_map proto=%s", app->edit_protocol);
        scene_manager_previous_scene(app->scene_manager);
        return;
    }
    census_editor_view_configure(
        app->editor_view,
        CensusEditorFields,
        app->edit_frame,
        app->edit_nbits,
        &app->edit_map,
        &app->edit_dirty);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewEditor);
}

bool subcensus_scene_edit_fields_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void subcensus_scene_edit_fields_on_exit(void* context) {
    UNUSED(context);
}
