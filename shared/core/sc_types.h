/* sc_types.h — shared enums/constants for the SubCensus C logic core.
 *
 * Host-compilable (no Flipper/ESP headers). These mirror the schema enums in
 * the shared/schema specs so on-disk strings round-trip identically on every
 * sensor. See CLAUDE.md for the single-source-of-truth / no-drift rule.
 */
#ifndef SC_TYPES_H
#define SC_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Modulation class — matches the `modulation` enum in the schemas
 * ("OOK", "2-FSK", "TPMS-preset"). Hard k-NN gate (System §6). */
typedef enum {
    SC_MOD_UNKNOWN = -1,
    SC_MOD_OOK = 0,
    SC_MOD_2FSK = 1,
    SC_MOD_TPMS = 2,
} ScModulation;

static inline const char* sc_modulation_str(ScModulation m) {
    switch(m) {
    case SC_MOD_OOK:
        return "OOK";
    case SC_MOD_2FSK:
        return "2-FSK";
    case SC_MOD_TPMS:
        return "TPMS-preset";
    default:
        return "";
    }
}

static inline ScModulation sc_modulation_from_str(const char* s) {
    if(!s) return SC_MOD_UNKNOWN;
    if(strcmp(s, "OOK") == 0) return SC_MOD_OOK;
    if(strcmp(s, "2-FSK") == 0) return SC_MOD_2FSK;
    if(strcmp(s, "TPMS-preset") == 0) return SC_MOD_TPMS;
    return SC_MOD_UNKNOWN;
}

/* Cadence class — matches the `cadence_class` enum (System §7a). */
typedef enum {
    SC_CADENCE_NONE = -1, /* not yet estimated (empty in CSV) */
    SC_CADENCE_PERIODIC = 0,
    SC_CADENCE_QUASI_PERIODIC = 1,
    SC_CADENCE_EVENT_DRIVEN = 2,
    SC_CADENCE_NEAR_CONTINUOUS = 3,
    SC_CADENCE_SEEN_ONCE = 4,
} ScCadenceClass;

static inline const char* sc_cadence_str(ScCadenceClass c) {
    switch(c) {
    case SC_CADENCE_PERIODIC:
        return "periodic";
    case SC_CADENCE_QUASI_PERIODIC:
        return "quasi-periodic";
    case SC_CADENCE_EVENT_DRIVEN:
        return "event-driven";
    case SC_CADENCE_NEAR_CONTINUOUS:
        return "near-continuous";
    case SC_CADENCE_SEEN_ONCE:
        return "seen-once";
    default:
        return "";
    }
}

/* Canonical frequency binning (System §7): carrier binned to 5 kHz so vectors from
 * the Zero (CC1101 RAW) and the Pi (rtl_433/.cu8) land in the same k-NN space.
 * BOTH tools MUST bin identically — this is the binding rule. */
#define SC_FREQ_BIN_HZ 5000

static inline int32_t sc_freq_bin(int32_t freq_hz) {
    /* round-to-nearest bin, expressed back in Hz */
    int32_t half = SC_FREQ_BIN_HZ / 2;
    if(freq_hz >= 0) {
        return ((freq_hz + half) / SC_FREQ_BIN_HZ) * SC_FREQ_BIN_HZ;
    }
    return -(((-freq_hz + half) / SC_FREQ_BIN_HZ) * SC_FREQ_BIN_HZ);
}

#endif /* SC_TYPES_H */
