#include <stdio.h>

#include "../subcensuszero_i.h"

/* SD card required (Zero §6.1): the app stores captures/config/places on /ext. With no card,
 * this blocking screen disables monitoring; About remains reachable. A poll timer recovers
 * automatically when a card is inserted (re-inits storage and returns to the main menu). */

#define SD_EVENT_INSERTED 0
#define SD_ITEM_ABOUT     1

static void sd_menu_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void sd_poll_cb(void* context) {
    SubCensusApp* app = context;
    if(census_sd_present(app->storage)) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SD_EVENT_INSERTED);
    }
}

void subcensus_scene_sd_required_on_enter(void* context) {
    SubCensusApp* app = context;
    Submenu* menu = app->submenu;
    submenu_reset(menu);
    submenu_set_header(menu, "SD card required");
    submenu_add_item(menu, "About", SD_ITEM_ABOUT, sd_menu_cb, app);

    app->live_timer = furi_timer_alloc(sd_poll_cb, FuriTimerTypePeriodic, app);
    furi_timer_start(app->live_timer, 500);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_sd_required_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == SD_EVENT_INSERTED) {
        /* card back — re-init storage and continue to the main menu (§6.1 auto-recover) */
        census_storage_init(app->storage, &app->settings);
        FURI_LOG_I("SubCensus", "SC scene=sd_required action=recovered");
        scene_manager_next_scene(app->scene_manager, SubCensusSceneStart);
        return true;
    }
    if(event.event == SD_ITEM_ABOUT) {
        scene_manager_next_scene(app->scene_manager, SubCensusSceneAbout);
        return true;
    }
    return false;
}

void subcensus_scene_sd_required_on_exit(void* context) {
    SubCensusApp* app = context;
    if(app->live_timer) {
        furi_timer_stop(app->live_timer);
        furi_timer_free(app->live_timer);
        app->live_timer = NULL;
    }
    submenu_reset(app->submenu);
}
