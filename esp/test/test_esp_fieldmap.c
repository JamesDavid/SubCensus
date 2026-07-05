/* test_esp_fieldmap.c — field-map differential overlay + segment labeling (Esp §5, System §7b).
 *
 * Proves the ESP runs the PASSIVE analysis (sc_diff + sc_crc) on-device via shared/core
 * sc_fieldmap and proposes a field_maps/ entry, with NO radio (RF boundary: fixtures prove
 * processing). Mirrors the corpus shape asserted by the shared-core test_diff.c.
 */
#include "esp_fieldmap.h"
#include "sc_test.h"

#include <string.h>

/* Same corpus shape as test/core/test_diff.c:
 *   byte0 = 0xA5           (static address)
 *   byte1 = frame counter  (0..7)
 *   byte2 = 0x10/0x11      (slow-varying sensor value)
 *   byte3 = XOR(b0,b1,b2)  (checksum) */
static void build(uint8_t* corpus, int n) {
    for(int i = 0; i < n; i++) {
        uint8_t* f = corpus + i * ESP_FIELDMAP_MAX_BYTES;
        f[0] = 0xA5;
        f[1] = (uint8_t)i;
        f[2] = (i < n / 2) ? 0x10 : 0x11;
        f[3] = (uint8_t)(f[0] ^ f[1] ^ f[2]);
    }
}

static void test_analyze(void) {
    static uint8_t corpus[8 * ESP_FIELDMAP_MAX_BYTES];
    build(corpus, 8);

    ScFieldMap map;
    float conf = 0.0f;
    SC_CHECK(esp_fieldmap_analyze(corpus, 8, 4, "acurite", 0, &map, &conf), "analyze 8x4 corpus");
    SC_CHECK_INT((int)map.nbits, 32);
    SC_CHECK_INT((int)map.n_fields, 4);
    SC_CHECK(!map.user_confirmed, "proposal is unconfirmed until the user confirms");

    SC_CHECK_INT(map.fields[0].cls, SC_FIELD_STATIC);   /* address */
    SC_CHECK_INT(map.fields[1].cls, SC_FIELD_COUNTER);  /* frame counter */
    SC_CHECK_INT(map.fields[2].cls, SC_FIELD_SLOW);     /* sensor value */
    SC_CHECK_INT(map.fields[3].cls, SC_FIELD_CHECKSUM); /* trailing checksum */

    SC_CHECK(map.has_checksum, "checksum discovered");
    SC_CHECK_INT(map.checksum.kind, SC_CK_XOR);
    SC_CHECK_INT((int)map.checksum_over_bytes, 3);
    SC_CHECK(conf > 0.5f && conf <= 1.0f, "confidence in range");
}

static void test_reject_short(void) {
    static uint8_t corpus[1 * ESP_FIELDMAP_MAX_BYTES];
    build(corpus, 1);
    ScFieldMap map;
    SC_CHECK(!esp_fieldmap_analyze(corpus, 1, 4, "x", 0, &map, NULL),
             "single frame is not analyzable");
}

static void test_parse_hex(void) {
    /* two aligned 4-byte frames, mixed separators */
    const char* text = "A5 00 10 B5\nA5 01 10,B4\n";
    static uint8_t corpus[ESP_FIELDMAP_MAX_FRAMES * ESP_FIELDMAP_MAX_BYTES];
    size_t nbytes = 0;
    size_t nf = esp_fieldmap_parse_hex(text, corpus, ESP_FIELDMAP_MAX_FRAMES, &nbytes);
    SC_CHECK_INT((int)nf, 2);
    SC_CHECK_INT((int)nbytes, 4);
    SC_CHECK_INT(corpus[0], 0xA5);
    SC_CHECK_INT(corpus[3], 0xB5);
    SC_CHECK_INT(corpus[ESP_FIELDMAP_MAX_BYTES + 1], 0x01);

    /* misaligned frames are rejected (differential needs equal-length frames) */
    size_t nb2 = 0;
    SC_CHECK_INT((int)esp_fieldmap_parse_hex("A5 00\nA5 00 10\n", corpus, 8, &nb2), 0);
    /* dangling nibble rejected */
    SC_CHECK_INT((int)esp_fieldmap_parse_hex("A5 0\n", corpus, 8, &nb2), 0);
}

