/* test_esp_catalog.c — per-device running cadence estimator + derived catalog record
 * (Esp §3, System §7a / §9). Proves the signature-keyed running estimator on fixtures — no
 * radio, no live RF (the RF boundary). This is the logic /api/debug/inject drives on-device. */
#include <string.h>

#include "census_schema.h"
#include "esp_catalog.h"
#include "sc_test.h"
#include "sc_types.h"

static ScFeatureVector fv_make(int32_t freq_bin, ScModulation mod, int32_t s0, int32_t s1) {
    ScFeatureVector fv;
    memset(&fv, 0, sizeof(fv));
    fv.freq_bin = freq_bin;
    fv.modulation = mod;
    fv.sym_dur_us[0] = s0;
    fv.sym_dur_us[1] = s1;
    fv.n_sym_dur = 2;
    return fv;
}

int main(void) {
    printf("test_esp_catalog\n");

    /* --- signature key: same waveform -> same key; different -> different --- */
    ScFeatureVector a = fv_make(433920000, SC_MOD_OOK, 350, 700);
    ScFeatureVector a2 = fv_make(433920000, SC_MOD_OOK, 350, 700);
    ScFeatureVector b = fv_make(433920000, SC_MOD_OOK, 500, 1000);
    ScFeatureVector c = fv_make(315000000, SC_MOD_OOK, 350, 700);
    SC_CHECK(esp_catalog_key(&a) == esp_catalog_key(&a2), "same waveform -> same key");
    SC_CHECK(esp_catalog_key(&a) != esp_catalog_key(&b), "diff symbols -> diff key");
    SC_CHECK(esp_catalog_key(&a) != esp_catalog_key(&c), "diff freq -> diff key");

    /* --- running estimator: metronomic 10 s cadence -> periodic --- */
    EspCatalog cat;
    esp_catalog_init(&cat, 1.0f);
    ScCadenceEstimate est;
    int slot = -1;
    for(int i = 0; i < 6; i++) {
        int64_t ts = 1000 + (int64_t)i * 10; /* every 10 s */
        slot = esp_catalog_observe(&cat, &a, 433920000, ts, "2026-07-04T00:00:00", &est);
    }
    SC_CHECK(slot >= 0, "slot allocated");
    SC_CHECK_INT(est.cls, SC_CADENCE_PERIODIC);
    SC_CHECK_INT(est.samples, 5); /* 6 receptions -> 5 intervals */
    SC_CHECK_DBL(est.period_s, 10.0, 1.5);

    /* a distinct device gets its own slot (independent estimator) */
    int slot_b = esp_catalog_observe(&cat, &b, 433920000, 5000, "2026-07-04T01:00:00", &est);
    SC_CHECK(slot_b >= 0 && slot_b != slot, "distinct signature -> distinct slot");
    SC_CHECK_INT(est.cls, SC_CADENCE_SEEN_ONCE); /* only one reception so far */

    /* --- catalog row carries cadence + (advisory) match, in CATALOG_RECORD column order --- */
    esp_catalog_set_match(&cat, slot, "PT2262 remote", "remote", 0.91f, "fingerprint");
    char row[288];
    int n = esp_catalog_row(&cat, slot, row, sizeof(row));
    SC_CHECK(n > 0, "row built");
    SC_CHECK(strstr(row, ",433920000,OOK,") != NULL, "freq + modulation");
    SC_CHECK(strstr(row, ",periodic,") != NULL, "cadence_class");
    SC_CHECK(strstr(row, "PT2262 remote,remote,0.91,fingerprint") != NULL, "advisory match");
    /* 16 columns per CATALOG_RECORD_HEADER (15 commas) */
    int commas = 0;
    for(const char* p = row; *p; p++)
        if(*p == ',') commas++;
    SC_CHECK_INT(commas, CATALOG_RECORD_NCOLS - 1);

    /* empty slot -> no row */
    SC_CHECK(esp_catalog_row(&cat, ESP_CATALOG_MAX - 1, row, sizeof(row)) < 0, "empty slot no row");

    /* --- peek is read-only: it must not add a reception --- */
    ScCadenceEstimate pk;
    SC_CHECK(esp_catalog_peek(&cat, &a, &pk), "peek finds seen device");
    SC_CHECK_INT(pk.samples, 5); /* unchanged by peek */
    SC_CHECK(!esp_catalog_peek(&cat, &c, &pk), "peek misses unseen device");

    return sc_test_summary();
}
