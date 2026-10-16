/* glibc trims its heap with brk(smaller) and grows it again later. If the emulator unmaps more than
 * asked -- or the wrong part -- live heap data disappears, which is exactly the "corrupted size vs.
 * prev_size" glibc then reports. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MB (1u << 20)

int main(void) {
    char *base = sbrk(0);
    if (base == (void *) -1) { perror("sbrk"); return 1; }
    if (brk(base + 4 * MB) != 0) { perror("grow"); return 1; }
    for (size_t i = 0; i < 4 * MB; i++) base[i] = (char) (i * 7 + 3);
    printf("wrote %u MB at %p\n", 4u, (void *) base);

    /* trim: give back the top MB, then take it again -- what glibc's free() does all the time */
    if (brk(base + 3 * MB) != 0) { perror("trim"); return 1; }
    if (brk(base + 4 * MB) != 0) { perror("regrow"); return 1; }
    printf("trimmed 1 MB and grew back\n");

    size_t bad_low = 0, bad_high = 0;
    for (size_t i = 0; i < 3 * MB; i++)
        if (base[i] != (char) (i * 7 + 3)) bad_low++;
    for (size_t i = 3 * MB; i < 4 * MB; i++)
        if (base[i] != (char) (i * 7 + 3)) bad_high++;
    printf("bytes changed below the trim point : %zu (must be 0)\n", bad_low);
    printf("bytes changed above the trim point : %zu (fresh pages there are fine)\n", bad_high);
    printf("%s\n", bad_low ? "FAIL: live heap data was destroyed by brk shrink" : "ok");
    return bad_low ? 1 : 0;
}
