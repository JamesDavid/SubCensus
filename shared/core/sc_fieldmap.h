/* sc_fieldmap.h — per-protocol field-map: labeled bit fields + checksum (System §6, §7b).
 *
 * A field map describes a frame's structure: which bit ranges are id/address/data/counter/
 * checksum, plus the checksum algorithm (sc_crc) so an edited frame can be re-signed. It backs
 * the Zero/Esp structured field editor (Zero §6) and the field-map discovery loop (§7b): the
 * differential analysis (sc_diff) seeds a proposal, the user labels segments, and a confirmed
 * structure is persisted as a `signatures/field_maps/<protocol>.fmap` entry. NEVER auto-committed.
 *
 * Bit indexing is MSB-first: bit i lives in byte i/8 at mask (0x80 >> (i%8)) — the same
 * convention as sc_diff, so a diff profile maps directly onto fields.
 *
 * On-disk `.fmap` text format (host-parseable; emitted/parsed on-device with no JSON lib):
 *   SC_FIELDMAP v1
 *   protocol <name>
 *   modulation <0=OOK|1=2FSK>
 *   nbits <N>
 *   field <name> <start_bit> <length> <class> <semantics>
 *   ...
 *   checksum <kind> <poly> <init> <gen> <key> <over_bytes>
 *   source <user|proposed>
 * Tokens are space-separated; <name>/<semantics> use '_' for spaces and "-" for empty.
 */
#ifndef SC_FIELDMAP_H
#define SC_FIELDMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sc_crc.h"
#include "sc_diff.h"

#define SC_FIELDMAP_MAX_FIELDS 16
#define SC_FIELD_NAME_LEN      16
#define SC_FIELD_SEM_LEN       24
#define SC_FIELDMAP_PROTO_LEN  24

typedef enum {
    SC_FIELD_STATIC = 0, /* id / address / preamble (never changes) */
    SC_FIELD_SLOW = 1, /* sensor value (slow-varying) */
    SC_FIELD_COUNTER = 2, /* sequence / counter (every-frame) */
    SC_FIELD_CHECKSUM = 3, /* trailing check block */
    SC_FIELD_DATA = 4, /* generic payload / unclassified */
} ScFieldClass;

typedef struct {
    char name[SC_FIELD_NAME_LEN];
    uint16_t start_bit;
    uint16_t length; /* bits (1..32 for get/set) */
    uint8_t cls; /* ScFieldClass */
    char semantics[SC_FIELD_SEM_LEN]; /* optional ("" if none) */
} ScField;

typedef struct {
    char protocol[SC_FIELDMAP_PROTO_LEN];
    uint16_t nbits; /* total frame length in bits */
    uint8_t modulation; /* 0 = OOK, 1 = 2-FSK */
    size_t n_fields;
    ScField fields[SC_FIELDMAP_MAX_FIELDS];
    bool has_checksum;
    ScChecksumSpec checksum;
    uint16_t checksum_over_bytes; /* bytes covered by the checksum */
    bool user_confirmed; /* true = user-confirmed, false = proposed */
} ScFieldMap;

const char* sc_field_class_str(uint8_t cls);
uint8_t sc_field_class_from_str(const char* s);

/* Extract an MSB-first bit field (length 1..32) from frame[nbytes]. Reads clamp at nbytes. */
uint32_t sc_field_get(const uint8_t* frame, size_t nbytes, size_t start_bit, size_t length);

/* Write `value` (low `length` bits) into an MSB-first bit field of frame[nbytes]. */
void sc_field_set(uint8_t* frame, size_t nbytes, size_t start_bit, size_t length, uint32_t value);

/* Compute a checksum spec over data[0..n) (dispatches to the sc_crc family). */
uint8_t sc_checksum_compute(const ScChecksumSpec* spec, const uint8_t* data, size_t n);

/* Recompute the checksum and write it into the frame's checksum field (in place).
 * Uses map->checksum over the first map->checksum_over_bytes bytes, writing into the field
 * flagged SC_FIELD_CHECKSUM. Returns false if the map has no checksum/checksum field. */
bool sc_fieldmap_resign(const ScFieldMap* map, uint8_t* frame, size_t nbytes);

/* Parse a `.fmap` text buffer (len bytes) into *out. Returns true on success. */
bool sc_fieldmap_parse(const char* text, size_t len, ScFieldMap* out);

/* Emit *map to `.fmap` text in out[cap] (NUL-terminated). Returns bytes written (excl. NUL). */
size_t sc_fieldmap_emit(const ScFieldMap* map, char* out, size_t cap);

/* Seed a proposed field map from a differential bit profile (System §7b layer 1): coalesce
 * adjacent same-class bits into byte-granular segments, tag a trailing static/checksum block.
 * `profiles` has `nbits` entries (from sc_diff_analyze). Writes into *out (proposed, unconfirmed). */
void sc_fieldmap_from_diff(
    const ScBitProfile* profiles,
    size_t nbits,
    const char* protocol,
    uint8_t modulation,
    ScFieldMap* out);

#endif /* SC_FIELDMAP_H */
