#include "../subcensuszero_i.h"

/* Placeholder for milestones not yet built (Recon/Sweep/Camp/Review land in M2-M8).
 * Keeps the menu navigable and honest about what's implemented. */

void subcensus_scene_todo_on_enter(void* context) {
    SubCensusApp* app = context;
    Popup* p = app->popup;
    popup_reset(p);
    popup_set_header(p, "Not yet built", 64, 8, AlignCenter, AlignTop);
    popup_set_text(
        p, app->todo_msg ? app->todo_msg : "Coming soon", 64, 34, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewPopup);
}

bool subcensus_scene_todo_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void subcensus_scene_todo_on_exit(void* context) {
    SubCensusApp* app = context;
    popup_reset(app->popup);
}
