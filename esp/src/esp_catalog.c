#include "esp_catalog.h"

#include <stdio.h>
#include <string.h>

#include "census_schema.h" /* CATALOG_RECORD_HEADER column order */
#include "sc_types.h"

void esp_catalog_init(EspCatalog* c, float bin_width_s) {
    memset(c, 0, sizeof(*c));
    c->bin_width_s = bin_width_s > 0 ? bin_width_s : 1.0f;
}

uint64_t esp_catalog_key(const ScFeatureVector* fv) {
    /* FNV-1a-ish rolling hash over the gating features (freq_bin, modulation, symbol
     * durations) — the SAME fields the k-NN hard-gates + ranks on, so a device's repeat
     * receptions collapse onto one running estimator. */
    uint64_t k = 1469598103934665603ULL;
    const uint64_t prime = 1099511628211ULL;
    int32_t fields[5] = {fv->freq_bin, (int32_t)fv->modulation + 2, fv->sym_dur_us[0],
                         fv->sym_dur_us[1], fv->sym_dur_us[2]};
    for(int i = 0; i < 5; i++) {
        k ^= (uint32_t)fields[i];
        k *= prime;
    }
    return k;
}

int esp_catalog_observe(
    EspCatalog* c, const ScFeatureVector* fv, int32_t freq_hz, int64_t ts_s,
    const char* ts_iso, ScCadenceEstimate* est) {
    uint64_t key = esp_catalog_key(fv);
    int slot = -1, free_slot = -1;
    for(int i = 0; i < ESP_CATALOG_MAX; i++) {
        if(c->slots[i].used && c->slots[i].key == key) {
            slot = i;
            break;
        }
        if(!c->slots[i].used && free_slot < 0) free_slot = i;
    }
    if(slot < 0) {
        if(free_slot < 0) return -1; /* table full (bounded RAM, System §7a) */
        slot = free_slot;
        EspCatalogSlot* s = &c->slots[slot];
        memset(s, 0, sizeof(*s));
        s->used = true;
        s->key = key;
        s->freq_hz = freq_hz;
        s->modulation = fv->modulation;
        sc_cadence_init(&s->cad, c->bin_width_s);
        if(ts_iso) snprintf(s->first_seen, sizeof(s->first_seen), "%s", ts_iso);
    }
    EspCatalogSlot* s = &c->slots[slot];
    sc_cadence_observe(&s->cad, ts_s);
    s->count++;
    if(ts_iso) snprintf(s->last_seen, sizeof(s->last_seen), "%s", ts_iso);
    if(est) sc_cadence_estimate(&s->cad, est);
    return slot;
}

bool esp_catalog_peek(const EspCatalog* c, const ScFeatureVector* fv, ScCadenceEstimate* est) {
    uint64_t key = esp_catalog_key(fv);
    for(int i = 0; i < ESP_CATALOG_MAX; i++) {
        if(c->slots[i].used && c->slots[i].key == key) {
            if(est) sc_cadence_estimate(&c->slots[i].cad, est);
            return true;
        }
    }
    return false;
}

void esp_catalog_set_match(
    EspCatalog* c, int slot, const char* name, const char* cls, float conf, const char* source) {
    if(slot < 0 || slot >= ESP_CATALOG_MAX || !c->slots[slot].used) return;
    EspCatalogSlot* s = &c->slots[slot];
    snprintf(s->match_name, sizeof(s->match_name), "%s", name ? name : "");
    snprintf(s->match_class, sizeof(s->match_class), "%s", cls ? cls : "");
    s->match_conf = conf;
    snprintf(s->match_source, sizeof(s->match_source), "%s", source ? source : "");
}

int esp_catalog_row(const EspCatalog* c, int i, char* out, size_t cap) {
    if(i < 0 || i >= ESP_CATALOG_MAX || !c->slots[i].used) return -1;
    const EspCatalogSlot* s = &c->slots[i];

    ScCadenceEstimate est;
    sc_cadence_estimate(&s->cad, &est);

    /* null-able fields render empty (System §7a / §9): period_s is null for event-driven /
     * seen-once, cadence_class + match_* empty when absent. */
    char conf[16] = "", period[16] = "", regularity[16] = "", samples[16] = "";
    const char* cadence = sc_cadence_str(est.cls); /* "" for SC_CADENCE_NONE */
    if(s->match_source[0]) snprintf(conf, sizeof(conf), "%.2f", (double)s->match_conf);
    if(est.cls != SC_CADENCE_NONE) {
        if(est.period_s > 0) snprintf(period, sizeof(period), "%.1f", (double)est.period_s);
        snprintf(regularity, sizeof(regularity), "%.3f", (double)est.regularity);
        snprintf(samples, sizeof(samples), "%ld", (long)est.samples);
    }

    /* CATALOG_RECORD_HEADER:
     * ts,freq_hz,modulation,device_class,first_seen,last_seen,count,match_name,match_class,
     * match_conf,match_source,label,cadence_class,period_s,period_regularity,cadence_samples */
    int n = snprintf(
        out, cap, "%s,%ld,%s,,%s,%s,%ld,%s,%s,%s,%s,,%s,%s,%s,%s", s->last_seen,
        (long)s->freq_hz, sc_modulation_str(s->modulation), s->first_seen, s->last_seen,
        (long)s->count, s->match_name, s->match_class, conf, s->match_source, cadence, period,
        regularity, samples);
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}
