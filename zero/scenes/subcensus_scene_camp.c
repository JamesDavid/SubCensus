#include "../subcensuszero_i.h"

/* Camp live view (Zero §3.2, §6). Starts the capture worker on the chosen frequency; a timer
 * refreshes the live view (freq · RSSI · hits) and each capture flashes the LED (§4). Back
 * stops the worker (on_exit). Live RSSI/capture needs a real signal (TODO(hw)). */

#define CAMP_EVENT_CAPTURE 0

static void camp_worker_cb(void* context) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, CAMP_EVENT_CAPTURE);
}

/* OK on a recent-hits row: jump to Review (worker keeps running, §6). */
static void camp_jump_cb(void* context, uint32_t freq_hz) {
    SubCensusApp* app = context;
    app->review_jump_freq = freq_hz;
    scene_manager_next_scene(app->scene_manager, SubCensusSceneReview);
}

/* Long-press OK pauses/resumes monitoring in place (§6). */
static void camp_pause_cb(void* context, bool paused) {
    SubCensusApp* app = context;
    census_worker_set_paused(app->worker, paused);
}

static uint32_t g_camp_last_hits;

static void camp_timer_cb(void* context) {
    SubCensusApp* app = context;
    CensusHit hits[8];
    size_t n = census_worker_recent_hits(app->worker, hits, 8);
    uint32_t h = census_worker_hits(app->worker);
    bool rec = (h != g_camp_last_hits); /* "REC" flash on a fresh capture (§6) */
    g_camp_last_hits = h;
    census_camp_view_set_low(app->camp_view, census_sd_low(app->storage));
    census_camp_view_set_status(app->camp_view, census_worker_elapsed_s(app->worker), rec, 0, 0);
    census_camp_view_update(
        app->camp_view,
        app->live_sweep,
        census_worker_current_freq(app->worker),
        census_worker_rssi(app->worker),
        census_worker_hits(app->worker),
        hits,
        n);
}

void subcensus_scene_camp_on_enter(void* context) {
    SubCensusApp* app = context;
    app->live_sweep = false;
    /* per-band watchlist threshold for this freq if present, else Auto/global (§3.2/A2) */
    float thr = CENSUS_THR_AUTO;
    if(app->settings.use_watchlist)
        census_watchlist_threshold(app->storage, app->settings.place_id, app->camp_freq, &thr);

    census_camp_view_set_jump_callback(app->camp_view, camp_jump_cb, app);
    census_camp_view_set_pause_callback(app->camp_view, camp_pause_cb, app);
    census_worker_configure(app->worker, &app->settings, app->settings.place_id);
    census_worker_set_callback(app->worker, camp_worker_cb, app);
    census_worker_start_camp(app->worker, app->camp_freq, thr);

    app->live_timer = furi_timer_alloc(camp_timer_cb, FuriTimerTypePeriodic, app);
    furi_timer_start(app->live_timer, 100);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewCamp);
}

bool subcensus_scene_camp_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == CAMP_EVENT_CAPTURE) {
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

void subcensus_scene_camp_on_exit(void* context) {
    SubCensusApp* app = context;
    if(app->live_timer) {
        furi_timer_stop(app->live_timer);
        furi_timer_free(app->live_timer);
        app->live_timer = NULL;
    }
    census_worker_stop(app->worker);
}
