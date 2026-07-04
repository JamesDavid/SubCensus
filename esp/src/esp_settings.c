#include "esp_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void esp_settings_defaults(EspSettings* s) {
    memset(s, 0, sizeof(*s));
    strncpy(s->place_id, "home", ESP_PLACE_ID_LEN - 1);
    s->mode = EspModeCamp;
    s->freq_preset = 0;
    s->capture_preset = EspCaptureOok650;
    s->use_watchlist = true;
    s->rssi_auto = true;
    s->rssi_threshold = -80;
    s->dwell_ms = 80;
    s->capture_max_ms = 1500;
    s->signal_end_gap_ms = 120;
    s->survey_minutes = 15;
    s->auto_classify = true;
    s->match_db = true;
    s->tx_enabled = false;
    s->mqtt_enabled = false;
    s->mqtt_port = 1883;
}

int esp_settings_serialize(const EspSettings* s, char* out, size_t cap) {
    int n = snprintf(
        out, cap,
        "version=1\n"
        "place_id=%s\n"
        "mode=%u\n"
        "freq_preset=%u\n"
        "capture_preset=%u\n"
        "use_watchlist=%u\n"
        "rssi_auto=%u\n"
        "rssi_threshold=%ld\n"
        "dwell_ms=%lu\n"
        "capture_max_ms=%lu\n"
        "signal_end_gap_ms=%lu\n"
        "survey_minutes=%u\n"
        "auto_classify=%u\n"
        "match_db=%u\n"
        "tx_enabled=%u\n"
        "wifi_ssid=%s\n"
        "wifi_pass=%s\n"
        "mqtt_enabled=%u\n"
        "mqtt_host=%s\n"
        "mqtt_port=%u\n",
        s->place_id, s->mode, s->freq_preset, s->capture_preset, s->use_watchlist,
        s->rssi_auto, (long)s->rssi_threshold, (unsigned long)s->dwell_ms,
        (unsigned long)s->capture_max_ms, (unsigned long)s->signal_end_gap_ms,
        s->survey_minutes, s->auto_classify, s->match_db, s->tx_enabled, s->wifi_ssid,
        s->wifi_pass, s->mqtt_enabled, s->mqtt_host, s->mqtt_port);
    if(n < 0 || (size_t)n >= cap) return -1;
    return n;
}

/* Copy a value (up to newline) into dst[cap]. */
static void copy_val(const char* v, const char* vend, char* dst, size_t cap) {
    size_t len = (size_t)(vend - v);
    if(len >= cap) len = cap - 1;
    memcpy(dst, v, len);
    dst[len] = '\0';
}

bool esp_settings_deserialize(const char* text, EspSettings* s) {
    esp_settings_defaults(s);
    if(!text) return false;
    bool any = false;
    const char* p = text;
    while(*p) {
        const char* line = p;
        while(*p && *p != '\n') p++;
        const char* lend = p;
        if(*p == '\n') p++;

        const char* eq = line;
        while(eq < lend && *eq != '=') eq++;
        if(eq >= lend) continue;
        size_t klen = (size_t)(eq - line);
        const char* v = eq + 1;
        any = true;

#define KEY(name) (klen == strlen(name) && strncmp(line, name, klen) == 0)
        char buf[ESP_STR_LEN];
        if(KEY("place_id")) {
            copy_val(v, lend, s->place_id, ESP_PLACE_ID_LEN);
        } else if(KEY("wifi_ssid")) {
            copy_val(v, lend, s->wifi_ssid, ESP_STR_LEN);
        } else if(KEY("wifi_pass")) {
            copy_val(v, lend, s->wifi_pass, ESP_STR_LEN);
        } else if(KEY("mqtt_host")) {
            copy_val(v, lend, s->mqtt_host, ESP_STR_LEN);
        } else {
            copy_val(v, lend, buf, sizeof(buf));
            long iv = strtol(buf, NULL, 10);
            if(KEY("mode"))
                s->mode = (uint8_t)iv;
            else if(KEY("freq_preset"))
                s->freq_preset = (uint8_t)iv;
            else if(KEY("capture_preset"))
                s->capture_preset = (uint8_t)iv;
            else if(KEY("use_watchlist"))
                s->use_watchlist = iv != 0;
            else if(KEY("rssi_auto"))
                s->rssi_auto = iv != 0;
            else if(KEY("rssi_threshold"))
                s->rssi_threshold = (int32_t)iv;
            else if(KEY("dwell_ms"))
                s->dwell_ms = (uint32_t)iv;
            else if(KEY("capture_max_ms"))
                s->capture_max_ms = (uint32_t)iv;
            else if(KEY("signal_end_gap_ms"))
                s->signal_end_gap_ms = (uint32_t)iv;
            else if(KEY("survey_minutes"))
                s->survey_minutes = (uint16_t)iv;
            else if(KEY("auto_classify"))
                s->auto_classify = iv != 0;
            else if(KEY("match_db"))
                s->match_db = iv != 0;
            else if(KEY("tx_enabled"))
                s->tx_enabled = iv != 0;
            else if(KEY("mqtt_enabled"))
                s->mqtt_enabled = iv != 0;
            else if(KEY("mqtt_port"))
                s->mqtt_port = (uint16_t)iv;
        }
#undef KEY
    }
    return any;
}
