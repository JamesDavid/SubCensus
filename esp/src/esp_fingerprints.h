/* esp_fingerprints.h — fingerprints.csv parse + row build (System §6, §7), pure C.
 *
 * The gated k-NN itself is shared/core (sc_knn); this parses the shared fingerprints.csv into
 * ScFingerprint and builds a row for the active-learning loop (confirm a label -> append the
 * capture's feature vector with source=user, System §6). Cadence fields are a soft feature
 * (System §6) and are parsed when present. The ESP classifies mostly via fingerprints — it
 * lacks the Flipper's rich decoder registry (Esp §3 honest limitation).
 */
#ifndef ESP_FINGERPRINTS_H
#define ESP_FINGERPRINTS_H

#include <stdbool.h>
#include <stddef.h>

#include "sc_knn.h"

/* Parse a fingerprints.csv DATA row into *out. device_name is copied into name_buf (borrowed
 * by out->device_name). Returns true on success. */
bool esp_fingerprint_parse_line(
    const char* line, ScFingerprint* out, char* name_buf, size_t name_cap);

/* Build a fingerprints.csv row (source=user) from a capture's feature vector + confirmed
 * label, for the active-learning append (System §6). Returns bytes written or -1 on overflow. */
int esp_fingerprint_row(
    const char* id, const ScFeatureVector* fv, const char* device_name,
    const char* device_class, char* out, size_t cap);

#endif /* ESP_FINGERPRINTS_H */
