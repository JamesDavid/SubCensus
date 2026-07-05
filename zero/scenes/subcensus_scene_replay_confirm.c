#include <furi_hal_region.h>
#include <stdio.h>

#include "../shared/core/sc_freq_bands.h"
#include "../shared/core/sc_slice.h"
#include "../subcensuszero_i.h"

/* TX confirmation (Zero §6): replay / edit-before-transmit is the ONLY TX path — explicit,
 * single-frame, TX-allow-list gated, and confirm-gated with No defaulted. The confirm shows
 * freq + preset before sending. Two sources: a stored `.sub` (Replay-to-identify) or an EDITED
 * frame (edit_from_tx). Edited frames pass a decode-back gate (re-encode -> re-slice round-trips
 * cleanly) before Send is offered (§6) and are logged DISTINCTLY from captures. Live CC1101
 * async TX = TODO(hw); the guard + confirm + distinct logging are enforced here. */

/* Decode-back gate (§6): an edited frame must re-encode to timings and re-slice back to the
 * same bits before TX is allowed — proof the edit is structurally sound. */
static bool edit_decodes_back(const SubCensusApp* app) {
    int32_t timings[2048];
    size_t nt =
        sc_slice_encode(app->edit_frame, app->edit_nbits, app->edit_unit_us, timings, 2048);
    uint8_t rt[CENSUS_EDIT_MAXBYTES];
    size_t nbits2 = sc_slice_bits(timings, nt, app->edit_unit_us, rt, CENSUS_EDIT_MAXBYTES);
    if(nbits2 != app->edit_nbits) return false;
    size_t nbytes = (app->edit_nbits + 7) / 8;
    for(size_t i = 0; i < nbytes; i++)
        if(rt[i] != app->edit_frame[i]) return false;
    return true;
}

static void replay_result(DialogExResult result, void* context) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void subcensus_scene_replay_confirm_on_enter(void* context) {
    SubCensusApp* app = context;
    DialogEx* d = app->dialog_ex;
    bool edited = app->edit_from_tx;
    uint32_t freq = edited ? app->edit_freq : app->review_freqs[app->review_sel];
    char mhz[12];
    census_freq_format_mhz(freq, mhz, sizeof(mhz));

    static char text[112];
    bool allowed = sc_freq_in_cc1101_band((int32_t)freq) &&
                   furi_hal_region_is_frequency_allowed(freq);
    bool decodes = !edited || edit_decodes_back(app);
    const char* mod = app->settings.capture_preset == CensusCaptureFsk ? "2-FSK" : "OOK";

    if(!allowed) {
        snprintf(text, sizeof(text), "TX not allowed on\n%s MHz (region/band).", mhz);
    } else if(edited && !decodes) {
        /* decode-back gate failed — block TX (§6) */
        snprintf(
            text, sizeof(text), "Edited frame does not\nre-decode cleanly.\nTX blocked (%s).", mhz);
    } else if(edited) {
        snprintf(
            text,
            sizeof(text),
            "Send EDITED %s MHz\n%s x1 frame to your\nown device? (logged sep.)",
            mhz,
            mod);
    } else {
        snprintf(
            text, sizeof(text), "Send %s MHz\n%s x1 frame\nto identify your device?", mhz, mod);
    }

    dialog_ex_reset(d);
    dialog_ex_set_context(d, app);
    dialog_ex_set_header(d, edited ? "Edit-TX?" : "Replay?", 64, 4, AlignCenter, AlignTop);
    dialog_ex_set_text(d, text, 64, 24, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(d, "No"); /* No defaulted for destructive/TX (§6) */
    if(allowed && decodes) dialog_ex_set_right_button_text(d, "Send");
    dialog_ex_set_result_callback(d, replay_result);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewDialogEx);
}

bool subcensus_scene_replay_confirm_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == DialogExResultRight) {
            bool edited = app->edit_from_tx;
            uint32_t freq = edited ? app->edit_freq : app->review_freqs[app->review_sel];
            /* TODO(hw): subghz async TX of the RAW frame ONCE (single frame — no auto-increment
             * / sweeping, §6 scope guard). For an edited frame, re-encode edit_frame ->
             * timings (sc_slice_encode) and TX those. An edited TX is NOT a census observation,
             * so it is logged DISTINCTLY (edits_log.csv), never into census_log (§6). */
            notification_message(app->notifications, &sequence_blink_magenta_10);
            if(edited) {
                census_edit_log_tx(
                    app->storage,
                    app->settings.place_id,
                    freq,
                    app->edit_preset,
                    app->edit_frame,
                    app->edit_nbits);
            }
            FURI_LOG_I(
                "SubCensus",
                "SC scene=replay action=tx edited=%d freq=%lu frames=1 (TODO hw)",
                edited,
                (unsigned long)freq);
        }
        app->edit_from_tx = false;
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void subcensus_scene_replay_confirm_on_exit(void* context) {
    SubCensusApp* app = context;
    dialog_ex_reset(app->dialog_ex);
}
