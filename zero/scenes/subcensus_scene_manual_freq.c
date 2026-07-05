#include <stdio.h>
#include <string.h>

#include "../subcensuszero_i.h"

/* Manual frequency entry (Zero §6): a number-entry scene (text input constrained to a MHz
 * value like "433.92"), validated against the firmware RX allow-list (reject out-of-band).
 * Routes by text_mode — either add to the Custom list, or camp on it. */

static void manual_done(void* context) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, 0);
}

/* Parse a MHz string ("433.92", "315") into Hz. Returns 0 on empty/garbage. */
static uint32_t parse_mhz(const char* s) {
    uint32_t mhz = 0;
    size_t i = 0;
    bool any = false;
    for(; s[i] && s[i] != '.'; i++) {
        if(s[i] < '0' || s[i] > '9') return 0;
        mhz = mhz * 10 + (uint32_t)(s[i] - '0');
        any = true;
    }
    uint32_t frac = 0, scale = 100000; /* 6 fractional digits -> Hz */
    if(s[i] == '.') {
        i++;
        for(int d = 0; s[i] && d < 6; i++, d++) {
            if(s[i] < '0' || s[i] > '9') return 0;
            frac += (uint32_t)(s[i] - '0') * scale;
            scale /= 10;
            any = true;
        }
    }
    if(!any) return 0;
    return mhz * 1000000u + frac;
}

void subcensus_scene_manual_freq_on_enter(void* context) {
    SubCensusApp* app = context;
    TextInput* ti = app->text_input;
    text_input_reset(ti);
    text_input_set_header_text(ti, "Freq MHz (e.g. 433.92)");
    app->text_buf[0] = '\0';
    text_input_set_result_callback(ti, manual_done, app, app->text_buf, 12, true);
    text_input_set_minimum_length(ti, 1);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewTextInput);
}

bool subcensus_scene_manual_freq_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    uint32_t hz = parse_mhz(app->text_buf);
    if(hz == 0 || !census_freq_is_allowed(hz)) {
        notification_message(app->notifications, &sequence_blink_red_100);
        FURI_LOG_I("SubCensus", "SC scene=manual_freq action=reject hz=%lu", (unsigned long)hz);
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }

    if(app->text_mode == SubCensusTextModeManualFreqCamp) {
        app->camp_freq = hz;
        FURI_LOG_I("SubCensus", "SC scene=manual_freq action=camp hz=%lu", (unsigned long)hz);
        scene_manager_next_scene(app->scene_manager, SubCensusSceneCamp);
    } else {
        /* add to the custom list (dedup, capacity-guarded) */
        bool dup = false;
        for(uint8_t i = 0; i < app->settings.custom_count; i++)
            if(app->settings.custom_freqs[i] == hz) dup = true;
        if(!dup && app->settings.custom_count < CENSUS_CUSTOM_MAX) {
            app->settings.custom_freqs[app->settings.custom_count++] = hz;
            census_settings_save(app->storage, &app->settings);
        }
        FURI_LOG_I(
            "SubCensus", "SC scene=manual_freq action=custom_add hz=%lu", (unsigned long)hz);
        scene_manager_previous_scene(app->scene_manager);
    }
    return true;
}

void subcensus_scene_manual_freq_on_exit(void* context) {
    SubCensusApp* app = context;
    text_input_reset(app->text_input);
}
