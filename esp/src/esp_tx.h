/* esp_tx.h — TX guard for replay / edit-before-transmit (Esp §3, System §7b), pure C.
 *
 * Monitoring is passive (Sweep/Camp/Recon never transmit). Replay/edit-TX is the ONLY TX path
 * and is: opt-in (off by default), explicit (never from a scan loop), single-frame (no
 * auto-increment / sweeping), and TX-allow-list gated. This guard is the gate the firmware
 * checks before any transmit; it's hardware-independent and unit-tested off-device.
 */
#ifndef ESP_TX_H
#define ESP_TX_H

#include <stdbool.h>
#include <stdint.h>

/* True only if TX is enabled in settings AND the frequency is in a CC1101 legal segment.
 * (Regulatory duty-cycle/power limits remain the operator's responsibility.) */
bool esp_tx_allowed(int32_t freq_hz, bool tx_enabled);

#endif /* ESP_TX_H */
