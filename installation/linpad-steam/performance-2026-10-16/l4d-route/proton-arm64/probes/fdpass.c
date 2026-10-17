/* Wine passes its shared-session objects to client processes as file descriptors over Unix sockets
 * (SCM_RIGHTS), and Chromium/CEF does the same for its IPC. If fd passing -- or the mapping of the
 * received fd -- is broken, both fail exactly the way they do here: "Failed to get shared session
 * object for window class" and a webhelper backend that never starts. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002U
#endif

#define N 65536

static int send_fds(int sock, int *fds, int n) {
    char buf[1] = { 'x' };
    struct iovec io = { .iov_base = buf, .iov_len = 1 };
    char cbuf[CMSG_SPACE(sizeof(int) * 4)];
    memset(cbuf, 0, sizeof cbuf);
    struct msghdr msg = { 0 };
    msg.msg_iov = &io; msg.msg_iovlen = 1;
    msg.msg_control = cbuf; msg.msg_controllen = CMSG_SPACE(sizeof(int) * n);
    struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(sizeof(int) * n);
    memcpy(CMSG_DATA(c), fds, sizeof(int) * n);
    return sendmsg(sock, &msg, 0) < 0 ? -1 : 0;
}

static int recv_fds(int sock, int *fds, int n) {
    char buf[1];
    struct iovec io = { .iov_base = buf, .iov_len = 1 };
    char cbuf[CMSG_SPACE(sizeof(int) * 4)];
    struct msghdr msg = { 0 };
    msg.msg_iov = &io; msg.msg_iovlen = 1;
    msg.msg_control = cbuf; msg.msg_controllen = sizeof cbuf;
    if (recvmsg(sock, &msg, 0) < 0) return -1;
    struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
    if (!c || c->cmsg_type != SCM_RIGHTS) return -1;
    memcpy(fds, CMSG_DATA(c), sizeof(int) * n);
    return 0;
}

int main(void) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv)) { perror("socketpair"); return 1; }
    pid_t pid = fork();
    if (pid == 0) {
        close(sv[0]);
        int memfd = (int) syscall(SYS_memfd_create, "fdp", MFD_CLOEXEC);
        int filefd = open("/tmp/fdpass-probe-file", O_RDWR | O_CREAT | O_TRUNC, 0600);
        if (memfd < 0 || filefd < 0) { perror("create"); _exit(2); }
        if (ftruncate(memfd, N) || ftruncate(filefd, N)) perror("ftruncate");
        int fds[2] = { memfd, filefd };
        if (send_fds(sv[1], fds, 2)) { perror("sendmsg"); _exit(3); }
        char ack;
        if (read(sv[1], &ack, 1) != 1) _exit(4);
        /* the parent wrote through the received fd: is that visible in our own mapping? */
        char *m = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
        int seen = 0;
        if (m != MAP_FAILED) { for (int i = 0; i < N; i++) if (m[i] == 0x7E) seen++; }
        printf("child: parent's write through the passed fd visible: %d of %d\n", seen, N);
        _exit(seen == N ? 0 : 5);
    }
    close(sv[1]);
    int fds[2] = { -1, -1 };
    if (recv_fds(sv[0], fds, 2)) { printf("parent: recvmsg failed: %s\n", strerror(errno)); return 1; }
    printf("parent: received fds %d %d\n", fds[0], fds[1]);
    int bad = 0;
    char *m = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_SHARED, fds[0], 0);
    char *f = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_SHARED, fds[1], 0);
    if (m == MAP_FAILED) { printf("parent: mmap of the received memfd failed: %s\n", strerror(errno)); bad++; }
    if (f == MAP_FAILED) { printf("parent: mmap of the received file fd failed: %s\n", strerror(errno)); bad++; }
    if (!bad) { memset(m, 0x7E, N); memset(f, 0x3C, N); }
    char ack = 'a';
    if (write(sv[0], &ack, 1) != 1) printf("parent: ack failed\n");
    int status = 0;
    waitpid(pid, &status, 0);
    printf("parent: child exit=%d\n", WEXITSTATUS(status));
    printf("%s\n", (bad || WEXITSTATUS(status)) ? "FAIL: fd passing or mapping is broken" : "ok");
    return (bad || WEXITSTATUS(status)) ? 1 : 0;
}
