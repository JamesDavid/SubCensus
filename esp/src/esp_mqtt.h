/* esp_mqtt.h — MQTT -> Home Assistant discovery payloads (Esp §6), pure C so it host-tests.
 *
 * Same HA MQTT-discovery contract as the Pi (mqtt.py): a device surfaces as HA entities
 * (rssi / last_seen / count) grouped under one HA device. The firmware publishes these via a
 * real broker (TODO(hw)); the topic/payload construction is hardware-independent and tested.
 */
#ifndef ESP_MQTT_H
#define ESP_MQTT_H

#include <stddef.h>
#include <stdint.h>

/* Build a STABLE per-identified-device id from its class + frequency, e.g. "weather_433920"
 * (freq in kHz). Non-alphanumeric chars in the class (e.g. the '/' in "water/gas-meter") are
 * folded to '_' so the id is topic/unique_id safe. On a confident classification the node
 * publishes a discovery config under this id so each identified device surfaces as its own HA
 * entity (Esp §6), not just the one node-level rssi sensor. Returns bytes written or -1. */
int esp_mqtt_device_id(const char* device_class, int32_t freq_hz, char* out, size_t cap);

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
