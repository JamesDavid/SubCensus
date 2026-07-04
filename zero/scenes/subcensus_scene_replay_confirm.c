#include <furi_hal_region.h>
#include <stdio.h>

#include "../shared/core/sc_freq_bands.h"
#include "../subcensuszero_i.h"

/* TX confirmation (Zero §6): replay / edit-before-transmit is the ONLY TX path — explicit,
 * single-frame, TX-allow-list gated, and confirm-gated with No defaulted. The confirm shows
 * freq + preset + repeat count before sending. Live CC1101 async TX = TODO(hw); the guard +
 * confirm + distinct logging are enforced here. (Full raw-bit / structured-field editing +
 * differential field-map overlay is the remaining optional on-device UI; the passive
 * differential + checksum primitives already ship in shared/core sc_diff / sc_crc.) */

static void replay_result(DialogExResult result, void* context) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void subcensus_scene_replay_confirm_on_enter(void* context) {
    SubCensusApp* app = context;
    DialogEx* d = app->dialog_ex;
    uint32_t freq = app->review_freqs[app->review_sel];
    char mhz[12];
    census_freq_format_mhz(freq, mhz, sizeof(mhz));

    static char text[96];
    bool allowed = sc_freq_in_cc1101_band((int32_t)freq) &&
                   furi_hal_region_is_frequency_allowed(freq);
    if(allowed) {
        snprintf(
            text,
            sizeof(text),
            "Send %s MHz\n%s x1 frame\nto identify your device?",
            mhz,
            app->settings.capture_preset == CensusCaptureFsk ? "2-FSK" : "OOK");
    } else {
        snprintf(text, sizeof(text), "TX not allowed on\n%s MHz (region/band).", mhz);
    }

    dialog_ex_reset(d);
    dialog_ex_set_context(d, app);
    dialog_ex_set_header(d, "Replay?", 64, 4, AlignCenter, AlignTop);
    dialog_ex_set_text(d, text, 64, 24, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(d, "No"); /* No defaulted for destructive/TX (§6) */
    if(allowed) dialog_ex_set_right_button_text(d, "Send");
    dialog_ex_set_result_callback(d, replay_result);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewDialogEx);
}

bool subcensus_scene_replay_confirm_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == DialogExResultRight) {
            uint32_t freq = app->review_freqs[app->review_sel];
            /* TODO(hw): subghz async TX of the .sub RAW frame ONCE (single frame — no
             * auto-increment / sweeping). Edited/replayed TX is logged DISTINCTLY (not into
             * census_log) — an edited TX is not a census observation (§6). */
            notification_message(app->notifications, &sequence_blink_magenta_10);
            FURI_LOG_I(
                "SubCensus",
                "SC scene=replay action=tx freq=%lu frames=1 (TODO hw)",
                (unsigned long)freq);
        }
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void subcensus_scene_replay_confirm_on_exit(void* context) {
    SubCensusApp* app = context;
    dialog_ex_reset(app->dialog_ex);
}
