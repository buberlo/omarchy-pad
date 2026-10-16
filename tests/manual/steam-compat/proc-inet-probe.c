#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
static int lookup(const char *file, int fd, unsigned wanted_state, unsigned port) {
    struct stat st;
    if (fstat(fd, &st)) return 1;
    FILE *f = fopen(file, "r");
    if (!f) return 2;
    char line[512], local[80], remote[80];
    unsigned state, uid; unsigned long long inode;
    unsigned matches = 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%*u: %79s %79s %x %*s %*s %*s %u %*u %llu", local, remote, &state, &uid, &inode) != 5) continue;
        if (inode != (unsigned long long) st.st_ino) continue;
        const char *sep = strrchr(local, ':');
        if (!sep || strtoul(sep+1, NULL, 16) != port || state != wanted_state) {
            fprintf(stderr, "bad entry: %s", line); fclose(f); return 3;
        }
        matches++;
    }
    fclose(f);
    return matches != 1;
}
static int check(int family, int type) {
    int server = socket(family, type, 0), client = -1, accepted = -1;
    if (server < 0) return 10;
    struct sockaddr_storage addr = {};
    socklen_t len;
    if (family == AF_INET) {
        struct sockaddr_in *a = (void *)&addr;
        a->sin_family = family; a->sin_addr.s_addr = htonl(INADDR_LOOPBACK); len = sizeof(*a);
    } else {
        struct sockaddr_in6 *a = (void *)&addr;
        a->sin6_family = family; a->sin6_addr = in6addr_loopback; len = sizeof(*a);
    }
    if (bind(server, (void *)&addr, len) || getsockname(server, (void *)&addr, &len)) return 11;
    unsigned port = family == AF_INET ? ntohs(((struct sockaddr_in *)&addr)->sin_port) : ntohs(((struct sockaddr_in6 *)&addr)->sin6_port);
    const char *file = family == AF_INET ? (type == SOCK_STREAM ? "/proc/net/tcp" : "/proc/net/udp") : (type == SOCK_STREAM ? "/proc/net/tcp6" : "/proc/net/udp6");
    if (type == SOCK_STREAM) {
        if (listen(server, 1) || lookup(file, server, 10, port)) return 12;
        client = socket(family, type, 0);
        if (client < 0 || connect(client, (void *)&addr, len)) return 13;
        accepted = accept(server, NULL, NULL);
        if (accepted < 0 || lookup(file, accepted, 1, port)) return 14;
    } else if (lookup(file, server, 7, port)) return 15;
    int duplicate = dup(server);
    if (duplicate < 0) return 16;
    close(server);
    if (lookup(file, duplicate, type == SOCK_STREAM ? 10 : 7, port)) return 17;
    close(duplicate);
    if (accepted >= 0) close(accepted);
    if (client >= 0) close(client);
    printf("PASS %s loopback port=%u inode-match dup-lifetime\n", file, port);
    return 0;
}
int main(void) {
    setbuf(stdout, NULL);
    int families[] = {AF_INET, AF_INET6};
    int types[] = {SOCK_STREAM, SOCK_DGRAM};
    for (unsigned f = 0; f < 2; f++) for (unsigned t = 0; t < 2; t++) {
        int result = check(families[f], types[t]);
        if (result) { printf("FAIL family=%d type=%d result=%d\n",families[f],types[t],result); return result; }
    }
    puts("PROC_INET_OK"); return 0;
}
