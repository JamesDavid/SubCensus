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

    return sc_test_summary();
}
