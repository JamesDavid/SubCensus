/* sc_freq_bands.h — CC1101 legal-segment membership (host-testable, System §3.3 / Zero §7).
 *
 * The CC1101 legal RX segments are 300-348 / 387-464 / 779-928 MHz (Zero §3.3 Stage A).
 * This pure predicate is the deterministic, host-testable half of the allowed-frequency
 * guard (Zero §7); the firmware also consults the region TX/RX allow-list at runtime
 * (census_freq.c). Kept in shared/core so the Esp reuses it.
 */
#ifndef SC_FREQ_BANDS_H
#define SC_FREQ_BANDS_H

#include <stdbool.h>
#include <stdint.h>

/* True if freq_hz falls in a CC1101 legal segment (300-348 / 387-464 / 779-928 MHz). */
static inline bool sc_freq_in_cc1101_band(int32_t freq_hz) {
    return (freq_hz >= 300000000 && freq_hz <= 348000000) ||
           (freq_hz >= 387000000 && freq_hz <= 464000000) ||
           (freq_hz >= 779000000 && freq_hz <= 928000000);
}

#endif /* SC_FREQ_BANDS_H */
