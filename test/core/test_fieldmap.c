/* test_fieldmap.c — per-protocol field map: bitfield get/set, checksum re-sign, .fmap
 * parse/emit round-trip, and differential-seeded proposal (System §6, §7b). */
#include "sc_crc.h"
#include "sc_diff.h"
#include "sc_fieldmap.h"
#include "sc_test.h"

#include <string.h>

static void test_bitfield(void) {
    /* frame: 0xA5 0x3C 0x00 ; MSB-first indexing */
    uint8_t frame[3] = {0xA5, 0x3C, 0x00};
    SC_CHECK_INT((int)sc_field_get(frame, 3, 0, 8), 0xA5);
    SC_CHECK_INT((int)sc_field_get(frame, 3, 8, 8), 0x3C);
    SC_CHECK_INT((int)sc_field_get(frame, 3, 0, 4), 0xA); /* high nibble of byte0 */
    SC_CHECK_INT((int)sc_field_get(frame, 3, 4, 4), 0x5);
    SC_CHECK_INT((int)sc_field_get(frame, 3, 0, 16), 0xA53C);

    /* set a 12-bit field spanning byte2 + into byte..: write 0xABC at bit 8 (byte1..2) */
    sc_field_set(frame, 3, 8, 12, 0xABC);
    SC_CHECK_INT((int)sc_field_get(frame, 3, 8, 12), 0xABC);
    /* byte0 untouched */
    SC_CHECK_INT(frame[0], 0xA5);

    /* set beyond the buffer is clamped (no crash) */
    sc_field_set(frame, 3, 20, 8, 0xFF);
    SC_CHECK_INT((int)sc_field_get(frame, 3, 20, 4), 0xF);
}

static void test_checksum_resign(void) {
    /* build a map: byte0 id (static), byte1 data (slow), byte2 checksum = XOR(b0,b1) */
    ScFieldMap map;
    memset(&map, 0, sizeof(map));
    strcpy(map.protocol, "test_xor");
    map.nbits = 24;
    map.modulation = 0;
    map.n_fields = 3;
    strcpy(map.fields[0].name, "id");
    map.fields[0].start_bit = 0;
    map.fields[0].length = 8;
    map.fields[0].cls = SC_FIELD_STATIC;
    strcpy(map.fields[1].name, "data");
    map.fields[1].start_bit = 8;
    map.fields[1].length = 8;
    map.fields[1].cls = SC_FIELD_SLOW;
    strcpy(map.fields[2].name, "crc");
    map.fields[2].start_bit = 16;
    map.fields[2].length = 8;
    map.fields[2].cls = SC_FIELD_CHECKSUM;
    map.has_checksum = true;
    map.checksum.kind = SC_CK_XOR;
    map.checksum_over_bytes = 2;

    uint8_t frame[3] = {0xA5, 0x42, 0x00};
    SC_CHECK(sc_fieldmap_resign(&map, frame, 3), "resign writes checksum");
    SC_CHECK_INT(frame[2], 0xA5 ^ 0x42);

    /* edit the data field, re-sign, checksum tracks it */
    sc_field_set(frame, 3, 8, 8, 0x10);
    sc_fieldmap_resign(&map, frame, 3);
    SC_CHECK_INT(frame[2], 0xA5 ^ 0x10);
}

static void test_roundtrip(void) {
    ScFieldMap m;
    memset(&m, 0, sizeof(m));
    strcpy(m.protocol, "acme weather"); /* space -> '_' encoding */
    m.nbits = 32;
    m.modulation = 1;
    m.n_fields = 2;
    strcpy(m.fields[0].name, "id");
    m.fields[0].start_bit = 0;
    m.fields[0].length = 8;
    m.fields[0].cls = SC_FIELD_STATIC;
    strcpy(m.fields[1].name, "temp");
    m.fields[1].start_bit = 8;
    m.fields[1].length = 12;
    m.fields[1].cls = SC_FIELD_SLOW;
    strcpy(m.fields[1].semantics, "tracks temp");
    m.has_checksum = true;
    m.checksum.kind = SC_CK_CRC8;
    m.checksum.poly = 0x07;
    m.checksum.init = 0x00;
    m.checksum_over_bytes = 3;
    m.user_confirmed = true;

    char buf[512];
    size_t len = sc_fieldmap_emit(&m, buf, sizeof(buf));
    SC_CHECK(len > 0, "emit produced text");

    ScFieldMap p;
    SC_CHECK(sc_fieldmap_parse(buf, len, &p), "parse ok");
    SC_CHECK_STR(p.protocol, "acme weather"); /* space survived the encode/decode */
    SC_CHECK_INT(p.nbits, 32);
    SC_CHECK_INT(p.modulation, 1);
    SC_CHECK_INT((int)p.n_fields, 2);
    SC_CHECK_STR(p.fields[1].name, "temp");
    SC_CHECK_INT(p.fields[1].start_bit, 8);
    SC_CHECK_INT(p.fields[1].length, 12);
    SC_CHECK_INT(p.fields[1].cls, SC_FIELD_SLOW);
    SC_CHECK_STR(p.fields[1].semantics, "tracks temp");
    SC_CHECK(p.has_checksum, "checksum parsed");
    SC_CHECK_INT(p.checksum.kind, SC_CK_CRC8);
    SC_CHECK_INT(p.checksum.poly, 0x07);
    SC_CHECK_INT(p.checksum_over_bytes, 3);
    SC_CHECK(p.user_confirmed, "source=user parsed");
}

static void test_from_diff(void) {
    /* same corpus as test_diff: byte0 static id, byte1 counter, byte2 slow, byte3 checksum */
    uint8_t frames[8][4];
    for(int i = 0; i < 8; i++) {
        frames[i][0] = 0xA5;
        frames[i][1] = (uint8_t)i;
        frames[i][2] = (i < 4) ? 0x10 : 0x11;
        frames[i][3] = sc_xor_bytes(frames[i], 3);
    }
    ScBitProfile prof[32];
    sc_diff_analyze(&frames[0][0], 8, 32, 4, prof);

    ScFieldMap m;
    sc_fieldmap_from_diff(prof, 32, "unknown_433", 0, &m);
    SC_CHECK_INT((int)m.n_fields, 4); /* one byte-granular segment per byte */
    SC_CHECK_INT(m.nbits, 32);
    SC_CHECK(!m.user_confirmed, "proposal is unconfirmed");
    SC_CHECK_INT(m.fields[0].cls, SC_FIELD_STATIC); /* id byte */
    SC_CHECK_INT(m.fields[1].cls, SC_FIELD_COUNTER); /* counter byte */
    SC_CHECK_STR(m.fields[1].name, "byte1");
    /* byte2 has a slow-varying low bit -> slow */
    SC_CHECK_INT(m.fields[2].cls, SC_FIELD_SLOW);
}

int main(void) {
    printf("test_fieldmap\n");
    test_bitfield();
    test_checksum_resign();
    test_roundtrip();
    test_from_diff();
    return sc_test_summary();
}
