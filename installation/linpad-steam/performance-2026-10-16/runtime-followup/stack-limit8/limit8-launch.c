#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>
/* Local comparison with Steam launcher's ulimit -s 8192. */
int main(int argc, char **argv) {
    if (argc != 4) return 64;
    struct rlimit limit = {.rlim_cur = 8 * 1024 * 1024, .rlim_max = 8 * 1024 * 1024};
    if (setrlimit(RLIMIT_STACK, &limit)) { perror("setrlimit"); return 1; }
    char *args[] = {"/opt/mono-init/launch", argv[1], argv[2], argv[3], NULL};
    execv(args[0], args);
    perror("execv");
    return 1;
}
