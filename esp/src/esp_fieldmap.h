/* esp_fieldmap.h — ESP web-UI adapter for field-map discovery (Esp §5, System §7b).
 *
 * Thin glue over shared/core sc_fieldmap (structure/segmentation/checksum/resign/.fmap IO) +
 * sc_diff (differential) + sc_crc (checksum family). The heavy lifting is shared/core so the
 * Zero and Esp propose IDENTICAL structures; this file only adds the ESP-specific bits the web
 * UI needs and shared/core doesn't carry:
 *   - hex-frame corpus parsing (the browser posts many captures of one device as hex),
 *   - running the passive differential + CORPUS checksum discovery to seed a proposed ScFieldMap
 *     (sc_fieldmap_from_diff seeds byte segments from a diff profile but can't name the checksum
 *     without the corpus — that's done here via sc_checksum_search validated across all frames),
 *   - serializing a ScFieldMap to the web-UI JSON (fields round-trip to ScField).
 *
 * Passive — NEVER transmits. The confirmed structure is persisted as a `.fmap` entry via
 * sc_fieldmap_emit; active own-device confirmation of a re-signed edit (sc_fieldmap_resign) rides
 * the guarded single-frame /api/edit_tx path. Pure C, host-tested (test_esp_fieldmap.c).
 */
#ifndef ESP_FIELDMAP_H
#define ESP_FIELDMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sc_fieldmap.h"

/* Byte-granular segmentation caps at sc_fieldmap's 16 fields (128-bit frames — ample for ISM). */
#define ESP_FIELDMAP_MAX_BYTES 16
#define ESP_FIELDMAP_MAX_FRAMES 64

/* Parse a corpus of hex frames (frames separated by newline/';'; bytes optionally space/comma
 * separated) into `out` (row-major, `*nbytes_out` bytes per frame at ESP_FIELDMAP_MAX_BYTES
 * stride). All frames must be equal length (differential needs aligned frames). Returns the frame
 * count (0 on error/overflow); `out` must hold max_frames*ESP_FIELDMAP_MAX_BYTES bytes. */
size_t esp_fieldmap_parse_hex(
    const char* text, uint8_t* out, size_t max_frames, size_t* nbytes_out);

/* Run the passive differential (sc_diff) + corpus checksum discovery (sc_crc) over `n_frames`
 * frames of `nbytes` each (row-major, ESP_FIELDMAP_MAX_BYTES stride) and seed a PROPOSED ScFieldMap
 * (sc_fieldmap_from_diff) into *out with a named checksum when one is found consistently. Writes a
 * 0..1 proposal confidence to *confidence when non-NULL. Returns true if analyzable
 * (n_frames >= 2, 1 <= nbytes <= ESP_FIELDMAP_MAX_BYTES). Passive — no TX (System §7b). */
bool esp_fieldmap_analyze(
    const uint8_t* frames, size_t n_frames, size_t nbytes, const char* protocol,
    uint8_t modulation, ScFieldMap* out, float* confidence);

/* Serialize a ScFieldMap to the web-UI JSON (System §8 field_maps shape); fields round-trip to
 * ScField (name/start_bit/length/class/semantics). Returns bytes written or -1 on overflow. */
int esp_fieldmap_to_json(const ScFieldMap* map, float confidence, char* out, size_t cap);

#endif /* ESP_FIELDMAP_H */
