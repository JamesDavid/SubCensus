#include "esp_mqtt.h"

#include <stdio.h>

int esp_mqtt_discovery_topic(
    const char* base_topic, const char* device_id, const char* entity, char* out, size_t cap) {
    int n = snprintf(out, cap, "homeassistant/sensor/%s/%s_%s/config", base_topic, device_id, entity);
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

int esp_mqtt_state_topic(const char* base_topic, const char* device_id, char* out, size_t cap) {
    int n = snprintf(out, cap, "%s/device/%s/state", base_topic, device_id);
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

int esp_mqtt_discovery_payload(
    const char* base_topic, const char* device_id, const char* device_name, const char* entity,
    const char* label, const char* value_template, const char* unit, const char* ha_class,
    char* out, size_t cap) {
    char state[96];
    esp_mqtt_state_topic(base_topic, device_id, state, sizeof(state));
    /* build the optional fields */
    char extra[128] = "";
    int eo = 0;
    if(unit && unit[0]) eo += snprintf(extra + eo, sizeof(extra) - eo, ",\"unit_of_measurement\":\"%s\"", unit);
    if(ha_class && ha_class[0]) eo += snprintf(extra + eo, sizeof(extra) - eo, ",\"device_class\":\"%s\"", ha_class);

    int n = snprintf(
        out, cap,
        "{\"name\":\"%s %s\",\"state_topic\":\"%s\",\"value_template\":\"%s\","
        "\"unique_id\":\"%s_%s_%s\",\"device\":{\"identifiers\":[\"%s_%s\"],"
        "\"name\":\"%s\",\"manufacturer\":\"SubCensusEsp\"}%s}",
        device_name, label, state, value_template, base_topic, device_id, entity,
        base_topic, device_id, device_name, extra);
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}
