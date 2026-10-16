//
//  AppGroup.h
//  iSH
//
//  Created by Theodore Dubois on 2/28/20.
//

#import <Foundation/Foundation.h>

NSURL *ContainerURL(void);

/// Takes the one "this app is running the Linux system" lock for this process and holds it
/// until the process exits (the kernel drops it then, even after a crash).
///
/// LinPad and LinPad Steam are two apps that share one Linux system through the app group
/// container (`roots/`), and the fakefs is not made for two kernels at once: two apps
/// mounting it would corrupt the system. Both apps call this before `mount_root`.
///
/// Returns YES when this process may boot: the lock was taken, it was already ours, or
/// locking is not possible at all (no app group, an unopenable file). It only returns NO
/// when another process demonstrably holds the lock. `reason`, when given, is set to a
/// sentence naming the holder and what to do about it.
/// `rootName` is the root this app boots ("default" for the desktop, "arch" for the game app); the
/// lock is per root, so two apps on two different roots may run at once. One root, one app still holds.
BOOL ISHSessionLockAcquire(NSString *rootName, NSString **reason);

/// Why the last ISHSessionLockAcquire said no, or nil. Set before `boot` returns so the
/// Swift side can show the busy screen instead of a generic boot failure.
NSString *ISHSessionLockMessage(void);
