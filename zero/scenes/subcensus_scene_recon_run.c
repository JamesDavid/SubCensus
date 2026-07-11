#include "../subcensuszero_i.h"

/* Run Recon: prompt Accumulate (default) or Fresh (Zero §3.3 lifecycle), then run. */

enum {
    ReconRunAccumulate = 0,
    ReconRunFresh = 1
};

static void recon_run_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void subcensus_scene_recon_run_on_enter(void* context) {
    SubCensusApp* app = context;
    subcensus_ensure_recon(app);
    Submenu* menu = app->submenu;
    submenu_reset(menu);
    submenu_set_header(menu, "Run Recon");
    submenu_add_item(menu, "Accumulate (default)", ReconRunAccumulate, recon_run_cb, app);
    submenu_add_item(menu, "Fresh (clear first)", ReconRunFresh, recon_run_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_recon_run_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        app->recon_fresh = (event.event == ReconRunFresh);
        scene_manager_next_scene(app->scene_manager, SubCensusSceneRecon);
        return true;
    }
    return false;
}

void subcensus_scene_recon_run_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
