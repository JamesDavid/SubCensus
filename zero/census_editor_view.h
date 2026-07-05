/* census_editor_view.h — the on-device edit-before-transmit / field-map editor view (Zero §6,
 * System §7b). One custom View, three modes:
 *   RAW       — byte/hex edit of any capture (Up/Down pick byte, Left/Right +/- its value).
 *   FIELDS    — structured field editor for a known protocol: labeled fields from a field-map;
 *               Left/Right adjust the selected field's value; the checksum is recomputed
 *               (sc_fieldmap_resign) after every edit so the frame stays self-consistent.
 *   DISCOVERY — differential-seeded segment labeling on an unknown: Left/Right cycle each
 *               segment's class (static/slow/counter/checksum/data) to label the structure.
 * It edits the target frame (and, for FIELDS/DISCOVERY, the ScFieldMap) in place and sets *dirty.
 * Single-frame only — no auto-increment / value sweeping (§6 scope guard).
 */
#ifndef CENSUS_EDITOR_VIEW_H
#define CENSUS_EDITOR_VIEW_H

#include <gui/view.h>

#include "../shared/core/sc_fieldmap.h"

typedef enum {
    CensusEditorRaw = 0,
    CensusEditorFields = 1,
    CensusEditorDiscovery = 2,
} CensusEditorMode;

typedef struct CensusEditorView CensusEditorView;

CensusEditorView* census_editor_view_alloc(void);
void census_editor_view_free(CensusEditorView* v);
View* census_editor_view_get_view(CensusEditorView* v);

/* Point the view at a target frame (+ optional field-map for FIELDS/DISCOVERY). `nbits` is the
 * frame length; `dirty` is set true on any edit. `unit_us` is informational. */
void census_editor_view_configure(
    CensusEditorView* v,
    CensusEditorMode mode,
    uint8_t* frame,
    size_t nbits,
    ScFieldMap* map,
    bool* dirty);

#endif /* CENSUS_EDITOR_VIEW_H */