static void test_resign(void) {
    /* re-sign an edited frame through the shared field-map path (active-confirm re-sign) */
    static uint8_t corpus[8 * ESP_FIELDMAP_MAX_BYTES];
    build(corpus, 8);
    ScFieldMap map;
    esp_fieldmap_analyze(corpus, 8, 4, "acurite", 0, &map, NULL);

    uint8_t frame[4] = {0xA5, 0x02, 0x11, 0x00}; /* stale check byte */
    SC_CHECK(sc_fieldmap_resign(&map, frame, 4), "resign applies the discovered checksum");
    SC_CHECK_INT(frame[3], 0xA5 ^ 0x02 ^ 0x11);
}

static void test_json(void) {
    static uint8_t corpus[8 * ESP_FIELDMAP_MAX_BYTES];
    build(corpus, 8);
    ScFieldMap map;
    float conf = 0.0f;
    esp_fieldmap_analyze(corpus, 8, 4, "acurite:0x1234", 0, &map, &conf);

    /* apply user segment labels (the labeling step) then serialize */
    strncpy(map.fields[0].name, "address", sizeof(map.fields[0].name) - 1);
    strncpy(map.fields[2].semantics, "tracks temperature", sizeof(map.fields[2].semantics) - 1);

    char buf[1024];
    int n = esp_fieldmap_to_json(&map, conf, buf, sizeof(buf));
    SC_CHECK(n > 0, "json serialized");
    SC_CHECK(strstr(buf, "\"signature\":\"acurite:0x1234\"") != NULL, "signature present");
    SC_CHECK(strstr(buf, "\"name\":\"address\"") != NULL, "user segment label present");
    SC_CHECK(strstr(buf, "\"class\":\"checksum\"") != NULL, "checksum class present");
    SC_CHECK(strstr(buf, "\"kind\":\"xor\"") != NULL, "checksum kind present");
    SC_CHECK(strstr(buf, "tracks temperature") != NULL, "semantics label present");
    SC_CHECK(strstr(buf, "PROPOSAL") != NULL, "marked as a proposal (never auto-committed)");

    /* overflow is reported, not silently truncated */
    char tiny[16];
    SC_CHECK_INT(esp_fieldmap_to_json(&map, conf, tiny, sizeof(tiny)), -1);
}

static void test_fmap_roundtrip(void) {
    /* the confirmed structure persists as a .fmap and round-trips (shared/core format) */
    static uint8_t corpus[8 * ESP_FIELDMAP_MAX_BYTES];
    build(corpus, 8);
    ScFieldMap map;
    esp_fieldmap_analyze(corpus, 8, 4, "acurite", 0, &map, NULL);
    map.user_confirmed = true;

    char text[512];
    size_t n = sc_fieldmap_emit(&map, text, sizeof(text));
    SC_CHECK(n > 0, "emit .fmap");

    ScFieldMap back;
    SC_CHECK(sc_fieldmap_parse(text, n, &back), "parse .fmap");
    SC_CHECK_INT((int)back.n_fields, (int)map.n_fields);
    SC_CHECK(back.user_confirmed, "confirmed flag round-trips");
    SC_CHECK_INT(back.fields[3].cls, SC_FIELD_CHECKSUM);
}

int main(void) {
    printf("test_esp_fieldmap\n");
    test_analyze();
    test_reject_short();
    test_parse_hex();
    test_resign();
    test_json();
    test_fmap_roundtrip();
    return sc_test_summary();
}
