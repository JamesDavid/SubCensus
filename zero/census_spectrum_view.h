/* census_spectrum_view.h — Recon live "surfing" view (Zero §6): a split screen with a
 * spectrum/activity strip (RSSI bars across the segment being swept, peak-hold-with-decay, a
 * cursor at the current sample, a segment label) that AUTO-FOLLOWS the sweep across the CC1101
 * segments; OK toggles the strip vs a live top-hits mini-list; Left/Right page segments
 * manually. A status line (segment · freq · hot-bin count · elapsed · pass) is always visible.
 */
#ifndef CENSUS_SPECTRUM_VIEW_H
#define CENSUS_SPECTRUM_VIEW_H

#include <gui/view.h>

#define CENSUS_SPEC_BARS 60
#define CENSUS_SPEC_HITS 6

typedef struct CensusSpectrumView CensusSpectrumView;

CensusSpectrumView* census_spectrum_view_alloc(void);
void census_spectrum_view_free(CensusSpectrumView* v);
View* census_spectrum_view_get_view(CensusSpectrumView* v);

typedef void (*CensusSpectrumBackCallback)(void* context);
void census_spectrum_view_set_back_callback(
    CensusSpectrumView* v,
    CensusSpectrumBackCallback cb,
    void* ctx);

/* Which segment the view wants drawn: -1 = follow the sweep, 0..2 = manually paged. The scene
 * reads this to build bars for the right segment. */
int census_spectrum_view_paged_segment(CensusSpectrumView* v);

/* Push the latest sweep state (called from the scene's refresh timer). `bars` holds `nbars`
 * dBm values (CENSUS_RSSI_NONE for empty buckets) across [lo,hi]. */
void census_spectrum_view_update(
    CensusSpectrumView* v,
    uint8_t seg,
    uint32_t lo,
    uint32_t hi,
    uint32_t cursor_freq,
    const float* bars,
    size_t nbars,
    const uint32_t* hit_freqs,
    const float* hit_peaks,
    size_t hit_n,
    uint32_t hot_bins,
    uint32_t pass,
    uint32_t elapsed_s,
    bool paged);

#endif /* CENSUS_SPECTRUM_VIEW_H */
