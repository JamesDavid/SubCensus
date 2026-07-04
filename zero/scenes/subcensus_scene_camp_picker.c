#include <stdio.h>

#include "../subcensuszero_i.h"

/* Camp frequency picker (Zero §6). M2: the allowed preset list; watchlist/manual/auto pick
 * land with Recon (§3.2 Auto=busiest) in later milestones. */

static void picker_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void subcensus_scene_camp_picker_on_enter(void* context) {
    SubCensusApp* app = context;
    Submenu* menu = app->submenu;
    submenu_reset(menu);
    submenu_set_header(menu, "Camp frequency");
    for(size_t i = 0; i < census_freq_us_count; i++) {
        char mhz[12];
        census_freq_format_mhz(census_freq_us[i], mhz, sizeof(mhz));
        char label[24];
        snprintf(label, sizeof(label), "%s MHz", mhz);
        submenu_add_item(menu, label, i, picker_cb, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_camp_picker_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event < census_freq_us_count) {
        app->camp_freq = census_freq_us[event.event];
        scene_manager_next_scene(app->scene_manager, SubCensusSceneCamp);
        return true;
    }
    return false;
}

void subcensus_scene_camp_picker_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
