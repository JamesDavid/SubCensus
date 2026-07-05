/* test_slice.c — RAW timing <-> bit-frame round-trip (System §7b editor support). */
#include "sc_slice.h"
#include "sc_test.h"

int main(void) {
    printf("test_slice\n");

    /* a small OOK-ish pulse train at unit=100us:
     *   +100 (1), -200 (00), +100 (1), -100 (0), +300 (111)  => bits 1 00 1 0 111 = 1001 0111 */
    int32_t timings[] = {100, -200, 100, -100, 300};
    uint8_t frame[4];
    size_t nbits = sc_slice_bits(timings, 5, 100, frame, sizeof(frame));
    SC_CHECK_INT((int)nbits, 8);
    SC_CHECK_INT(frame[0], 0x97); /* 1001 0111 */

    /* encode is the exact inverse (coalescing same-level runs) */
    int32_t out[16];
    size_t nt = sc_slice_encode(frame, nbits, 100, out, 16);
    /* runs: 1 -> +100 ; 00 -> -200 ; 1 -> +100 ; 0 -> -100 ; 111 -> +300 */
    SC_CHECK_INT((int)nt, 5);
    SC_CHECK_INT(out[0], 100);
    SC_CHECK_INT(out[1], -200);
    SC_CHECK_INT(out[2], 100);
    SC_CHECK_INT(out[3], -100);
    SC_CHECK_INT(out[4], 300);

    /* slice(encode(bits)) == bits */
    uint8_t frame2[4];
    size_t nbits2 = sc_slice_bits(out, nt, 100, frame2, sizeof(frame2));
    SC_CHECK_INT((int)nbits2, (int)nbits);
    SC_CHECK_INT(frame2[0], frame[0]);

    /* rounding: a 250us mark at unit=100 rounds to 3 units (nearest), 240 -> 2 */
    int32_t t2[] = {250};
    uint8_t f2[2];
    SC_CHECK_INT((int)sc_slice_bits(t2, 1, 100, f2, sizeof(f2)), 3);
    int32_t t3[] = {240};
    SC_CHECK_INT((int)sc_slice_bits(t3, 1, 100, f2, sizeof(f2)), 2);

    return sc_test_summary();
}
