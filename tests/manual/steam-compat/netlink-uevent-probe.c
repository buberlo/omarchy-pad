#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

// LinPad's deliberately idle uevent endpoint; no device events are synthesized.
static int failures;
static void check(const char *name, int ok) {
    printf("%s %s errno=%d\n", name, ok ? "PASS" : "FAIL", errno);
    failures += !ok;
}
static int name_of(int fd, struct sockaddr_nl *name) {
    socklen_t len = sizeof(*name);
    memset(name, 0, sizeof(*name));
    return getsockname(fd, (struct sockaddr *)name, &len) == 0 && len == sizeof(*name)
        && name->nl_family == AF_NETLINK;
}
static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}
int main(void) {
    setbuf(stdout, NULL);
    alarm(10);
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC,
                    NETLINK_KOBJECT_UEVENT);
    check("uevent monitor socket", fd >= 0);
    if (fd < 0) return 1;
    check("nonblocking flag", (fcntl(fd, F_GETFL) & O_NONBLOCK) != 0);
    check("close-on-exec flag", (fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0);
    const int options[] = {SO_DOMAIN, SO_TYPE, SO_PROTOCOL};
    const int expected[] = {AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT};
    for (unsigned i = 0; i < sizeof(options)/sizeof(options[0]); i++) {
        int value = -1; socklen_t len = sizeof(value);
        check("socket identity", getsockopt(fd, SOL_SOCKET, options[i], &value, &len) == 0
              && value == expected[i]);
    }
    int yes = 1, value = 0; socklen_t len = sizeof(value);
    check("enable credentials", setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &yes, sizeof(yes)) == 0);
    check("credentials round trip", getsockopt(fd, SOL_SOCKET, SO_PASSCRED, &value, &len) == 0 && value == 1);
    check("short credential option rejected", setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &yes, 1) == -1 && errno == EINVAL);
    value = 16384;
    check("receive buffer", setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value)) == 0);
    struct sockaddr_nl local, bindaddr = {.nl_family = AF_NETLINK, .nl_groups = 2};
    check("unbound name", name_of(fd, &local) && local.nl_pid == 0 && local.nl_groups == 0);
    check("short bind rejected", bind(fd, (struct sockaddr *)&bindaddr, sizeof(bindaddr)-1) == -1 && errno == EINVAL);
    bindaddr.nl_family = AF_UNIX;
    check("wrong bind family rejected", bind(fd, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) == -1 && errno == EINVAL);
    bindaddr.nl_family = AF_NETLINK;
    check("bind udev group", bind(fd, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) == 0);
    check("bound name", name_of(fd, &local) && local.nl_pid != 0 && local.nl_groups == 2);
    unsigned first_port = local.nl_pid;
    int second = socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
    check("second monitor", second >= 0 && bind(second, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) == 0);
    check("unique automatic port", name_of(second, &local) && local.nl_pid != 0 && local.nl_pid != first_port);
    int third = socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
    bindaddr.nl_pid = first_port;
    check("duplicate explicit port rejected", third >= 0 && bind(third, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) == -1 && errno == EADDRINUSE);
    bindaddr.nl_groups = 1;
    check("same port rebind", bind(fd, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) == 0 && name_of(fd, &local) && local.nl_groups == 1);
    bindaddr.nl_groups = 0x80000002U;
    check("full group mask readback", bind(fd, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) == 0 && name_of(fd, &local) && local.nl_groups == 0x80000002U);
    bindaddr.nl_pid = 0;
    check("different port rebind rejected", bind(fd, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) == -1 && errno == EINVAL);
    struct pollfd p = {.fd = fd, .events = POLLIN};
    double start = now_ms(); int n = poll(&p, 1, 100); double elapsed = now_ms()-start;
    printf("idle elapsed_ms=%.1f result=%d revents=%#x\n", elapsed, n, p.revents);
    check("idle monitor sleeps", n == 0 && p.revents == 0 && elapsed >= 75 && elapsed < 2000);
    char buf[8192];
    check("empty receive", recv(fd, buf, sizeof(buf), MSG_DONTWAIT) == -1 && errno == EAGAIN);
    struct { struct nlmsghdr hdr; struct ifinfomsg body; } request = {
        .hdr = {.nlmsg_len = sizeof(request), .nlmsg_type = RTM_GETLINK,
                .nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP, .nlmsg_seq = 917},
        .body = {.ifi_family = AF_UNSPEC},
    };
    check("uevent send rejects route payload", send(fd, &request, sizeof(request), 0) == -1 && errno == EOPNOTSUPP);
    check("uevent write unsupported", write(fd, &request, sizeof(request)) == -1 && errno == EOPNOTSUPP);
    struct iovec iov = {.iov_base=&request, .iov_len=sizeof(request)};
    struct msghdr msg = {.msg_iov=&iov, .msg_iovlen=1};
    check("uevent sendmsg unsupported", sendmsg(fd, &msg, 0) == -1 && errno == EOPNOTSUPP);
    p.revents=0;
    check("no synthetic route reply", poll(&p, 1, 0) == 0 && p.revents == 0);
    struct sock_filter ins = BPF_STMT(BPF_RET|BPF_K, 0xffffffff);
    struct sock_fprog filter = {.len=1, .filter=&ins};
    check("unsupported BPF reports failure", setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &filter, sizeof(filter)) == -1);
    int group = 2;
    check("unsupported membership option reports failure", setsockopt(fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &group, sizeof(group)) == -1);
    int duplicate = dup(fd); close(fd); bindaddr.nl_pid=first_port;
    check("dup retains bound port", duplicate >= 0 && bind(third, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) == -1 && errno == EADDRINUSE);
    close(duplicate);
    check("last close releases port", bind(third, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) == 0);
    close(third); close(second);
    int route = socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK, NETLINK_ROUTE);
    check("route socket preserved", route >= 0);
    check("route request preserved", send(route, &request, sizeof(request), 0) == sizeof(request));
    p=(struct pollfd){.fd=route, .events=POLLIN};
    check("route reply readiness", poll(&p, 1, 500) == 1 && (p.revents & POLLIN));
    n=recv(route,buf,sizeof(buf),MSG_DONTWAIT);
    struct nlmsghdr header={0}; if(n >= (int)sizeof(header)) memcpy(&header,buf,sizeof(header));
    check("route reply remains netlink", n >= (int)sizeof(header) && header.nlmsg_seq == 917
          && (header.nlmsg_type == RTM_NEWLINK || header.nlmsg_type == NLMSG_DONE));
    close(route);
    alarm(0);
    printf("NETLINK_UEVENT_%s failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures != 0;
}
