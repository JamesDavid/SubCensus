#include "census_freq.h"

#include <furi_hal_region.h>
#include <stdio.h>

#include "../shared/core/sc_freq_bands.h"

/* Default preset lists (Zero §3.1). 868 is EU — offered but off the US default. */
const uint32_t census_freq_us[] = {315000000, 390000000, 433920000, 915000000};
const size_t census_freq_us_count = 4;

const uint32_t census_freq_eu[] = {315000000, 390000000, 433920000, 868350000, 915000000};
const size_t census_freq_eu_count = 5;

const char* census_freq_preset_name(CensusFreqPreset p) {
    switch(p) {
    case CensusFreqPresetUS:
        return "US ISM";
    case CensusFreqPresetEU:
        return "EU ISM";
    case CensusFreqPresetCustom:
        return "Custom";
    default:
        return "?";
    }
}

bool census_freq_is_allowed(uint32_t freq_hz) {
    /* pure segment predicate (host-tested) AND the firmware region allow-list */
    if(!sc_freq_in_cc1101_band((int32_t)freq_hz)) return false;
    return furi_hal_region_is_frequency_allowed(freq_hz);
}

void census_freq_format_mhz(uint32_t freq_hz, char* buf, size_t cap) {
    uint32_t mhz = freq_hz / 1000000;
    uint32_t frac = (freq_hz % 1000000) / 10000; /* 2 decimals */
    snprintf(buf, cap, "%lu.%02lu", (unsigned long)mhz, (unsigned long)frac);
}
