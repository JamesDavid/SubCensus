/* census_edit.h — edit-before-transmit / field-map discovery support (Zero §6, System §7b).
 *
 * Loads a capture's `.sub`, slices it to an aligned bit frame (sc_slice), gathers a same-device
 * frame corpus for the passive differential analysis (sc_diff -> sc_fieldmap_from_diff), and
 * reads/writes `signatures/field_maps/<protocol>.fmap` entries (sc_fieldmap). Edited transmits
 * are logged DISTINCTLY from captures (an edited TX is not a census observation, §6). All heavy
 * lifting is shared/core; this is the Flipper-side storage glue. Live TX = TODO(hw).
 */
#ifndef CENSUS_EDIT_H
#define CENSUS_EDIT_H

#include <storage/storage.h>

#include "../shared/core/sc_fieldmap.h"

/* Load a capture's `.sub` (place-relative sub_rel) and slice it to an MSB-first bit frame in
 * out_frame[cap_bytes]. Returns the number of bits (0 on failure). Reports the slice unit
 * (shortest dominant symbol), the carrier freq, and the preset name. */
size_t census_edit_load_sub(
    Storage* storage,
    const char* place_id,
    const char* sub_rel,
    uint8_t* out_frame,
    size_t cap_bytes,
    int32_t* out_unit_us,
    uint32_t* out_freq,
    char* out_preset,
    size_t preset_cap);

/* Gather same-frequency captures from census_log.csv into an aligned frame corpus for the
 * differential analysis (§7b). `frames` is [max_frames * stride_bytes]; each row is one sliced
 * frame. Returns the number of frames; sets *out_nbits to the common (min) frame length in bits.
 * Frames are sliced with `unit_us`. */
size_t census_edit_corpus(
    Storage* storage,
    const char* place_id,
    uint32_t freq,
    int32_t unit_us,
    uint8_t* frames,
    size_t max_frames,
    size_t stride_bytes,
    size_t* out_nbits);

/* Field-map IO — signatures/field_maps/<protocol>.fmap (System §6). */
bool census_fieldmap_load(Storage* storage, const char* protocol, ScFieldMap* out);
bool census_fieldmap_save(Storage* storage, const ScFieldMap* map);

/* Append an edited TX to the place's edits_log.csv — logged distinctly from census_log (§6). */
void census_edit_log_tx(
    Storage* storage,
    const char* place_id,
    uint32_t freq,
    const char* preset,
    const uint8_t* frame,
    size_t nbits);

#endif /* CENSUS_EDIT_H */
