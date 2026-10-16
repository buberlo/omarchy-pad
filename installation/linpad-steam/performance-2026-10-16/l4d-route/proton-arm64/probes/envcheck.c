#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void) {
    int local = 0;
    printf("ISH_HIGH_VA in my environment : %s\n", getenv("ISH_HIGH_VA") ? getenv("ISH_HIGH_VA") : "(absent)");
    printf("stack address                 : %p\n", (void *) &local);
    printf("high stack expected           : 0x7fff....\n");
    return 0;
}
