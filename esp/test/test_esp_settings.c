/* test_esp_settings.c — settings serialize/deserialize round-trip (Esp §5). */
#include "esp_settings.h"
#include "sc_test.h"

int main(void) {
    printf("test_esp_settings\n");

    EspSettings d;
    esp_settings_defaults(&d);
    SC_CHECK_STR(d.place_id, "home");
    SC_CHECK_INT(d.mode, EspModeCamp);
    SC_CHECK_INT(d.rssi_auto, 1);
    SC_CHECK_INT(d.tx_enabled, 0); /* TX opt-in, off by default (Esp §3) */
    SC_CHECK_INT(d.mqtt_port, 1883);

    /* mutate + round-trip through serialize/deserialize */
    EspSettings s;
    esp_settings_defaults(&s);
    s.mode = EspModeSweep;
    s.rssi_auto = false;
    s.rssi_threshold = -72;
    s.dwell_ms = 120;
    s.capture_max_ms = 2000;
    s.survey_minutes = 30;
    s.tx_enabled = true;
    s.mqtt_enabled = true;
    s.mqtt_port = 1884;
    strncpy(s.place_id, "garage_1a2b", ESP_PLACE_ID_LEN - 1);
    strncpy(s.wifi_ssid, "HomeNet", ESP_STR_LEN - 1);
    strncpy(s.wifi_pass, "p@ss w0rd", ESP_STR_LEN - 1);
    strncpy(s.mqtt_host, "192.168.1.10", ESP_STR_LEN - 1);

    char buf[1024];
    int n = esp_settings_serialize(&s, buf, sizeof(buf));
    SC_CHECK(n > 0, "serialize produced output");

    EspSettings r;
    SC_CHECK(esp_settings_deserialize(buf, &r), "deserialize ok");
    SC_CHECK_INT(r.mode, EspModeSweep);
    SC_CHECK_INT(r.rssi_auto, 0);
    SC_CHECK_INT(r.rssi_threshold, -72);
    SC_CHECK_INT(r.dwell_ms, 120);
    SC_CHECK_INT(r.capture_max_ms, 2000);
    SC_CHECK_INT(r.survey_minutes, 30);
    SC_CHECK_INT(r.tx_enabled, 1);
    SC_CHECK_INT(r.mqtt_enabled, 1);
    SC_CHECK_INT(r.mqtt_port, 1884);
    SC_CHECK_STR(r.place_id, "garage_1a2b");
    SC_CHECK_STR(r.wifi_ssid, "HomeNet");
    SC_CHECK_STR(r.wifi_pass, "p@ss w0rd"); /* value with a space preserved to end-of-line */
    SC_CHECK_STR(r.mqtt_host, "192.168.1.10");

    /* unknown keys ignored; missing keys keep defaults */
    EspSettings p;
    SC_CHECK(esp_settings_deserialize("mode=1\nbogus=9\n", &p), "partial parse");
    SC_CHECK_INT(p.mode, EspModeSweep);
    SC_CHECK_INT(p.mqtt_port, 1883); /* default kept */

    return sc_test_summary();
}
