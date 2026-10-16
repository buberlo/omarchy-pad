/* Writes a file instead of printing: a console PE's output needs a Windows console, which may not
 * exist here, so stdout is not a reliable sign of success. This is also built as 32-bit x86, the
 * architecture Left 4 Dead's engine uses. */
#include <stdio.h>
int main(void) {
    FILE *f = fopen("C:\\linpad-probe.txt", "w");
    if (!f) return 2;
    fprintf(f, "probe ran in a %d-bit Windows process\n", (int)(sizeof(void *) * 8));
    fclose(f);
    return 0;
}
