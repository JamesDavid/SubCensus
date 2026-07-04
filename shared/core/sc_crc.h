/* sc_crc.h — checksum / CRC family for field-map discovery (System §7b tier 2).
 *
 * Standard textbook algorithms (CRC-8 MSB/LSB-first, XOR, sum, Galois LFSR digest)
 * matching the rtl_433 / reveng family by *behaviour*. Reference parameters, not code,
 * are taken from rtl_433 (GPL) per CLAUDE.md — these are clean reimplementations.
 *
 * Used to (a) re-sign edited frames (structured field editor, Zero §6) and
 * (b) name the algorithm on an unknown's trailing block (checksum discovery, §7b).
 */
#ifndef SC_CRC_H
#define SC_CRC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Reverse the bits of a byte. */
uint8_t sc_reflect8(uint8_t x);

/* CRC-8, MSB-first, no reflection. `poly` is the generator, `init` the seed. */
uint8_t sc_crc8(const uint8_t* msg, size_t n, uint8_t poly, uint8_t init);

/* CRC-8, LSB-first (reflected in/out). Matches rtl_433 crc8le / CRC-8/MAXIM family. */
uint8_t sc_crc8le(const uint8_t* msg, size_t n, uint8_t poly, uint8_t init);

/* XOR of all bytes. */
uint8_t sc_xor_bytes(const uint8_t* msg, size_t n);

/* Sum of all bytes, mod 256. */
uint8_t sc_add_bytes(const uint8_t* msg, size_t n);

/* Galois LFSR digest (rtl_433 lfsr_digest8): MSB-first, key rolled by `gen`. */
uint8_t sc_lfsr_digest8(const uint8_t* msg, size_t n, uint8_t gen, uint8_t key);

/* --- checksum discovery (System §7b) --- */

typedef enum {
    SC_CK_NONE = 0,
    SC_CK_XOR,
    SC_CK_SUM,
    SC_CK_CRC8, /* MSB-first, poly+init */
    SC_CK_CRC8LE, /* LSB-first, poly+init */
    SC_CK_LFSR8, /* Galois LFSR digest, gen+key */
} ScChecksumKind;

typedef struct {
    ScChecksumKind kind;
    uint8_t poly; /* CRC8 / CRC8LE */
    uint8_t init; /* CRC8 / CRC8LE */
    uint8_t gen; /* LFSR8 */
    uint8_t key; /* LFSR8 */
} ScChecksumSpec;

const char* sc_checksum_kind_str(ScChecksumKind k);

/* Brute the common family over data[0..n) against `target` (the observed check byte).
 * On the first match, fill `*out` and return true. Tries XOR, SUM, then CRC-8 MSB/LSB
 * over a set of common polynomials with init 0x00 and 0xFF. Returns false if none match. */
bool sc_checksum_search(const uint8_t* data, size_t n, uint8_t target, ScChecksumSpec* out);

#endif /* SC_CRC_H */
