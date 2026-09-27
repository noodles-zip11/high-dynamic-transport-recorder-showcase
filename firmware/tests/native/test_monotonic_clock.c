#include <stdint.h>
#include <stdio.h>

#include "time/monotonic_clock.h"

static int expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }

    return 1;
}

int main(void)
{
    monotonic_clock_t clock = {0};
    uint64_t before_wrap = 0U;
    uint64_t at_wrap_edge = 0U;
    uint64_t after_wrap = 0U;
    uint64_t after_wrap_next = 0U;
    uint64_t repeated = 0U;
    monotonic_clock_t fractional_clock = {0};
    uint64_t fractional_us = 0U;

    if (!expect_true(!monotonic_clock_observe(NULL, 0U, 1000U, &before_wrap),
                     "a null clock must be rejected")
        || !expect_true(!monotonic_clock_observe(&clock, 0U, 0U, &before_wrap),
                        "a zero tick rate must be rejected")
        || !expect_true(!monotonic_clock_observe(&clock, 0U, 1000U, NULL),
                        "a null result must be rejected"))
    {
        return 1;
    }

    if (!expect_true(monotonic_clock_observe(&clock, UINT32_C(0xFFFFFFFE),
                                             1000U, &before_wrap),
                     "the first observation must succeed")
        || !expect_true(monotonic_clock_observe(&clock, UINT32_C(0xFFFFFFFF),
                                                1000U, &at_wrap_edge),
                        "the pre-wrap observation must succeed")
        || !expect_true(monotonic_clock_observe(&clock, 0U, 1000U,
                                                &after_wrap),
                        "the wrap observation must succeed")
        || !expect_true(monotonic_clock_observe(&clock, 1U, 1000U,
                                                &after_wrap_next),
                        "the post-wrap observation must succeed"))
    {
        return 1;
    }

    if (!expect_true(before_wrap < at_wrap_edge
                     && at_wrap_edge < after_wrap
                     && after_wrap < after_wrap_next,
                     "microseconds must remain strictly increasing across wrap")
        || !expect_true(after_wrap == UINT64_C(4294967296000),
                        "the extended wrap tick must retain its full epoch"))
    {
        return 1;
    }

    if (!expect_true(monotonic_clock_observe(&clock, 1U, 1000U, &repeated),
                     "a repeated observation must succeed")
        || !expect_true(repeated == after_wrap_next,
                        "a repeated tick must return a stable timestamp"))
    {
        return 1;
    }

    if (!expect_true(monotonic_clock_observe(&fractional_clock, 1U, 128U,
                                             &fractional_us),
                     "a non-1000 Hz observation must succeed")
        || !expect_true(fractional_us == UINT64_C(7812),
                        "fractional tick conversion must use integer microseconds"))
    {
        return 1;
    }

    puts("monotonic clock tests: PASS");
    return 0;
}
