/* esp_mqtt.h — MQTT -> Home Assistant discovery payloads (Esp §6), pure C so it host-tests.
 *
 * Same HA MQTT-discovery contract as the Pi (mqtt.py): a device surfaces as HA entities
 * (rssi / last_seen / count) grouped under one HA device. The firmware publishes these via a
 * real broker (TODO(hw)); the topic/payload construction is hardware-independent and tested.
 */
#ifndef ESP_MQTT_H
#define ESP_MQTT_H

#include <stddef.h>

/* homeassistant/sensor/<base>/<device_id>_<entity>/config */
int esp_mqtt_discovery_topic(
    const char* base_topic, const char* device_id, const char* entity, char* out, size_t cap);

/* <base>/device/<device_id>/state */
int esp_mqtt_state_topic(const char* base_topic, const char* device_id, char* out, size_t cap);

/* HA discovery config payload for one entity (unit/device_class optional -> pass "" to skip).
 * Groups under one HA device via identifiers=[<base>_<device_id>]. */
int esp_mqtt_discovery_payload(
    const char* base_topic, const char* device_id, const char* device_name, const char* entity,
    const char* label, const char* value_template, const char* unit, const char* ha_class,
    char* out, size_t cap);

#endif /* ESP_MQTT_H */
