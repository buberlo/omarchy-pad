#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int failures;

static double milliseconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}

static void check(const char *name, int ok) {
    printf("%s %s errno=%d\n", name, ok ? "PASS" : "FAIL", errno);
    failures += !ok;
}

static void idle_poll(const char *name, int fd) {
    struct pollfd p = {.fd = fd, .events = 0};
    double start = milliseconds();
    int n = poll(&p, 1, 100);
    double elapsed = milliseconds() - start;
    printf("%s elapsed_ms=%.1f result=%d revents=%#x\n", name, elapsed, n, p.revents);
    check(name, n == 0 && p.revents == 0 && elapsed >= 75 && elapsed < 1000);
}

static void *close_later(void *arg) {
    usleep(30000);
    close(*(int *)arg);
    return NULL;
}

static void hangup_poll(const char *name, int reader, int writer) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, close_later, &writer))
        exit(90);
    struct pollfd p = {.fd = reader, .events = 0};
    int n = poll(&p, 1, 500);
    check(name, n == 1 && (p.revents & POLLHUP));
    pthread_join(thread, NULL);
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    alarm(5);
    char path[96];
    snprintf(path, sizeof(path), "/tmp/fifo-idle-poll-%ld", (long)getpid());
    if (argc == 2 && strcmp(argv[1], "--named-fifo-hup") == 0) {
        // Separate existing Darwin compatibility gap: host named FIFOs do not
        // report writer closure via poll/kqueue, even though read returns EOF.
        // This case remains failing and is not part of the idle-wakeup repair.
        if (mkfifo(path, 0600)) return 90;
        int reader = open(path, O_RDONLY | O_NONBLOCK);
        int writer = open(path, O_WRONLY | O_NONBLOCK);
        unlink(path);
        if (reader < 0 || writer < 0 || write(writer, "x", 1) != 1) return 90;
        hangup_poll("named FIFO HUP without requested events", reader, writer);
        close(reader);
        return failures;
    }
    if (mkfifo(path, 0600)) return 90;
    int fd = open(path, O_RDWR | O_NONBLOCK);
    unlink(path);
    if (fd < 0) return 90;
    idle_poll("empty named FIFO events=0 sleeps", fd);
    if (write(fd, "x", 1) != 1) return 90;
    idle_poll("queued named FIFO events=0 sleeps", fd);
    struct pollfd p = {.fd = fd, .events = POLLIN};
    check("FIFO requested readability", poll(&p, 1, 0) == 1 && (p.revents & POLLIN));
    p.revents = 0;
    check("FIFO readability remains level triggered", poll(&p, 1, 0) == 1 && (p.revents & POLLIN));
    close(fd);

    int pipefd[2];
    if (pipe(pipefd) || write(pipefd[1], "x", 1) != 1) return 90;
    idle_poll("queued pipe events=0 sleeps", pipefd[0]);
    hangup_poll("pipe HUP without requested events", pipefd[0], pipefd[1]);
    close(pipefd[0]);

    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) || write(sockets[1], "x", 1) != 1) return 90;
    idle_poll("queued socket events=0 sleeps", sockets[0]);
    hangup_poll("socket HUP without requested events", sockets[0], sockets[1]);
    close(sockets[0]);
    alarm(0);
    return failures;
}
