#include <stdio.h>

#include "../subcensuszero_i.h"

/* Custom frequency-list editor (Zero §6): reached from Settings (Freq preset -> Custom).
 * Lists the per-app custom frequencies; OK on an entry removes it; "+ Add frequency" opens the
 * manual number-entry scene (validated against the RX allow-list). Custom lists are the Sweep
 * fallback when no watchlist (§3.1). */

#define FREQ_EDIT_ADD 0xFFFFu

static void freq_edit_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void subcensus_scene_freq_edit_on_enter(void* context) {
    SubCensusApp* app = context;
    Submenu* menu = app->submenu;
    submenu_reset(menu);
    submenu_set_header(menu, "Custom list (OK=remove)");

    for(uint8_t i = 0; i < app->settings.custom_count; i++) {
        char mhz[12], label[28];
        census_freq_format_mhz(app->settings.custom_freqs[i], mhz, sizeof(mhz));
        snprintf(label, sizeof(label), "%s MHz", mhz);
        submenu_add_item(menu, label, i, freq_edit_cb, app);
    }
    if(app->settings.custom_count == 0)
        submenu_add_item(menu, "(empty - add below)", 0xFFFEu, freq_edit_cb, app);
    submenu_add_item(menu, "+ Add frequency", FREQ_EDIT_ADD, freq_edit_cb, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_freq_edit_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == FREQ_EDIT_ADD) {
        app->text_mode = SubCensusTextModeManualFreqCustom;
        scene_manager_next_scene(app->scene_manager, SubCensusSceneManualFreq);
        return true;
    }
    if(event.event < app->settings.custom_count) {
        /* remove the selected frequency (shift down), persist, refresh */
        uint8_t idx = (uint8_t)event.event;
        for(uint8_t i = idx; i + 1 < app->settings.custom_count; i++)
            app->settings.custom_freqs[i] = app->settings.custom_freqs[i + 1];
        app->settings.custom_count--;
        census_settings_save(app->storage, &app->settings);
        notification_message(app->notifications, &sequence_blink_blue_100);
        /* rebuild the list in place */
        subcensus_scene_freq_edit_on_enter(app);
        return true;
    }
    return false;
}

void subcensus_scene_freq_edit_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
