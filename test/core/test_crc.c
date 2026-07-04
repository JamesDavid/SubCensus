/* test_crc.c — checksum family + discovery (System §7b). */
#include "sc_crc.h"
#include "sc_test.h"

/* Standard CRC check string "123456789". */
static const uint8_t CHECK[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
#define CHECK_N (sizeof(CHECK))

int main(void) {
    printf("test_crc\n");

    /* Known vectors. */
    SC_CHECK_INT(sc_reflect8(0x01), 0x80);
    SC_CHECK_INT(sc_reflect8(0x80), 0x01);
    SC_CHECK_INT(sc_reflect8(0xF0), 0x0F);

    /* CRC-8/SMBUS: poly 0x07, init 0x00, check = 0xF4. */
    SC_CHECK_INT(sc_crc8(CHECK, CHECK_N, 0x07, 0x00), 0xF4);
    /* CRC-8/MAXIM: reflected poly 0x31, init 0x00, check = 0xA1. */
    SC_CHECK_INT(sc_crc8le(CHECK, CHECK_N, 0x31, 0x00), 0xA1);

    /* XOR of 0x31..0x39 = 0x31; SUM = 0xDD. */
    SC_CHECK_INT(sc_xor_bytes(CHECK, CHECK_N), 0x31);
    SC_CHECK_INT(sc_add_bytes(CHECK, CHECK_N), 0xDD);

    /* LFSR digest is deterministic + input-sensitive. */
    uint8_t d1 = sc_lfsr_digest8(CHECK, CHECK_N, 0x98, 0x3e);
    uint8_t d2 = sc_lfsr_digest8(CHECK, CHECK_N, 0x98, 0x3e);
    SC_CHECK_INT(d1, d2);
    uint8_t flipped[CHECK_N];
    memcpy(flipped, CHECK, CHECK_N);
    flipped[0] ^= 0x01;
    SC_CHECK(sc_lfsr_digest8(flipped, CHECK_N, 0x98, 0x3e) != d1,
             "lfsr digest should change when input changes");

    /* Checksum discovery: build a frame whose trailing byte is a known CRC-8, then
     * confirm the search names it. */
    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t target = sc_crc8(payload, sizeof(payload), 0x07, 0x00);
    ScChecksumSpec spec;
    SC_CHECK(sc_checksum_search(payload, sizeof(payload), target, &spec),
             "should find a matching checksum algorithm");
    SC_CHECK_INT(spec.kind, SC_CK_CRC8);
    SC_CHECK_INT(spec.poly, 0x07);
    SC_CHECK_STR(sc_checksum_kind_str(spec.kind), "crc8");

    /* XOR discovery. */
    uint8_t xtarget = sc_xor_bytes(payload, sizeof(payload));
    SC_CHECK(sc_checksum_search(payload, sizeof(payload), xtarget, &spec), "xor found");
    SC_CHECK_INT(spec.kind, SC_CK_XOR);

    return sc_test_summary();
}
