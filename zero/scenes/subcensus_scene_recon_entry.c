#include <stdio.h>

#include "../subcensuszero_i.h"

/* Recon results per-entry actions (Zero §6): Pin / Exclude (persisted in watchlist.csv as
 * source=user-pin / user-exclude, System §9) / Camp here (jump straight to Camp on this bin). */

enum {
    ReconEntryPin = 0,
    ReconEntryExclude = 1,
    ReconEntryCampHere = 2,
};

static void entry_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void subcensus_scene_recon_entry_on_enter(void* context) {
    SubCensusApp* app = context;
    Submenu* menu = app->submenu;
    submenu_reset(menu);
    char mhz[12];
    census_freq_format_mhz(app->recon_sel_freq, mhz, sizeof(mhz));
    char header[24];
    snprintf(header, sizeof(header), "%s MHz", mhz);
    submenu_set_header(menu, header);
    submenu_add_item(menu, "Pin", ReconEntryPin, entry_cb, app);
    submenu_add_item(menu, "Exclude", ReconEntryExclude, entry_cb, app);
    submenu_add_item(menu, "Camp here", ReconEntryCampHere, entry_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_recon_entry_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case ReconEntryPin:
        census_watchlist_set_source(
            app->storage, app->settings.place_id, app->recon_sel_freq, "user-pin");
        notification_message(app->notifications, &sequence_success);
        scene_manager_previous_scene(app->scene_manager);
        return true;
    case ReconEntryExclude:
        census_watchlist_set_source(
            app->storage, app->settings.place_id, app->recon_sel_freq, "user-exclude");
        notification_message(app->notifications, &sequence_success);
        scene_manager_previous_scene(app->scene_manager);
        return true;
    case ReconEntryCampHere:
        app->camp_freq = app->recon_sel_freq;
        scene_manager_next_scene(app->scene_manager, SubCensusSceneCamp);
        return true;
    default:
        return false;
    }
}

void subcensus_scene_recon_entry_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
