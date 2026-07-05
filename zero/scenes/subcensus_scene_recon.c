#include <stdio.h>

#include "../subcensuszero_i.h"

/* Recon live view (Zero §6): the "surfing" split-screen. A refresh timer reads the survey's
 * per-bin peaks and pushes them to the spectrum strip (census_spectrum_view), which auto-follows
 * the sweep across the CC1101 segments; OK toggles the top-hits mini-list; Left/Right page
 * segments. On completion it advances to the Recon results scene. Live RSSI = TODO(hw). */

#define RECON_EVENT_DONE 0

static void recon_progress_cb(void* context) {
    SubCensusApp* app = context;
    if(!census_recon_is_running(app->recon)) {
        view_dispatcher_send_custom_event(app->view_dispatcher, RECON_EVENT_DONE);
    }
}

static void recon_back_cb(void* context) {
    SubCensusApp* app = context;
    census_recon_stop(app->recon);
}

static void recon_timer_cb(void* context) {
    SubCensusApp* app = context;
    CensusRecon* r = app->recon;

    int want = census_spectrum_view_paged_segment(app->spectrum_view);
    uint8_t follow = census_recon_current_segment(r);
    uint8_t seg = want >= 0 ? (uint8_t)want : follow;

    uint32_t lo = 0, hi = 0;
    census_recon_segment_bounds(seg, &lo, &hi);
    float bars[CENSUS_SPEC_BARS];
    census_recon_segment_bars(r, seg, bars, CENSUS_SPEC_BARS);

    uint32_t hit_freqs[CENSUS_SPEC_HITS];
    float hit_peaks[CENSUS_SPEC_HITS];
    size_t hit_n = census_recon_top_hits(r, hit_freqs, hit_peaks, CENSUS_SPEC_HITS);

    census_spectrum_view_update(
        app->spectrum_view,
        seg,
        lo,
        hi,
        census_recon_current_freq(r),
        bars,
        CENSUS_SPEC_BARS,
        hit_freqs,
        hit_peaks,
        hit_n,
        census_recon_hot_bins(r),
        census_recon_pass(r),
        census_recon_elapsed_s(r),
        want >= 0);
}

void subcensus_scene_recon_on_enter(void* context) {
    SubCensusApp* app = context;
    census_spectrum_view_set_back_callback(app->spectrum_view, recon_back_cb, app);

    census_recon_set_progress(app->recon, recon_progress_cb, app);
    census_recon_start(app->recon, &app->settings, app->settings.place_id, app->recon_fresh);

    app->live_timer = furi_timer_alloc(recon_timer_cb, FuriTimerTypePeriodic, app);
    furi_timer_start(app->live_timer, 250);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSpectrum);
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
}
