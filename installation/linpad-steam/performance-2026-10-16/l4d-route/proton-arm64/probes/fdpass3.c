/* Wine's server socket is a bound Unix socket in the filesystem
 * (/tmp/.wine-<uid>/server-<dev>-<inode>/socket), not a socketpair. It sends the shared-session
 * file descriptor over that socket with SCM_RIGHTS. If fd passing works over socketpair but not
 * over a bound/connected/accepted socket -- as some sandboxes do -- wine's client maps an empty copy
 * of the session and reports "Session object id doesn't match expected id". */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002U
#endif
#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#define F_SEAL_SEAL 0x0001
#define F_SEAL_SHRINK 0x0002
#define F_SEAL_GROW 0x0004
#endif

#define N 65536
#define SOCK_PATH "/tmp/fdpass2.sock"

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
    unlink(SOCK_PATH);
    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0) { perror("socket"); return 1; }
    struct sockaddr_un addr = { 0 };
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);
    if (bind(listener, (struct sockaddr *) &addr, sizeof addr)) { perror("bind"); return 1; }
    if (listen(listener, 4)) { perror("listen"); return 1; }

    pid_t pid = fork();
    if (pid == 0) {
        /* server side: accept, create a section, send it */
        int conn = accept(listener, NULL, NULL);
        if (conn < 0) { perror("accept"); _exit(2); }
        int memfd = (int) syscall(SYS_memfd_create, "session", MFD_ALLOW_SEALING);
        if (memfd < 0) { perror("memfd"); _exit(3); }
        if (ftruncate(memfd, N)) perror("ftruncate");
        char *m = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
        if (m == MAP_FAILED) { perror("mmap"); _exit(4); }
        /* the section carries an id the client must find again */
        memset(m, 0, N);
        *(unsigned long *) m = 0x1122334455667788UL;
        if (fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) != 0)
            printf("server: sealing failed: %s\n", strerror(errno));
        if (send_fds(conn, &memfd, 1)) { perror("sendmsg"); _exit(5); }
        sleep(2);
        /* prove sharing: a write here must be visible in the client's mapping */
        *(unsigned long *) (m + 8) = 0xAABBCCDDEEFF0011UL;
        char ack;
        read(conn, &ack, 1);
        _exit(0);
    }

    int conn = socket(AF_UNIX, SOCK_STREAM, 0);
    if (conn < 0) { perror("client socket"); return 1; }
    if (connect(conn, (struct sockaddr *) &addr, sizeof addr)) { perror("connect"); return 1; }
    int fd = -1;
    if (recv_fds(conn, &fd, 1)) { printf("client: no fd received: %s\n", strerror(errno)); return 1; }
    printf("client: received fd %d\n", fd);
    char *m = mmap(NULL, N, PROT_READ, MAP_SHARED, fd, 0);  /* read-only, as wine maps its session */
    if (m == MAP_FAILED) { printf("client: mmap failed: %s\n", strerror(errno)); return 1; }
    unsigned long id = *(unsigned long *) m;
    printf("client: object id read from the shared section: 0x%lx (want 0x1122334455667788)\n", id);
    printf("client: id read again immediately: 0x%lx\n", *(unsigned long *) m);
    sleep(1);
    printf("client: id before the server writes: 0x%lx\n", *(unsigned long *) m);
    sleep(3);
    unsigned long second = *(unsigned long *) (m + 8);
    printf("client: after the server's later write: 0x%lx (want 0xaabbccddeeff0011)\n", second);
    char ack = 'a';
    write(conn, &ack, 1);
    int status = 0;
    waitpid(pid, &status, 0);
    int ok = (id == 0x1122334455667788UL) && (second == 0xAABBCCDDEEFF0011UL);
    printf("%s\n", ok ? "ok" : "FAIL: the section is not really shared");
    unlink(SOCK_PATH);
    return ok ? 0 : 1;
}
