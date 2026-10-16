//
//  AppGroup.m
//  iSH
//
//  Created by Theodore Dubois on 2/28/20.
//

#import "AppGroup.h"
#import <Foundation/Foundation.h>
#import <mach-o/ldsyms.h>
#import <mach-o/loader.h>
#import <mach-o/getsect.h>
#import <dlfcn.h>
#import <fcntl.h>
#import <sys/file.h>
#import <unistd.h>

#define CSMAGIC_EMBEDDED_SIGNATURE 0xfade0cc0
#define CSMAGIC_EMBEDDED_ENTITLEMENTS 0xfade7171

struct cs_blob_index {
    uint32_t type;
    uint32_t offset;
};

struct cs_superblob {
    uint32_t magic;
    uint32_t length;
    uint32_t count;
    struct cs_blob_index index[];
};

struct cs_entitlements {
    uint32_t magic;
    uint32_t length;
    char entitlements[];
};

static NSDictionary *AppEntitlements(void) {
    static NSDictionary *entitlements;
    if (entitlements != nil)
        return entitlements;
    
    // Inspired by codesign.c in Darwin sources for Security.framework
    
    const struct mach_header_64 *header = &_mh_execute_header;
    
    // Simulator executables have fake entitlements in the code signature. The real entitlements can be found in an __entitlements section.
    size_t entitlements_size;
    char *entitlements_data = (char *) getsectiondata(header, "__TEXT", "__entitlements", &entitlements_size);
    if (entitlements_data != NULL) {
        NSData *data = [NSData dataWithBytesNoCopy:entitlements_data
                                            length:entitlements_size
                                      freeWhenDone:NO];
        return entitlements = [NSPropertyListSerialization propertyListWithData:data
                                                                        options:NSPropertyListImmutable
                                                                         format:nil
                                                                          error:nil];
    }
    
    // Find the LC_CODE_SIGNATURE
    struct load_command *lc = (void *) (header + 1);
    struct linkedit_data_command *cs_lc = NULL;
    for (uint32_t i = 0; i < header->ncmds; i++) {
        if (lc->cmd == LC_CODE_SIGNATURE) {
            cs_lc = (void *) lc;
            break;
        }
        lc = (void *) ((char *) lc + lc->cmdsize);
    }
    if (cs_lc == NULL)
        return nil;

    // Read the code signature off disk, as it's apparently not loaded into memory
    NSFileHandle *fileHandle = [NSFileHandle fileHandleForReadingFromURL:NSBundle.mainBundle.executableURL error:nil];
    if (fileHandle == nil)
        return nil;
    [fileHandle seekToFileOffset:cs_lc->dataoff];
    NSData *csData = [fileHandle readDataOfLength:cs_lc->datasize];
    [fileHandle closeFile];
    const struct cs_superblob *cs = csData.bytes;
    if (ntohl(cs->magic) != CSMAGIC_EMBEDDED_SIGNATURE)
        return nil;
    
    // Find the entitlements in the code signature
    NSData *entitlementsData = nil;
    for (uint32_t i = 0; i < ntohl(cs->count); i++) {
        struct cs_entitlements *ents = (void *) ((char *) cs + ntohl(cs->index[i].offset));
        if (ntohl(ents->magic) == CSMAGIC_EMBEDDED_ENTITLEMENTS) {
            entitlementsData = [NSData dataWithBytes:ents->entitlements
                                              length:ntohl(ents->length) - offsetof(struct cs_entitlements, entitlements)];
        }
    }
    if (entitlementsData == nil)
        return nil;
    
    return entitlements = [NSPropertyListSerialization propertyListWithData:entitlementsData
                                                                    options:NSPropertyListImmutable
                                                                     format:nil
                                                                      error:nil];
}

NSArray<NSString *> *CurrentAppGroups(void) {
    return AppEntitlements()[@"com.apple.security.application-groups"];
}

NSURL *ContainerURL(void) {
    NSString *appGroup = CurrentAppGroups().firstObject;
    NSURL *url = appGroup ? [NSFileManager.defaultManager containerURLForSecurityApplicationGroupIdentifier:appGroup] : nil;
#if TARGET_OS_SIMULATOR
    // Ad-hoc simulator builds can't carry the app-group entitlement.
    if (url == nil)
        url = [NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
#endif
    return url;
}

#pragma mark - One Linux system, one app

// Held for the lifetime of the process; the kernel releases it when the process dies,
// however it dies, which is why it is the right signal for "a kernel is running on this
// system". A suspended app still holds it, and that is deliberate: its guest is still
// running in the background.
static int sessionLockFD = -1;
static NSString *sessionLockMessage;

/// What another app wrote into the lock file when it took it: "<bundle id> <date>".
static NSString *SessionLockHolderDescription(NSString *path) {
    NSData *data = [NSData dataWithContentsOfFile:path];
    NSString *line = [[[NSString alloc] initWithData:data ?: [NSData data] encoding:NSUTF8StringEncoding]
                      stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSString *holder = line.length > 0 ? line : @"another LinPad app";
    NSString *bundleID = [holder componentsSeparatedByString:@" "].firstObject;
    // Names the user sees on the home screen; both apps are built from this source.
    if ([bundleID hasSuffix:@".steam"])
        return @"LinPad Steam";
    if ([bundleID isEqualToString:@"com.konradkern.linpad"])
        return @"Linux for iPad (LinPad)";
    return holder;
}

BOOL ISHSessionLockAcquire(NSString **reason) {
    if (sessionLockFD >= 0)
        return YES; // ours already
    // No container (simulator, or an install without the app group): never block booting.
    NSURL *roots = [ContainerURL() URLByAppendingPathComponent:@"roots" isDirectory:YES];
    if (roots == nil)
        return YES;
    [NSFileManager.defaultManager createDirectoryAtURL:roots withIntermediateDirectories:YES attributes:@{} error:nil];
    NSString *path = [[roots URLByAppendingPathComponent:@".session.lock"] path];
    int fd = open(path.fileSystemRepresentation, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0)
        return YES; // cannot even open the file: fail open, never block a boot
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        NSString *holder = SessionLockHolderDescription(path);
        sessionLockMessage = [NSString stringWithFormat:
            @"%@ is already using this Linux system. Close it in the app switcher (swipe it "
            @"away), then start this app again.", holder];
        if (reason != NULL)
            *reason = sessionLockMessage;
        close(fd);
        return NO;
    }
    ftruncate(fd, 0);
    NSString *record = [NSString stringWithFormat:@"%@ %@\n", NSBundle.mainBundle.bundleIdentifier ?: @"?",
                        [NSDate.date descriptionWithLocale:nil]];
    (void) [record writeToFile:path atomically:NO encoding:NSUTF8StringEncoding error:nil];
    sessionLockFD = fd;
    return YES;
}

NSString *ISHSessionLockMessage(void) {
    return sessionLockMessage;
}
