#include <string.h>
#include "kernel/calls.h"

#define PRCTL_SET_PDEATHSIG_ 1
#define PRCTL_GET_PDEATHSIG_ 2
#define PRCTL_GET_DUMPABLE_ 3
#define PRCTL_SET_DUMPABLE_ 4
#define PRCTL_SET_KEEPCAPS_ 8
#define PRCTL_SET_NAME_ 15
#define PRCTL_CAPBSET_READ_ 23
#define PRCTL_CAPBSET_DROP_ 24
#define PRCTL_SET_SECUREBITS_ 28
#define PRCTL_SET_TIMERSLACK_ 29
#define PRCTL_GET_TIMERSLACK_ 30
#define PRCTL_SET_CHILD_SUBREAPER_ 36
#define PRCTL_GET_CHILD_SUBREAPER_ 37
#define PRCTL_SET_NO_NEW_PRIVS_ 38
#define PRCTL_GET_NO_NEW_PRIVS_ 39
#define PRCTL_GET_TID_ADDRESS_ 40
#define PRCTL_SET_THP_DISABLE_ 41
#define PRCTL_GET_THP_DISABLE_ 42

int_t sys_prctl(dword_t option, addr_t arg2, addr_t UNUSED(arg3), addr_t UNUSED(arg4), addr_t UNUSED(arg5)) {
    switch (option) {
        case PRCTL_SET_KEEPCAPS_:
            // stub
            return 0;
        case PRCTL_SET_NAME_: {
            char name[16];
            if (user_read_string(arg2, name, sizeof(name) - 1))
                return _EFAULT;
            name[sizeof(name) - 1] = '\0';
            STRACE("prctl(PRCTL_SET_NAME, \"%s\")", name);
            strcpy(current->comm, name);
            update_thread_name();
            return 0;
        }
        // The options below are the ones real programs reach for during startup. Answering them as
        // if they succeeded (and reporting harmless values to the getters) is what the rest of this
        // kernel already does for comparable calls; returning EINVAL instead makes callers abort.
        // Steam's reaper dies on "prctl() failed!" and its client asserts "Permission denied" in
        // tier0/threadtools during startup without them.
        case PRCTL_SET_PDEATHSIG_:
        case PRCTL_SET_DUMPABLE_:
        case PRCTL_SET_SECUREBITS_:
        case PRCTL_SET_TIMERSLACK_:
        case PRCTL_SET_CHILD_SUBREAPER_:
        case PRCTL_SET_NO_NEW_PRIVS_:
        case PRCTL_SET_THP_DISABLE_:
        case PRCTL_CAPBSET_DROP_:
            STRACE("prctl(%#x, %#lx) = 0", option, (unsigned long) arg2);
            return 0;
        case PRCTL_GET_PDEATHSIG_: {
            int_t sig = 0;
            if (user_put(arg2, sig))
                return _EFAULT;
            return 0;
        }
        case PRCTL_GET_DUMPABLE_:
            return 1;
        case PRCTL_GET_CHILD_SUBREAPER_: {
            int_t v = 0;
            if (user_put(arg2, v))
                return _EFAULT;
            return 0;
        }
        case PRCTL_GET_NO_NEW_PRIVS_:
            return 0;
        case PRCTL_GET_TIMERSLACK_:
            return 50000;
        case PRCTL_GET_THP_DISABLE_:
            return 0;
        case PRCTL_CAPBSET_READ_:
            // every capability is in the bounding set
            return arg2 < 64 ? 1 : _EINVAL;
        case PRCTL_GET_TID_ADDRESS_: {
            // report this thread's tid address as unknown but successful: callers only use it to
            // read their own tid back, and the value is not otherwise observed
            addr_t addr = 0;
            if (user_put(arg2, addr))
                return _EFAULT;
            return 0;
        }
        default:
            STRACE("prctl(%#x)", option);
            return _EINVAL;
    }
}

int_t sys_arch_prctl(int_t code, addr_t addr) {
    STRACE("arch_prctl(%#x, %#x)", code, addr);
    return _EINVAL;
}

#define REBOOT_MAGIC1 0xfee1dead
#define REBOOT_MAGIC2 672274793
#define REBOOT_MAGIC2A 85072278
#define REBOOT_MAGIC2B 369367448
#define REBOOT_MAGIC2C 537993216

#define REBOOT_CMD_CAD_OFF 0
#define REBOOT_CMD_CAD_ON 0x89abcdef

int_t sys_reboot(int_t magic, int_t magic2, int_t cmd) {
    STRACE("reboot(%#x, %d, %d)", magic, magic2, cmd);
    if (!superuser())
        return _EPERM;
    if (magic != (int) REBOOT_MAGIC1 ||
            (magic2 != REBOOT_MAGIC2 &&
             magic2 != REBOOT_MAGIC2A &&
             magic2 != REBOOT_MAGIC2B &&
             magic2 != REBOOT_MAGIC2C))
        return _EINVAL;

    switch (cmd) {
        case REBOOT_CMD_CAD_ON:
        case REBOOT_CMD_CAD_OFF:
            return 0;
        default:
            return _EPERM;
    }
}
