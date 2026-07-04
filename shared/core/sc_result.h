/* sc_result.h — shared result codes for the SubCensus C core. */
#ifndef SC_RESULT_H
#define SC_RESULT_H

typedef enum {
    SC_OK = 0,
    SC_ERR = -1,        /* malformed input / generic failure */
    SC_TRUNCATED = 1,   /* output buffer/capacity too small; partial result produced */
    SC_EMPTY = 2,       /* nothing to do (e.g. no timings / no samples) */
} ScResult;

#endif /* SC_RESULT_H */
