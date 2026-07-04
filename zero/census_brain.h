/* census_brain.h — fingerprints.csv parse/append + protocol_map lookup for the FAP (System §6).
 *
 * The gated k-NN itself is shared/core (sc_knn). This loads the global signatures/fingerprints.csv
 * into ScFingerprint[] for matching, and builds a row for the active-learning append (confirm a
 * label -> append the capture's feature vector, source=user). protocol_map lookup handles the
 * decodable tier. Advisory only — never auto-relabels (System §6).
 */
#ifndef CENSUS_BRAIN_H
#define CENSUS_BRAIN_H

#include <storage/storage.h>

#include "../shared/core/sc_knn.h"

#define CENSUS_BRAIN_MAX_FPS 64

typedef struct {
    ScFingerprint fps[CENSUS_BRAIN_MAX_FPS];
    char names[CENSUS_BRAIN_MAX_FPS][24];
    size_t count;
} CensusBrain;

/* Load signatures/fingerprints.csv (global brain) into `brain`. */
void census_brain_load(Storage* storage, CensusBrain* brain);

/* Append the confirmed capture's feature vector to the global fingerprints.csv (source=user)
 * and to the census_log label — the active-learning loop (System §6). Returns true on success. */
bool census_brain_confirm_label(
    Storage* storage,
    const ScFeatureVector* fv,
    const char* device_class,
    const char* device_name);

#endif /* CENSUS_BRAIN_H */
