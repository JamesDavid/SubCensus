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

/* Called on OK (toggle list) and Back — the scene uses this to stop the worker on Back. */
typedef void (*CensusCampViewBackCallback)(void* context);
void census_camp_view_set_back_callback(
    CensusCampView* v,
    CensusCampViewBackCallback cb,
    void* ctx);

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
