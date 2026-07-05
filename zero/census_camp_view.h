/* census_camp_view.h — live Camp/Sweep view (Zero §6): freq · RSSI bar · hits · last match,
 * with OK toggling a recent-hits list. Back stops (handled by the scene). */
#ifndef CENSUS_CAMP_VIEW_H
#define CENSUS_CAMP_VIEW_H

#include <gui/view.h>

#include "census_worker.h"

typedef struct CensusCampView CensusCampView;

CensusCampView* census_camp_view_alloc(void);
void census_camp_view_free(CensusCampView* v);
View* census_camp_view_get_view(CensusCampView* v);

/* Called on Back from the live view — the scene uses this to stop the worker. */
typedef void (*CensusCampViewBackCallback)(void* context);
void census_camp_view_set_back_callback(
    CensusCampView* v,
    CensusCampViewBackCallback cb,
    void* ctx);

/* Called when OK is pressed on a recent-hits row: jump to that capture in Review (worker keeps
 * running, §6). `freq_hz` is the selected hit's frequency. */
typedef void (*CensusCampViewJumpCallback)(void* context, uint32_t freq_hz);
void census_camp_view_set_jump_callback(
    CensusCampView* v,
    CensusCampViewJumpCallback cb,
    void* ctx);

/* Show/hide an "SD LOW" banner (§6.1 SD full: captures stop, blip rows continue). */
void census_camp_view_set_low(CensusCampView* v, bool low);

/* Live status (§6): elapsed seconds, a brief "REC" overlay after a capture, and the sweep
 * position (pos/count; count 0 = camp mode, no position shown). */
void census_camp_view_set_status(
    CensusCampView* v,
    uint32_t elapsed_s,
    bool rec,
    uint8_t pos,
    uint8_t count);

/* Push the latest worker state into the view model (called from a refresh timer). */
void census_camp_view_update(
    CensusCampView* v,
    bool sweep,
    uint32_t freq_hz,
    float rssi,
    uint32_t hits,
    const CensusHit* recent,
    size_t recent_len);

#endif /* CENSUS_CAMP_VIEW_H */
