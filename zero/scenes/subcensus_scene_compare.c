#include <stdio.h>
#include <string.h>

#include "../subcensuszero_i.h"

/* Compare places (Zero §5.6 stretch): pick another place to diff its watchlist against the
 * active place's — "what's here that isn't at home?". Selecting a place opens the diff result. */

static char g_ids[CENSUS_MAX_PLACES][CENSUS_PLACE_ID_LEN];
static size_t g_count;

static void compare_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void subcensus_scene_compare_on_enter(void* context) {
    SubCensusApp* app = context;
    Submenu* menu = app->submenu;
    submenu_reset(menu);
    submenu_set_header(menu, "Compare to place");

    char all[CENSUS_MAX_PLACES][CENSUS_PLACE_ID_LEN];
    size_t n = census_place_list(app->storage, all, CENSUS_MAX_PLACES);
    g_count = 0;
    for(size_t i = 0; i < n; i++) {
        if(strcmp(all[i], app->settings.place_id) == 0) continue; /* skip the active place */
        char name[CENSUS_PLACE_NAME_LEN];
        census_place_name(app->storage, all[i], name, sizeof(name));
        strncpy(g_ids[g_count], all[i], CENSUS_PLACE_ID_LEN - 1);
        g_ids[g_count][CENSUS_PLACE_ID_LEN - 1] = '\0';
        submenu_add_item(menu, name, g_count, compare_cb, app);
        g_count++;
    }
    if(g_count == 0) submenu_add_item(menu, "No other places", 999, compare_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_compare_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event < g_count) {
        strncpy(app->selected_place, g_ids[event.event], CENSUS_PLACE_ID_LEN - 1);
        app->selected_place[CENSUS_PLACE_ID_LEN - 1] = '\0';
        scene_manager_next_scene(app->scene_manager, SubCensusSceneCompareResult);
        return true;
    }
    return false;
}

void subcensus_scene_compare_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
