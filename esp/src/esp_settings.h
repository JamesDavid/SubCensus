/* esp_settings.h — SubCensusEsp node settings (Esp §5 Settings), pure C so it host-tests.
 *
 * Serialized as key=value lines to NVS/LittleFS by the firmware; the (de)serialization here
 * is hardware-independent and unit-tested off-device. Mode/preset/threshold semantics mirror
 * the Zero (shared capture model); WiFi/MQTT are ESP-specific (Esp §6).
 */
#ifndef ESP_SETTINGS_H
#define ESP_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP_PLACE_ID_LEN 40
#define ESP_STR_LEN 64

typedef enum { EspModeRecon = 0, EspModeSweep = 1, EspModeCamp = 2 } EspMode;
typedef enum {
    EspCaptureOok650 = 0,
    EspCaptureOok270 = 1,
    EspCaptureFsk = 2,
    EspCaptureDual = 3,
} EspCapturePreset;

typedef struct {
    char place_id[ESP_PLACE_ID_LEN];
    uint8_t mode;           /* EspMode */
    uint8_t freq_preset;    /* 0 US / 1 EU / 2 custom */
    uint8_t capture_preset; /* EspCapturePreset */
    bool use_watchlist;
    bool rssi_auto;
    int32_t rssi_threshold; /* dBm when !rssi_auto */
    uint32_t dwell_ms;
    uint32_t capture_max_ms;
    uint32_t signal_end_gap_ms;
    uint16_t survey_minutes;
    bool auto_classify;
    bool match_db;
    bool tx_enabled; /* replay/edit-TX opt-in; OFF by default (Esp §3) */
    /* networking (Esp §6) */
    char wifi_ssid[ESP_STR_LEN];
    char wifi_pass[ESP_STR_LEN];
    bool mqtt_enabled;
    char mqtt_host[ESP_STR_LEN];
    uint16_t mqtt_port;
} EspSettings;

void esp_settings_defaults(EspSettings* s);

/* Serialize to key=value lines in out[cap] (NUL-terminated). Returns bytes written, or -1
 * on overflow. Secrets (wifi_pass) are included — the caller stores this in protected NVS. */
int esp_settings_serialize(const EspSettings* s, char* out, size_t cap);

/* Parse key=value lines (unknown keys ignored; missing keys keep their default). Returns
 * true if at least a header/any field parsed. Applies defaults first. */
bool esp_settings_deserialize(const char* text, EspSettings* s);

#endif /* ESP_SETTINGS_H */
