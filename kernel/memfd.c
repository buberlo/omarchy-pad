#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "kernel/calls.h"
#include "kernel/errno.h"
#include "kernel/fs.h"
#include "fs/fd.h"
#include "fs/real.h"

// memfd is backed by an unlinked host temp file, so guest MAP_SHARED mappings
// of it are real host shared mappings of fd->real_fd.

#define MFD_CLOEXEC_ 1
#define MFD_ALLOW_SEALING_ 2
#define MFD_NAME_MAX 249

// Sealing. Chromium/CEF and modern wine create their shared memory as
// memfd_create(MFD_ALLOW_SEALING) + ftruncate + F_ADD_SEALS, and treat any error as a failed
// allocation -- CEF's webhelper then never starts and wine cannot create the session objects it
// shares between its server and clients ("Failed to get shared session object for window class").
// virglrenderer/Venus seals its device-memory fds the same way. fcntl used to answer both commands
// with a constant, so callers saw EINVAL/0 instead of real seals.
#ifndef F_ADD_SEALS_
#define F_ADD_SEALS_ 1033
#endif
#ifndef F_GET_SEALS_
#define F_GET_SEALS_ 1034
#endif
#define F_SEAL_SEAL_ 0x0001
#define F_SEAL_SHRINK_ 0x0002
#define F_SEAL_GROW_ 0x0004
#define F_SEAL_WRITE_ 0x0008
#define F_SEAL_FUTURE_WRITE_ 0x0010
#define F_SEAL_ALL_ (F_SEAL_SEAL_ | F_SEAL_SHRINK_ | F_SEAL_GROW_ | F_SEAL_WRITE_ | F_SEAL_FUTURE_WRITE_)

// What a memfd carries in fd->data: its name for /proc/self/fd, its seals, and whether it was
// created with MFD_ALLOW_SEALING (Linux refuses F_ADD_SEALS without it).
struct memfd_info {
    char *name;
    unsigned int seals;
    bool allow_sealing;
};

static struct mount memfd_mount;

static struct memfd_info *memfd_of(struct fd *fd) {
    if (fd == NULL || fd->mount != &memfd_mount)
        return NULL;
    return (struct memfd_info *) fd->data;
}

bool memfd_is_memfd(struct fd *fd) {
    return memfd_of(fd) != NULL;
}

// F_ADD_SEALS: EINVAL for anything that is not a memfd or asks for unknown bits, EPERM when the
// memfd was created without MFD_ALLOW_SEALING or is already sealed.
int memfd_add_seals(struct fd *fd, unsigned int seals) {
    struct memfd_info *info = memfd_of(fd);
    if (info == NULL)
        return _EINVAL;
    if (seals & ~(unsigned int) F_SEAL_ALL_)
        return _EINVAL;
    if (!info->allow_sealing)
        return _EPERM;
    if (info->seals & F_SEAL_SEAL_)
        return _EPERM;
    info->seals |= seals;
    return 0;
}

// F_GET_SEALS
int memfd_get_seals(struct fd *fd) {
    struct memfd_info *info = memfd_of(fd);
    if (info == NULL)
        return _EINVAL;
    return (int) info->seals;
}

static int memfd_fstat(struct fd *fd, struct statbuf *stat) {
    int err = realfs_fstat(fd, stat);
    if (err < 0)
        return err;
    stat->mode = S_IFREG | (stat->mode & 07777);
    return 0;
}

static int memfd_getpath(struct fd *fd, char *buf) {
    struct memfd_info *info = memfd_of(fd);
    sprintf(buf, "/memfd:%s (deleted)", info != NULL ? info->name : "?");
    return 0;
}

static int memfd_close(struct fd *fd) {
    struct memfd_info *info = memfd_of(fd);
    if (info != NULL) {
        free(info->name);
        free(info);
        fd->data = NULL;
    }
    return 0;
}

static const struct fs_ops memfd_fs = {
    .magic = 0x01021994, // TMPFS_MAGIC
    .fstat = memfd_fstat,
    .fsetattr = realfs_fsetattr,
    .getpath = memfd_getpath,
    .close = memfd_close,
};

static struct mount memfd_mount = {
    .fs = &memfd_fs,
    .point = "",
};

static int memfd_host_file(void) {
    const char *dir = getenv("TMPDIR");
    if (dir == NULL || *dir == '\0')
        dir = "/tmp";
    char template[MAX_PATH];
    if (snprintf(template, sizeof(template), "%s/ish-memfd.XXXXXX", dir) >= (int) sizeof(template))
        return _ENAMETOOLONG;
    int real_fd = mkstemp(template);
    if (real_fd < 0)
        return errno_map();
    unlink(template);
    return real_fd;
}

fd_t sys_memfd_create(addr_t name_addr, uint_t flags) {
    char name[MFD_NAME_MAX + 1];
    if (user_read_string(name_addr, name, sizeof(name)))
        return _EFAULT;
    STRACE("memfd_create(\"%s\", %#x)", name, flags);
    if (flags & ~(MFD_CLOEXEC_ | MFD_ALLOW_SEALING_))
        return _EINVAL;

    int real_fd = memfd_host_file();
    if (real_fd < 0)
        return real_fd;
    struct fd *fd = fd_create(&realfs_fdops);
    if (fd == NULL) {
        close(real_fd);
        return _ENOMEM;
    }
    mount_retain(&memfd_mount);
    fd->mount = &memfd_mount;
    fd->real_fd = real_fd;
    fd->dir = NULL;
    fd->flags = O_RDWR_;
    struct memfd_info *info = calloc(1, sizeof(*info));
    if (info == NULL) {
        fd_close(fd);
        return _ENOMEM;
    }
    info->name = strdup(name);
    info->seals = 0;
    info->allow_sealing = (flags & MFD_ALLOW_SEALING_) != 0;
    fd->data = info;
    return f_install(fd, O_RDWR_ | (flags & MFD_CLOEXEC_ ? O_CLOEXEC_ : 0));
}
