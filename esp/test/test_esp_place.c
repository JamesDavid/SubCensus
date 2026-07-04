/* test_esp_place.c — place_id slug + path helpers (Esp §4). */
#include <string.h>

#include "esp_place.h"
#include "sc_test.h"

int main(void) {
    printf("test_esp_place\n");

    char id[40];
    esp_place_id_from_name("Home", id, sizeof(id));
    /* slug + short hash; rename-safe (System §4) */
    SC_CHECK(strncmp(id, "home_", 5) == 0, "slug prefix 'home_'");
    SC_CHECK(strlen(id) == 9, "home_ + 4 hex chars");

    char id2[40];
    esp_place_id_from_name("Home", id2, sizeof(id2));
    SC_CHECK_STR(id, id2); /* deterministic */

    char id3[40];
    esp_place_id_from_name("My Truck!", id3, sizeof(id3));
    SC_CHECK(strncmp(id3, "my-truck_", 9) == 0, "spaces/punct -> single dashes, trimmed");

    char empty[40];
    esp_place_id_from_name("", empty, sizeof(empty));
    SC_CHECK(strncmp(empty, "place_", 6) == 0, "empty name -> 'place_' fallback");

    /* paths: identical layout on LittleFS ("/") or SD ("/sd"); only the base differs */
    char path[128];
    esp_place_file("/littlefs", "home_1a2b", "census_log.csv", path, sizeof(path));
    SC_CHECK_STR(path, "/littlefs/places/home_1a2b/census_log.csv");

    esp_place_dir("/sd", "home_1a2b", path, sizeof(path));
    SC_CHECK_STR(path, "/sd/places/home_1a2b");

    esp_signatures_dir("/littlefs", path, sizeof(path));
    SC_CHECK_STR(path, "/littlefs/signatures"); /* GLOBAL, never per-place */

    return sc_test_summary();
}
