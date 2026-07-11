#include "../subcensuszero_i.h"

/* Sweep live view (Zero §3.1). Cycles the watchlist (if present) or the preset list, reusing
 * the capture worker + the shared live view. Live RSSI/capture = TODO(hw). */

/* Kept clear of the DialogEx result range (0..2), which the no-recon hint uses (§3.1). */
#define SWEEP_EVENT_CAPTURE 0x100

static void sweep_worker_cb(void* context) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, SWEEP_EVENT_CAPTURE);
}

/* OK on a recent-hits row: jump to Review (worker keeps running, §6). */
static void sweep_jump_cb(void* context, uint32_t freq_hz) {
    SubCensusApp* app = context;
    app->review_jump_freq = freq_hz;
    scene_manager_next_scene(app->scene_manager, SubCensusSceneReview);
}

/* Long-press OK pauses/resumes monitoring in place (§6). */
static void sweep_pause_cb(void* context, bool paused) {
    SubCensusApp* app = context;
    census_worker_set_paused(app->worker, paused);
}

static uint32_t g_sweep_last_hits;

static void sweep_timer_cb(void* context) {
    SubCensusApp* app = context;
    CensusHit hits[8];
    size_t n = census_worker_recent_hits(app->worker, hits, 8);
    uint32_t h = census_worker_hits(app->worker);
    bool rec = (h != g_sweep_last_hits); /* "REC" flash on a fresh capture (§6) */
    g_sweep_last_hits = h;
    census_camp_view_set_low(app->camp_view, census_sd_low(app->storage));
    census_camp_view_set_status(
        app->camp_view,
        census_worker_elapsed_s(app->worker),
        rec,
        census_worker_sweep_pos(app->worker),
        census_worker_sweep_count(app->worker));
    census_camp_view_update(
        app->camp_view,
        app->live_sweep,
        census_worker_current_freq(app->worker),
        census_worker_rssi(app->worker),
        census_worker_hits(app->worker),
        hits,
        n);
}

/* Start the sweep worker on the active watchlist (if present) or the preset/custom fallback. */
static void sweep_begin(SubCensusApp* app) {
    uint32_t freqs[16];
    float thr[16];
    size_t n = 0;
    if(app->settings.use_watchlist) {
        n = census_watchlist_load(app->storage, app->settings.place_id, freqs, thr, 16);
    }
    if(n == 0) { /* no valid recon -> fall back to the preset/custom list, never blocked (§9) */
        if(app->settings.freq_preset == CensusFreqPresetCustom && app->settings.custom_count > 0) {
            for(uint8_t i = 0; i < app->settings.custom_count && i < 16; i++)
                freqs[i] = app->settings.custom_freqs[i];
            n = app->settings.custom_count < 16 ? app->settings.custom_count : 16;
        } else {
            const uint32_t* list =
                app->settings.freq_preset == CensusFreqPresetEU ? census_freq_eu : census_freq_us;
            size_t cnt = app->settings.freq_preset == CensusFreqPresetEU ? census_freq_eu_count :
                                                                           census_freq_us_count;
            for(size_t i = 0; i < cnt && i < 16; i++)
                freqs[i] = list[i];
            n = cnt;
        }
        for(size_t i = 0; i < n; i++)
            thr[i] = CENSUS_THR_AUTO; /* no per-band threshold -> Auto/global (§3.1) */
    }

    census_camp_view_set_jump_callback(app->camp_view, sweep_jump_cb, app);
    census_camp_view_set_pause_callback(app->camp_view, sweep_pause_cb, app);
    census_worker_configure(app->worker, &app->settings, app->settings.place_id);
    census_worker_set_callback(app->worker, sweep_worker_cb, app);
    census_worker_start_sweep(app->worker, freqs, thr, n, app->settings.dwell_ms);

    app->live_timer = furi_timer_alloc(sweep_timer_cb, FuriTimerTypePeriodic, app);
    furi_timer_start(app->live_timer, 100);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewCamp);
}

static void sweep_hint_result(DialogExResult result, void* context) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void subcensus_scene_sweep_on_enter(void* context) {
    SubCensusApp* app = context;
    subcensus_ensure_recon(app);
    subcensus_ensure_worker(app);
    app->live_sweep = true;

    /* No valid recon: never blocked, but show a dismissable hint (Zero §3.1 / System §9). */
    uint32_t probe[16];
    size_t nwl = app->settings.use_watchlist ?
                     census_watchlist_freqs(app->storage, app->settings.place_id, probe, 16) :
                     1; /* watchlist off -> no hint */
    if(app->settings.use_watchlist && nwl == 0) {
        DialogEx* d = app->dialog_ex;
        dialog_ex_reset(d);
        dialog_ex_set_context(d, app);
        dialog_ex_set_header(d, "No recon here", 64, 4, AlignCenter, AlignTop);
        dialog_ex_set_text(
            d,
            "Sweeping the default\nband list. Run Recon\nto focus revisit.",
            64,
            24,
            AlignCenter,
            AlignCenter);
        dialog_ex_set_left_button_text(d, "Run Recon");
        dialog_ex_set_right_button_text(d, "Proceed");
        dialog_ex_set_result_callback(d, sweep_hint_result);
        view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewDialogEx);
        return;
    }
    sweep_begin(app);
}

bool subcensus_scene_sweep_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == DialogExResultRight) { /* Proceed on the preset/custom fallback */
        sweep_begin(app);
        return true;
    }
    if(event.event == DialogExResultLeft) { /* Run Recon now */
        app->recon_fresh = false;
        scene_manager_next_scene(app->scene_manager, SubCensusSceneReconRun);
        return true;
    }
    if(event.event == SWEEP_EVENT_CAPTURE) {
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
