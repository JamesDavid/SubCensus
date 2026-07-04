/* test_diff.c — differential bitfield analysis (System §7b tier 1). */
#include "sc_crc.h"
#include "sc_diff.h"
#include "sc_test.h"

/* 8 frames, 4 bytes each:
 *   byte0 = 0xA5           (static address)
 *   byte1 = frame counter  (0..7)
 *   byte2 = 0x10, flips to 0x11 halfway (slow-varying sensor value, low bit only)
 *   byte3 = XOR(b0,b1,b2)  (checksum; changes when payload changes)  */
static void build(uint8_t frames[8][4]) {
    for(int i = 0; i < 8; i++) {
        frames[i][0] = 0xA5;
        frames[i][1] = (uint8_t)i;
        frames[i][2] = (i < 4) ? 0x10 : 0x11;
        frames[i][3] = sc_xor_bytes(frames[i], 3);
    }
}

int main(void) {
    printf("test_diff\n");

    uint8_t frames[8][4];
    build(frames);

    ScBitProfile prof[32];
    sc_diff_analyze(&frames[0][0], 8, 32, 4, prof);

    /* byte0 (bits 0..7): all static */
    for(int b = 0; b < 8; b++) {
        SC_CHECK_INT(prof[b].cls, SC_BIT_STATIC);
        SC_CHECK_INT(prof[b].distinct, 1);
        SC_CHECK_DBL(prof[b].change_rate, 0.0, 0.001);
    }

    /* byte1 LSB (bit 15): counter -> changes every frame */
    SC_CHECK_INT(prof[15].cls, SC_BIT_COUNTER);
    SC_CHECK_DBL(prof[15].change_rate, 1.0, 0.001);
    SC_CHECK_DBL(prof[15].entropy, 1.0, 0.01); /* balanced 0/1 -> 1 bit */

    /* byte2 LSB (bit 23): slow-varying (flips once) */
    SC_CHECK_INT(prof[23].cls, SC_BIT_SLOW);
    SC_CHECK_INT(prof[23].distinct, 2);
    SC_CHECK(
        prof[23].change_rate > 0.0 && prof[23].change_rate < 0.8,
        "slow bit changes but not every frame");

    /* byte2 high bits (16..22) never change -> static */
    SC_CHECK_INT(prof[16].cls, SC_BIT_STATIC);

    /* checksum byte (24..31): non-static, at least one bit varies */
    int cksum_varies = 0;
    for(int b = 24; b < 32; b++)
        if(prof[b].distinct == 2) cksum_varies = 1;
    SC_CHECK(cksum_varies, "checksum block varies with payload");

    /* And the checksum algorithm itself is nameable (sc_crc tier 2) on any frame. */
    ScChecksumSpec spec;
    SC_CHECK(sc_checksum_search(frames[3], 3, frames[3][3], &spec), "checksum named");
    SC_CHECK_INT(spec.kind, SC_CK_XOR);

    return sc_test_summary();
}
