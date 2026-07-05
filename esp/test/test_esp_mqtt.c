/* test_esp_mqtt.c — HA discovery payloads (Esp §6). Same contract as the Pi's mqtt.py. */
#include <string.h>

#include "esp_mqtt.h"
#include "sc_test.h"

int main(void) {
    printf("test_esp_mqtt\n");

    char topic[128];
    esp_mqtt_discovery_topic("subcensusesp", "abc123", "rssi", topic, sizeof(topic));
    SC_CHECK_STR(topic, "homeassistant/sensor/subcensusesp/abc123_rssi/config");

    esp_mqtt_state_topic("subcensusesp", "abc123", topic, sizeof(topic));
    SC_CHECK_STR(topic, "subcensusesp/device/abc123/state");

    char payload[512];
    int n = esp_mqtt_discovery_payload(
        "subcensusesp", "abc123", "Garden temp", "rssi", "RSSI",
        "{{ value_json.rssi }}", "dBm", "signal_strength", payload, sizeof(payload));
    SC_CHECK(n > 0, "payload built");
    /* grouped under one HA device, unique id, state topic, signal_strength class */
    SC_CHECK(strstr(payload, "\"unique_id\":\"subcensusesp_abc123_rssi\"") != NULL, "unique_id");
    SC_CHECK(strstr(payload, "\"identifiers\":[\"subcensusesp_abc123\"]") != NULL, "device identifiers");
    SC_CHECK(strstr(payload, "\"state_topic\":\"subcensusesp/device/abc123/state\"") != NULL, "state topic");
    SC_CHECK(strstr(payload, "\"device_class\":\"signal_strength\"") != NULL, "ha device_class");
    SC_CHECK(strstr(payload, "\"unit_of_measurement\":\"dBm\"") != NULL, "unit");

    /* optional fields omitted when empty */
    n = esp_mqtt_discovery_payload("b", "d", "Dev", "count", "Count", "{{ value_json.count }}",
                                   "", "", payload, sizeof(payload));
    SC_CHECK(n > 0, "count payload built");
    SC_CHECK(strstr(payload, "unit_of_measurement") == NULL, "no unit when empty");
    SC_CHECK(strstr(payload, "device_class") == NULL, "no ha class when empty");

    /* --- per-identified-device id (Esp §6): stable, topic-safe, class+freq-scoped --- */
    char dev[32];
    esp_mqtt_device_id("weather", 433920000, dev, sizeof(dev));
    SC_CHECK_STR(dev, "weather_433920"); /* freq in kHz */
    /* the '/' in "water/gas-meter" is folded so the id stays a valid topic/unique_id token */
    esp_mqtt_device_id("water/gas-meter", 915000000, dev, sizeof(dev));
    SC_CHECK_STR(dev, "water_gas_meter_915000");
    /* distinct devices -> distinct unique_id + HA device grouping */
    char id_a[32], id_b[32], pa[512], pb[512];
    esp_mqtt_device_id("weather", 433920000, id_a, sizeof(id_a));
    esp_mqtt_device_id("tpms", 315000000, id_b, sizeof(id_b));
    esp_mqtt_discovery_payload("subcensusesp", id_a, "Acurite", "rssi", "RSSI",
                               "{{ value_json.rssi }}", "dBm", "signal_strength", pa, sizeof(pa));
    esp_mqtt_discovery_payload("subcensusesp", id_b, "TPMS", "rssi", "RSSI",
                               "{{ value_json.rssi }}", "dBm", "signal_strength", pb, sizeof(pb));
    SC_CHECK(strstr(pa, "\"unique_id\":\"subcensusesp_weather_433920_rssi\"") != NULL, "device A uid");
    SC_CHECK(strstr(pb, "\"unique_id\":\"subcensusesp_tpms_315000_rssi\"") != NULL, "device B uid");
    SC_CHECK(strstr(pa, "\"identifiers\":[\"subcensusesp_weather_433920\"]") != NULL, "device A group");

    return sc_test_summary();
}
