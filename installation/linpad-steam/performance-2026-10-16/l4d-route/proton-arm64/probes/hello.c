#include <stdio.h>
int main(void) {
    printf("l4d-probe: hello from %d-bit %s\n", (int)(sizeof(void*) * 8),
#ifdef _WIN32
           "windows"
#else
           "linux"
#endif
    );
    fflush(stdout);
    return 0;
}
