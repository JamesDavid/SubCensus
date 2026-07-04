#include "esp_place.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

void esp_place_id_from_name(const char* name, char* out_id, size_t cap) {
    char slug[40];
    size_t j = 0;
    int prev_dash = 0;
    for(size_t k = 0; name[k] && j < sizeof(slug) - 6; k++) {
        char c = name[k];
        if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        int alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if(alnum) {
            slug[j++] = c;
            prev_dash = 0;
        } else if(!prev_dash && j > 0) {
            slug[j++] = '-';
            prev_dash = 1;
        }
    }
    while(j > 0 && slug[j - 1] == '-') j--;
    slug[j] = '\0';
    if(j == 0) {
        strncpy(slug, "place", sizeof(slug) - 6);
        slug[sizeof(slug) - 6] = '\0';
    }
    /* short djb2 hash of the original name for uniqueness/rename-safety (System §4) */
    uint32_t h = 5381;
    for(size_t k = 0; name[k]; k++) h = ((h << 5) + h) + (uint8_t)name[k];
    snprintf(out_id, cap, "%s_%04x", slug, (unsigned)(h & 0xFFFF));
}

void esp_place_dir(const char* base, const char* place_id, char* out, size_t cap) {
    snprintf(out, cap, "%s/places/%s", base, place_id);
}

void esp_place_file(const char* base, const char* place_id, const char* file, char* out, size_t cap) {
    snprintf(out, cap, "%s/places/%s/%s", base, place_id, file);
}

void esp_signatures_dir(const char* base, char* out, size_t cap) {
    snprintf(out, cap, "%s/signatures", base);
}
