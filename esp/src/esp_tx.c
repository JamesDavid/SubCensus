#include "esp_tx.h"

#include "sc_freq_bands.h"

bool esp_tx_allowed(int32_t freq_hz, bool tx_enabled) {
    if(!tx_enabled) return false;              /* opt-in; off by default (Esp §3) */
    return sc_freq_in_cc1101_band(freq_hz);    /* TX allow-list = CC1101 legal segments */
}
