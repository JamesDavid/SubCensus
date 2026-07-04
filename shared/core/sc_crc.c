#include "sc_crc.h"

uint8_t sc_reflect8(uint8_t x) {
    x = (uint8_t)((x & 0xF0) >> 4 | (x & 0x0F) << 4);
    x = (uint8_t)((x & 0xCC) >> 2 | (x & 0x33) << 2);
    x = (uint8_t)((x & 0xAA) >> 1 | (x & 0x55) << 1);
    return x;
}

uint8_t sc_crc8(const uint8_t* msg, size_t n, uint8_t poly, uint8_t init) {
    uint8_t r = init;
    for(size_t i = 0; i < n; i++) {
        r ^= msg[i];
        for(int b = 0; b < 8; b++) {
            if(r & 0x80)
                r = (uint8_t)((r << 1) ^ poly);
            else
                r = (uint8_t)(r << 1);
        }
    }
    return r;
}

uint8_t sc_crc8le(const uint8_t* msg, size_t n, uint8_t poly, uint8_t init) {
    uint8_t r = sc_reflect8(init);
    uint8_t p = sc_reflect8(poly);
    for(size_t i = 0; i < n; i++) {
        r ^= msg[i];
        for(int b = 0; b < 8; b++) {
            if(r & 0x01)
                r = (uint8_t)((r >> 1) ^ p);
            else
                r = (uint8_t)(r >> 1);
        }
    }
    return r;
}

uint8_t sc_xor_bytes(const uint8_t* msg, size_t n) {
    uint8_t r = 0;
    for(size_t i = 0; i < n; i++)
        r ^= msg[i];
    return r;
}

uint8_t sc_add_bytes(const uint8_t* msg, size_t n) {
    uint8_t r = 0;
    for(size_t i = 0; i < n; i++)
        r = (uint8_t)(r + msg[i]);
    return r;
}

uint8_t sc_lfsr_digest8(const uint8_t* msg, size_t n, uint8_t gen, uint8_t key) {
    uint8_t sum = 0;
    uint8_t k = key;
    for(size_t byte = 0; byte < n; byte++) {
        uint8_t data = msg[byte];
        for(int i = 7; i >= 0; i--) {
            if((data >> i) & 1) sum ^= k;
            if(k & 1)
                k = (uint8_t)((k >> 1) ^ gen);
            else
                k = (uint8_t)(k >> 1);
        }
    }
    return sum;
}

const char* sc_checksum_kind_str(ScChecksumKind k) {
    switch(k) {
    case SC_CK_XOR:
        return "xor";
    case SC_CK_SUM:
        return "sum";
    case SC_CK_CRC8:
        return "crc8";
    case SC_CK_CRC8LE:
        return "crc8le";
    case SC_CK_LFSR8:
        return "lfsr8";
    default:
        return "none";
    }
}

/* Common CRC-8 polynomials seen across ISM device decoders (reference metadata). */
static const uint8_t SC__CRC8_POLYS[] = {
    0x07, /* CRC-8 / SMBUS */
    0x31, /* CRC-8 / MAXIM (typically reflected) */
    0x1D, /* CRC-8 / variants */
    0x2F,
    0x9B,
    0xD5,
    0x8D,
    0x9C,
};
static const uint8_t SC__CRC8_INITS[] = {0x00, 0xFF};

bool sc_checksum_search(const uint8_t* data, size_t n, uint8_t target, ScChecksumSpec* out) {
    ScChecksumSpec spec = {0};

    if(sc_xor_bytes(data, n) == target) {
        spec.kind = SC_CK_XOR;
        if(out) *out = spec;
        return true;
    }
    if(sc_add_bytes(data, n) == target) {
        spec.kind = SC_CK_SUM;
        if(out) *out = spec;
        return true;
    }
    for(size_t pi = 0; pi < sizeof(SC__CRC8_POLYS); pi++) {
        for(size_t ii = 0; ii < sizeof(SC__CRC8_INITS); ii++) {
            uint8_t poly = SC__CRC8_POLYS[pi];
            uint8_t init = SC__CRC8_INITS[ii];
            if(sc_crc8(data, n, poly, init) == target) {
                spec.kind = SC_CK_CRC8;
                spec.poly = poly;
                spec.init = init;
                if(out) *out = spec;
                return true;
            }
            if(sc_crc8le(data, n, poly, init) == target) {
                spec.kind = SC_CK_CRC8LE;
                spec.poly = poly;
                spec.init = init;
                if(out) *out = spec;
                return true;
            }
        }
    }
    return false;
}
