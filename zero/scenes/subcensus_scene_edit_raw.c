#include "../subcensuszero_i.h"

/* Raw bit/hex editor (Zero §6): the loaded frame as editable hex. Single-frame only (no
 * auto-increment / value sweeping, §6 scope guard). Edits set edit_dirty; Send is on the Edit
 * menu (TX-allow-list gated, logged distinctly). */

void subcensus_scene_edit_raw_on_enter(void* context) {
    SubCensusApp* app = context;
    census_editor_view_configure(
        app->editor_view,
        CensusEditorRaw,
        app->edit_frame,
        app->edit_nbits,
        NULL,
        &app->edit_dirty);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewEditor);
}

bool subcensus_scene_edit_raw_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void subcensus_scene_edit_raw_on_exit(void* context) {
    UNUSED(context);
}
