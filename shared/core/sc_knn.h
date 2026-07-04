/* sc_knn.h — gated k-NN classification (System §6).
 *
 * Pipeline tier 2 (raw/undecodable captures): require freq_bin AND modulation to match
 * (hard gate), then rank surviving fingerprints by a normalized weighted Euclidean
 * distance on the timing features. Cadence (System §7a) is a SOFT booster/penalty, never
 * a gate (a device seen once has no cadence yet). Returns top-N candidates with a
 * distance-derived confidence. Never auto-relabels — advisory only (System §6).
 *
 * Normalization scales/weights are fixed constants so the Zero (RAW-derived) and the Pi
 * (IQ-derived) score identically in one k-NN space (System §7, binding).
 */
#ifndef SC_KNN_H
#define SC_KNN_H

#include <stddef.h>

#include "sc_feature.h"
#include "sc_types.h"

typedef struct {
    ScFeatureVector fv;
    ScCadenceClass cadence_class; /* SC_CADENCE_NONE if unknown (soft feature) */
    double period_s;              /* 0 if unknown */
} ScKnnQuery;

typedef struct {
    ScFeatureVector fv;
    ScCadenceClass cadence_class;
    double period_s;
    const char* device_name; /* borrowed pointer */
    int device_class;        /* CensusDeviceClass index, or -1 */
} ScFingerprint;

typedef struct {
    int index;         /* index into the candidates array */
    double distance;   /* gated weighted-Euclidean distance (lower = closer) */
    double confidence; /* 0..1, distance-derived then cadence-adjusted */
} ScKnnMatch;

/* Match `q` against `n` candidates. Writes up to `topn` best matches (confidence desc)
 * into `out`; returns the number written. Candidates failing the freq_bin/modulation gate
 * are excluded entirely. */
size_t sc_knn_match(
    const ScKnnQuery* q,
    const ScFingerprint* cands,
    size_t n,
    ScKnnMatch* out,
    size_t topn);

#endif /* SC_KNN_H */
