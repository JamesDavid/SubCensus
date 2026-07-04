/* census_freq.h — frequency presets + allowed-frequency guard (Zero §3.1, §7).
 *
 * Monitoring rejects any freq outside the firmware RX allow-list (Zero §7). We combine the
 * pure CC1101-segment predicate (sc_freq_bands.h, host-testable) with the firmware region
 * check at runtime. TX (Replay) is separately gated by the TX allow-list (§6, later M8).
 */
#ifndef CENSUS_FREQ_H
#define CENSUS_FREQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Default preset lists (Zero §3.1). US/Tempe default; EU adds 868. */
extern const uint32_t census_freq_us[];
extern const size_t census_freq_us_count;
extern const uint32_t census_freq_eu[];
extern const size_t census_freq_eu_count;

typedef enum {
    CensusFreqPresetUS = 0,
    CensusFreqPresetEU = 1,
    CensusFreqPresetCustom = 2,
    CensusFreqPresetCount = 3,
} CensusFreqPreset;

const char* census_freq_preset_name(CensusFreqPreset p);

/* True if the frequency is legal to MONITOR (in a CC1101 segment AND region-allowed). */
bool census_freq_is_allowed(uint32_t freq_hz);

/* Format a frequency in MHz with 2 decimals into buf (e.g. "433.92"). */
void census_freq_format_mhz(uint32_t freq_hz, char* buf, size_t cap);

#endif /* CENSUS_FREQ_H */
