/* sc_util.h — tiny shared helpers for the SubCensus C core.
 *
 * Kept in one place so unity builds (the Zero FAP compiles the core as a single TU via
 * zero/subcensus_core.c) don't hit duplicate file-scope `static` definitions.
 */
#ifndef SC_UTIL_H
#define SC_UTIL_H

#include <stdint.h>

static inline int32_t sc_iabs32(int32_t v) {
    return v < 0 ? -v : v;
}

#endif /* SC_UTIL_H */
