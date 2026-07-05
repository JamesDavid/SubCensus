#include <stdio.h>

#include "../subcensuszero_i.h"

/* Camp frequency picker (Zero §6/§3.2): choose from — Watchlist (recon hot bins) · Allowed
 * presets · Manual entry (number-entry, RX-allow-list validated) · Auto (busiest watchlist;
 * greyed with no watchlist). Selecting a frequency starts Camp; Manual routes to the entry
 * scene; Auto resolves the busiest watchlist entry. */

#define PICK_MAX    40
#define PICK_MANUAL 0xFFFFFFFEu
#define PICK_AUTO   0xFFFFFFFDu

static uint32_t g_pick[PICK_MAX];
static size_t g_pick_n;

static void picker_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void add(Submenu* menu, SubCensusApp* app, const char* label, uint32_t value) {
    if(g_pick_n >= PICK_MAX) return;
    submenu_add_item(menu, label, g_pick_n, picker_cb, app);
    g_pick[g_pick_n++] = value;
}

void subcensus_scene_camp_picker_on_enter(void* context) {
    SubCensusApp* app = context;
    Submenu* menu = app->submenu;
    submenu_reset(menu);
    submenu_set_header(menu, "Camp frequency");
    g_pick_n = 0;

    /* Watchlist hot bins (Auto = busiest first, greyed-out label when absent, §3.2) */
    uint32_t wl[24];
    size_t nwl = census_watchlist_freqs(app->storage, app->settings.place_id, wl, 24);
    if(nwl > 0) {
        add(menu, app, "Auto (busiest watchlist)", PICK_AUTO);
        for(size_t i = 0; i < nwl; i++) {
            char mhz[12], label[28];
            census_freq_format_mhz(wl[i], mhz, sizeof(mhz));
            snprintf(label, sizeof(label), "WL %s MHz", mhz);
            add(menu, app, label, wl[i]);
        }
    } else {
        add(menu, app, "Auto (no watchlist)", PICK_AUTO); /* selectable but no-ops to a hint */
    }

    /* Allowed presets */
    for(size_t i = 0; i < census_freq_us_count; i++) {
        char mhz[12], label[24];
        census_freq_format_mhz(census_freq_us[i], mhz, sizeof(mhz));
        snprintf(label, sizeof(label), "%s MHz", mhz);
        add(menu, app, label, census_freq_us[i]);
    }

    /* Manual number entry */
    add(menu, app, "Manual entry...", PICK_MANUAL);

    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_camp_picker_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event >= g_pick_n) return false;
    uint32_t v = g_pick[event.event];

    if(v == PICK_MANUAL) {
        app->text_mode = SubCensusTextModeManualFreqCamp;
        scene_manager_next_scene(app->scene_manager, SubCensusSceneManualFreq);
        return true;
    }
    if(v == PICK_AUTO) {
        uint32_t busiest = census_watchlist_busiest(app->storage, app->settings.place_id);
        if(busiest == 0) {
            /* no watchlist — Auto is unavailable; notify and stay (§3.2) */
            notification_message(app->notifications, &sequence_blink_red_100);
            return true;
        }
        app->camp_freq = busiest;
    } else {
        app->camp_freq = v;
    }
    scene_manager_next_scene(app->scene_manager, SubCensusSceneCamp);
    return true;
}

void subcensus_scene_camp_picker_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
