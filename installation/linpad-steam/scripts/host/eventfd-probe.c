#define _GNU_SOURCE
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    int fd = eventfd(2, EFD_SEMAPHORE | EFD_NONBLOCK | EFD_CLOEXEC);
    printf("eventfd semaphore create: fd=%d errno=%d\n", fd, errno);
    if (fd < 0) return 1;
    int pollfd = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event event = {.events = EPOLLIN, .data.fd = fd};
    if (epoll_ctl(pollfd, EPOLL_CTL_ADD, fd, &event)) return 2;
    if (epoll_wait(pollfd, &event, 1, 0) != 1) return 3;
    for (int i = 0; i < 2; i++) {
        uint64_t value = 0;
        if (read(fd, &value, sizeof(value)) != sizeof(value) || value != 1) return 4;
    }
    uint64_t value = 0;
    if (read(fd, &value, sizeof(value)) != -1 || errno != EAGAIN) return 5;
    if (epoll_wait(pollfd, &event, 1, 0) != 0) return 6;
    close(pollfd); close(fd);
    fd = eventfd(2, EFD_NONBLOCK);
    if (fd < 0 || read(fd, &value, sizeof(value)) != sizeof(value) || value != 2) return 7;
    close(fd);
    puts("EVENTFD_SEMAPHORE_AND_COUNTER_OK");
    return 0;
}
