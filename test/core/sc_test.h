/* sc_test.h — tiny zero-dependency assert framework for shared/core native tests.
 *
 * Each test file is its own translation unit + executable with an int main() that
 * ends in `return sc_test_summary();`. The Python runner (test/core/run_tests.py)
 * compiles each test file together with the shared core sources using `zig cc`
 * (clang) and treats a non-zero exit as failure.
 *
 * RF boundary (CLAUDE.md / Debug §7): these tests prove *processing* on fixtures.
 * Nothing here touches a radio.
 */
#ifndef SC_TEST_H
#define SC_TEST_H

#include <math.h>
#include <stdio.h>
#include <string.h>

static int sc__checks = 0;
static int sc__fails = 0;

#define SC_CHECK(cond, msg)                                          \
    do {                                                             \
        sc__checks++;                                                \
        if(!(cond)) {                                                \
            sc__fails++;                                             \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        }                                                            \
    } while(0)

#define SC_CHECK_INT(actual, expected)                                                           \
    do {                                                                                         \
        sc__checks++;                                                                            \
        long _a = (long)(actual), _e = (long)(expected);                                         \
        if(_a != _e) {                                                                           \
            sc__fails++;                                                                         \
            printf(                                                                              \
                "  FAIL %s:%d: %s == %ld, expected %ld\n", __FILE__, __LINE__, #actual, _a, _e); \
        }                                                                                        \
    } while(0)

#define SC_CHECK_DBL(actual, expected, eps)                     \
    do {                                                        \
        sc__checks++;                                           \
        double _a = (double)(actual), _e = (double)(expected);  \
        if(fabs(_a - _e) > (eps)) {                             \
            sc__fails++;                                        \
            printf(                                             \
                "  FAIL %s:%d: %s == %g, expected %g (+-%g)\n", \
                __FILE__,                                       \
                __LINE__,                                       \
                #actual,                                        \
                _a,                                             \
                _e,                                             \
                (double)(eps));                                 \
        }                                                       \
    } while(0)

#define SC_CHECK_STR(actual, expected)                           \
    do {                                                         \
        sc__checks++;                                            \
        const char* _a = (actual);                               \
        const char* _e = (expected);                             \
        if(strcmp(_a, _e) != 0) {                                \
            sc__fails++;                                         \
            printf(                                              \
                "  FAIL %s:%d: %s == \"%s\", expected \"%s\"\n", \
                __FILE__,                                        \
                __LINE__,                                        \
                #actual,                                         \
                _a,                                              \
                _e);                                             \
        }                                                        \
    } while(0)

static inline int sc_test_summary(void) {
    printf("  %d checks, %d failures\n", sc__checks, sc__fails);
    return sc__fails == 0 ? 0 : 1;
}

#endif /* SC_TEST_H */
