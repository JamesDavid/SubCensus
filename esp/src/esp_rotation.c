#include "esp_rotation.h"

int esp_rotation_evict_for_count(int current_count, int max_count) {
    if(max_count <= 0) return 0;
    int evict = current_count + 1 - max_count;
    return evict > 0 ? evict : 0;
}

int esp_rotation_evict_for_size(
    const long* oldest_sizes, int n, long incoming, long current_total, long max_total) {
    if(max_total <= 0) return 0;
    long total = current_total + incoming;
    int evict = 0;
    while(total > max_total && evict < n) {
        total -= oldest_sizes[evict];
        evict++;
    }
    return evict;
}
