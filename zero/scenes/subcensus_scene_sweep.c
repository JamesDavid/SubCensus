#include "../subcensuszero_i.h"

/* Sweep live view (Zero §3.1). Cycles the watchlist (if present) or the preset list, reusing
 * the capture worker + the shared live view. Live RSSI/capture = TODO(hw). */

#define SWEEP_EVENT_CAPTURE 0

static void sweep_worker_cb(void* context) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, SWEEP_EVENT_CAPTURE);
}

static void sweep_timer_cb(void* context) {
    SubCensusApp* app = context;
    CensusHit hits[8];
    size_t n = census_worker_recent_hits(app->worker, hits, 8);
    census_camp_view_update(
        app->camp_view,
        app->live_sweep,
        census_worker_current_freq(app->worker),
        census_worker_rssi(app->worker),
        census_worker_hits(app->worker),
        hits,
        n);
}

void subcensus_scene_sweep_on_enter(void* context) {
    SubCensusApp* app = context;
    app->live_sweep = true;

    uint32_t freqs[16];
    size_t n = 0;
    if(app->settings.use_watchlist) {
        n = census_watchlist_freqs(app->storage, app->settings.place_id, freqs, 16);
    }
    if(n == 0) { /* no valid recon -> fall back to the preset list, never blocked (System §9) */
        for(size_t i = 0; i < census_freq_us_count && i < 16; i++)
            freqs[i] = census_freq_us[i];
        n = census_freq_us_count;
    }

    census_worker_configure(app->worker, &app->settings, app->settings.place_id);
    census_worker_set_callback(app->worker, sweep_worker_cb, app);
    census_worker_start_sweep(app->worker, freqs, n, app->settings.dwell_ms);

    app->live_timer = furi_timer_alloc(sweep_timer_cb, FuriTimerTypePeriodic, app);
    furi_timer_start(app->live_timer, 100);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewCamp);
}

bool subcensus_scene_sweep_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == SWEEP_EVENT_CAPTURE) {
        if(app->settings.notify != CensusNotifyOff) {
            notification_message(app->notifications, &sequence_blink_green_10);
        }
        if(app->settings.notify == CensusNotifyLedVibro) {
            notification_message(app->notifications, &sequence_single_vibro);
        }
        return true;
    }
    return false;
}

void subcensus_scene_sweep_on_exit(void* context) {
    SubCensusApp* app = context;
    if(app->live_timer) {
        furi_timer_stop(app->live_timer);
        furi_timer_free(app->live_timer);
        app->live_timer = NULL;
    }
    census_worker_stop(app->worker);
}
