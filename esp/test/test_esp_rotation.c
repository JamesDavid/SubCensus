/* test_esp_rotation.c — capped/rotating capture policy (Esp §4). */
#include "esp_rotation.h"
#include "sc_test.h"

int main(void) {
    printf("test_esp_rotation\n");

    /* count-based: cap 100 files */
    SC_CHECK_INT(esp_rotation_evict_for_count(50, 100), 0);   /* room */
    SC_CHECK_INT(esp_rotation_evict_for_count(99, 100), 0);   /* adding 1 -> exactly 100 */
    SC_CHECK_INT(esp_rotation_evict_for_count(100, 100), 1);  /* evict 1 oldest */
    SC_CHECK_INT(esp_rotation_evict_for_count(105, 100), 6);
    SC_CHECK_INT(esp_rotation_evict_for_count(50, 0), 0);     /* unlimited */

    /* size-based: cap total bytes; oldest first */
    long sizes[] = {1000, 2000, 3000};
    /* current 6000, incoming 500, max 6000 -> need to free >=500 -> evict oldest (1000) => 1 */
    SC_CHECK_INT(esp_rotation_evict_for_size(sizes, 3, 500, 6000, 6000), 1);
    /* incoming 2500 over 6000 cap: 6000+2500=8500, free until <=6000: 1000(->7500),2000(->5500) => 2 */
    SC_CHECK_INT(esp_rotation_evict_for_size(sizes, 3, 2500, 6000, 6000), 2);
    /* fits -> 0 */
    SC_CHECK_INT(esp_rotation_evict_for_size(sizes, 3, 100, 5000, 6000), 0);
    /* unlimited */
    SC_CHECK_INT(esp_rotation_evict_for_size(sizes, 3, 9999, 6000, 0), 0);

    return sc_test_summary();
}
