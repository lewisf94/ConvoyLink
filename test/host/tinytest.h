/**
 * tinytest.h — minimal host-test harness for ConvoyLink pure-C components.
 * No dependencies. Pattern:
 *
 *   TT_TEST(my_case) { TT_ASSERT_EQ(1 + 1, 2); }
 *   int main(void) { TT_RUN(my_case); return tt_summary("my_component"); }
 *
 * Each assert failure prints file:line and fails the case; the binary's
 * exit code is the number of failed cases (0 = success, what CI checks).
 */
#ifndef TINYTEST_H
#define TINYTEST_H

#include <math.h>
#include <stdio.h>
#include <string.h>

static int tt_cases = 0;
static int tt_failed_cases = 0;
static int tt_case_failed = 0;

#define TT_TEST(name) static void tt_##name(void)

#define TT_RUN(name)                                                       \
    do {                                                                   \
        tt_case_failed = 0;                                                \
        tt_cases++;                                                        \
        tt_##name();                                                       \
        if (tt_case_failed) {                                              \
            tt_failed_cases++;                                             \
            printf("[FAIL] %s\n", #name);                                  \
        } else {                                                           \
            printf("[ ok ] %s\n", #name);                                  \
        }                                                                  \
    } while (0)

#define TT_FAIL_(fmt, ...)                                                 \
    do {                                                                   \
        tt_case_failed = 1;                                                \
        printf("  assert failed %s:%d  " fmt "\n", __FILE__, __LINE__,     \
               __VA_ARGS__);                                               \
    } while (0)

#define TT_ASSERT(cond)                                                    \
    do {                                                                   \
        if (!(cond)) TT_FAIL_("%s", #cond);                                \
    } while (0)

#define TT_ASSERT_EQ(a, b) /* integer equality */                          \
    do {                                                                   \
        long long tt_a = (long long)(a), tt_b = (long long)(b);            \
        if (tt_a != tt_b)                                                  \
            TT_FAIL_("%s == %s  (%lld != %lld)", #a, #b, tt_a, tt_b);      \
    } while (0)

#define TT_ASSERT_NEAR(a, b, eps)                                          \
    do {                                                                   \
        double tt_a = (a), tt_b = (b);                                     \
        if (fabs(tt_a - tt_b) > (eps))                                     \
            TT_FAIL_("%s ~= %s  (%g vs %g, eps %g)", #a, #b, tt_a, tt_b,   \
                     (double)(eps));                                       \
    } while (0)

#define TT_ASSERT_MEMEQ(a, b, n)                                           \
    do {                                                                   \
        if (memcmp((a), (b), (n)) != 0)                                    \
            TT_FAIL_("memcmp(%s, %s, %s) != 0", #a, #b, #n);               \
    } while (0)

static inline int tt_summary(const char *suite)
{
    printf("%s: %d/%d cases passed\n", suite, tt_cases - tt_failed_cases,
           tt_cases);
    return tt_failed_cases;
}

#endif /* TINYTEST_H */
