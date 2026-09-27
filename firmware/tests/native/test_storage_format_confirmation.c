#include <stdio.h>

#include "storage_format_confirmation.h"

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "storage format confirmation: %s\n", message);
        return 0;
    }

    return 1;
}

int main(void)
{
    storage_format_confirmation_t confirmation = {0};

    storage_format_confirmation_init(&confirmation);
    if (!expect(!storage_format_confirmation_confirm(&confirmation, 100U),
                "a confirmation without a first request must be rejected"))
    {
        return 1;
    }

    storage_format_confirmation_request(&confirmation, 100U);
    if (!expect(storage_format_confirmation_confirm(
                    &confirmation, 100U + STORAGE_FORMAT_CONFIRM_WINDOW_TICKS - 1U),
                "a second command inside the window must be accepted")
        || !expect(!storage_format_confirmation_confirm(
                       &confirmation, 100U + STORAGE_FORMAT_CONFIRM_WINDOW_TICKS - 1U),
                   "an accepted confirmation must be single use"))
    {
        return 1;
    }

    storage_format_confirmation_request(&confirmation, 100U);
    if (!expect(!storage_format_confirmation_confirm(
                    &confirmation, 100U + STORAGE_FORMAT_CONFIRM_WINDOW_TICKS),
                "an expired request must be rejected"))
    {
        return 1;
    }

    puts("storage format confirmation: PASS");
    return 0;
}
