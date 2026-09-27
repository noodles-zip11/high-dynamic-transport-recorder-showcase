#include <stdio.h>
int main(void)
{
    if (2 + 2 != 4) {
        fputs("native smoke: FAIL\n", stderr);
        return 1;
    }

    puts("native smoke: PASS");
    return 0;
}
