/* esp_catalog.h — per-device running cadence estimator + derived catalog record (Esp §3,
 * System §7a / §9), pure C so it host-tests.
 *
 * Being always-on makes the ESP a strong cadence measurer (Esp §3): over hours/days it
 * accumulates inter-arrival statistics per device. Rather than a full event log, it keeps a
 * COMPACT per-signature running estimator (sc_cadence's ScCadenceEstimator: last_ts, count,
 * running mean/variance, small interval histogram) in RAM and flushes the derived cadence_*
 * fields to the catalog record (System §7a, §9). The host merge (build_signatures.py /
 * analyze_place) recomputes canonically; this is the live on-device estimate.
 *
 * A "signature" here is the device's waveform fingerprint (freq bin + modulation + dominant
 * symbol durations) — the same features the k-NN gates on — so repeat receptions of one device
 * collapse onto one running estimator. The estimate feeds BOTH the k-NN query (cadence is a
 * soft booster, System §6) and the catalog record.
 */
#ifndef ESP_CATALOG_H
#define ESP_CATALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sc_cadence.h"
#include "sc_feature.h"

#define ESP_CATALOG_MAX 32       /* bounded per-place device table (RAM, no event log) */
#define ESP_CATALOG_STR 32

typedef struct {
    bool used;
    uint64_t key;             /* signature key (esp_catalog_key) */
    int32_t freq_hz;
    ScModulation modulation;
    char first_seen[24];      /* ISO wall-clock of first reception */
    char last_seen[24];       /* ISO wall-clock of most recent reception */
    int32_t count;
    char match_name[ESP_CATALOG_STR];   /* advisory (System §6) */
    char match_class[ESP_CATALOG_STR];
    float match_conf;
    char match_source[16];
    ScCadenceEstimator cad;   /* compact running estimator (System §7a) */
} EspCatalogSlot;

typedef struct {
    EspCatalogSlot slots[ESP_CATALOG_MAX];
    float bin_width_s;        /* cadence histogram bin width (seconds) */
} EspCatalog;

/* Init an empty catalog; bin_width_s is the cadence histogram resolution (e.g. 1.0). */
void esp_catalog_init(EspCatalog* c, float bin_width_s);

/* Stable signature key from a feature vector: freq_bin + modulation + dominant symbol
 * durations. Identical waveforms hash identically so their receptions pool into one
 * estimator; distinct waveforms (almost always) differ. */
uint64_t esp_catalog_key(const ScFeatureVector* fv);

/* Observe a reception of `fv` at epoch `ts_s` / wall-clock `ts_iso`. Finds or allocates the
 * per-signature slot, feeds the running cadence estimator, updates aggregates, and writes the
 * current cadence estimate to *est (may be NULL). Returns the slot index, or -1 if full. */
int esp_catalog_observe(
    EspCatalog* c, const ScFeatureVector* fv, int32_t freq_hz, int64_t ts_s,
    const char* ts_iso, ScCadenceEstimate* est);

/* Read-only lookup of the current cadence estimate for a signature WITHOUT recording a new
 * reception. Returns true and fills *est if the device has a slot, false otherwise. Used by the
 * Review candidates path so its k-NN query sees the same soft cadence the live classify does. */
bool esp_catalog_peek(const EspCatalog* c, const ScFeatureVector* fv, ScCadenceEstimate* est);

/* Attach the (advisory, never auto-confirmed) classification result to a slot. */
void esp_catalog_set_match(
    EspCatalog* c, int slot, const char* name, const char* cls, float conf, const char* source);

/* Format slot `i` as a CATALOG_RECORD CSV row (no trailing newline; column order matches the
 * generated CATALOG_RECORD_HEADER). The cadence fields are recomputed from the running
 * estimator at format time. Returns bytes written, or -1 on overflow / empty slot. */
int esp_catalog_row(const EspCatalog* c, int i, char* out, size_t cap);

#endif /* ESP_CATALOG_H */
