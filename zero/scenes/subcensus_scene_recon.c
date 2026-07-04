#include <stdio.h>

#include "../subcensuszero_i.h"

/* Recon live status (Zero §3.3/§6). Runs the occupancy survey; a timer shows current freq,
 * hot-bin count, pass, and elapsed. On completion it emits occupancy.csv + watchlist.csv and
 * advances to the results. (The full auto-following spectrum strip is a later UI polish; this
 * is the glance-status view.) Live RSSI = TODO(hw). */

#define RECON_EVENT_DONE 0

static char g_recon_status[128];

static void recon_progress_cb(void* context) {
    SubCensusApp* app = context;
    if(!census_recon_is_running(app->recon)) {
        view_dispatcher_send_custom_event(app->view_dispatcher, RECON_EVENT_DONE);
    }
}

static void recon_timer_cb(void* context) {
    SubCensusApp* app = context;
    char mhz[12];
    census_freq_format_mhz(census_recon_current_freq(app->recon), mhz, sizeof(mhz));
    snprintf(
        g_recon_status,
        sizeof(g_recon_status),
        "%s MHz\nHot bins: %lu\nPass %lu  %lus\nBack: stop",
        mhz,
        (unsigned long)census_recon_hot_bins(app->recon),
        (unsigned long)census_recon_pass(app->recon),
        (unsigned long)census_recon_elapsed_s(app->recon));
    popup_set_text(app->popup, g_recon_status, 64, 30, AlignCenter, AlignCenter);
}

void subcensus_scene_recon_on_enter(void* context) {
    SubCensusApp* app = context;
    popup_reset(app->popup);
    popup_set_header(app->popup, "Recon", 64, 8, AlignCenter, AlignTop);
    popup_set_text(app->popup, "starting...", 64, 30, AlignCenter, AlignCenter);

    census_recon_set_progress(app->recon, recon_progress_cb, app);
    census_recon_start(app->recon, &app->settings, app->settings.place_id, app->recon_fresh);

    app->live_timer = furi_timer_alloc(recon_timer_cb, FuriTimerTypePeriodic, app);
    furi_timer_start(app->live_timer, 250);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewPopup);
}

bool subcensus_scene_recon_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == RECON_EVENT_DONE) {
        scene_manager_next_scene(app->scene_manager, SubCensusSceneReconResults);
        return true;
    }
    return false;
}

void subcensus_scene_recon_on_exit(void* context) {
    SubCensusApp* app = context;
    if(app->live_timer) {
        furi_timer_stop(app->live_timer);
        furi_timer_free(app->live_timer);
        app->live_timer = NULL;
    }
    census_recon_stop(app->recon);
    popup_reset(app->popup);
}
