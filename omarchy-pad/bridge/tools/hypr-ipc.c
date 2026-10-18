/* hypr-ipc: send one request to Hyprland's IPC socket and print the reply.
 *
 * hyprctl cannot always set SO_RCVTIMEO under iSH ("Couldn't set socket timeout (2)"),
 * so the gate scripts (and the project's bring-up) use this instead.  It is the same
 * request hyprctl sends: connect to $XDG_RUNTIME_DIR/hypr/<instance>/.socket.sock, write
 * the command, read the reply until the compositor closes the connection.
 *
 *   hypr-ipc cursorpos
 *   hypr-ipc monitors
 *   hypr-ipc --instance SIG keyword monitor HEADLESS-1, 1024x768@60, 0x0, 1
 *
 * Exit status is 0 when the command produced a reply, 1 otherwise.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr,
            "usage: hypr-ipc [--instance SIG] [--socket PATH] COMMAND [ARG...]\n"
            "  --instance SIG  Hyprland instance signature (default $HYPRLAND_INSTANCE_SIGNATURE,\n"
            "                  then the newest $XDG_RUNTIME_DIR/hypr/*/ with a .socket.sock)\n"
            "  --socket PATH   the IPC socket itself, overriding --instance\n");
}

static void find_instance(char *out, size_t out_size) {
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (!rt || !*rt)
        rt = "/run/user/0";
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/hypr", rt);
    DIR *d = opendir(dir);
    if (!d)
        return;
    time_t best_time = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char sock[900];
        snprintf(sock, sizeof(sock), "%s/%s/.socket.sock", dir, e->d_name);
        struct stat st;
        if (stat(sock, &st) != 0)
            continue;
        if (st.st_mtime >= best_time) {
            best_time = st.st_mtime;
            snprintf(out, out_size, "%s", e->d_name);
        }
    }
    closedir(d);
}

int main(int argc, char **argv) {
    const char *instance = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    const char *socket_path = NULL;
    int first = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--instance") == 0 && i + 1 < argc) {
            instance = argv[++i];
            first = i + 1;
        } else if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            socket_path = argv[++i];
            first = i + 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else {
            first = i;
            break;
        }
    }
    if (first >= argc) {
        usage();
        return 2;
    }

    char path[900];
    if (socket_path) {
        snprintf(path, sizeof(path), "%.800s", socket_path);
    } else {
        if (!instance || !*instance) {
            static char found[256];
            find_instance(found, sizeof(found));
            instance = found;
        }
        const char *rt = getenv("XDG_RUNTIME_DIR");
        if (!rt || !*rt)
            rt = "/run/user/0";
        if (!instance || !*instance) {
            fprintf(stderr, "hypr-ipc: no Hyprland instance found under %s/hypr\n", rt);
            return 1;
        }
        snprintf(path, sizeof(path), "%.400s/hypr/%.200s/.socket.sock", rt, instance);
    }

    char command[2048];
    size_t len = 0;
    for (int i = first; i < argc; i++)
        len += snprintf(command + len, sizeof(command) - len, "%s%s", i == first ? "" : " ", argv[i]);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("hypr-ipc: socket");
        return 1;
    }
    struct timeval tv = {.tv_sec = 10, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%.107s", path);
    if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        fprintf(stderr, "hypr-ipc: cannot connect to %s: %s\n", path, strerror(errno));
        close(fd);
        return 1;
    }
    if (write(fd, command, strlen(command)) < 0) {
        fprintf(stderr, "hypr-ipc: write: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    char buffer[4096];
    ssize_t n;
    long total = 0;
    while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
        if (fwrite(buffer, 1, (size_t) n, stdout) != (size_t) n)
            break;
        total += n;
    }
    close(fd);
    if (total == 0 || buffer[0] != '\n')
        fputc('\n', stdout);
    return total > 0 ? 0 : 1;
}
