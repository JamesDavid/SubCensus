/* test_esp_storage.c — storage-tier selection (Esp §4). */
#include <string.h>

#include "esp_storage.h"
#include "sc_test.h"

int main(void) {
    printf("test_esp_storage\n");

    /* SD present -> full per-place model at /sd, no rotation */
    SC_CHECK_STR(esp_storage_base(true), "/sd");
    SC_CHECK_INT(esp_storage_rotation_enabled(true), 0);
    SC_CHECK_INT(esp_storage_max_captures(true, 200), 0); /* unlimited */

    /* no SD -> LittleFS at root, capped/rotating */
    SC_CHECK_STR(esp_storage_base(false), "");
    SC_CHECK_INT(esp_storage_rotation_enabled(false), 1);
    SC_CHECK_INT(esp_storage_max_captures(false, 200), 200);

    return sc_test_summary();
}
