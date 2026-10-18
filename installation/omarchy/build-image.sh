#!/bin/sh
# Builds the Omarchy Pad rootfs: a stock Arch Linux ARM userland with Omarchy's own
# edge aarch64 packages on top, exported as a tar the iSH fakefs importer can consume.
#
#   sh installation/omarchy/build-image.sh [output.tar.gz]
#
# Everything runs in a throwaway Arch Linux ARM container (`menci/archlinuxarm`, the
# only usable arm64 Arch image) so the guest tree is assembled with real pacman and
# real package signatures. The result is a gzipped tar whose first member is "./" and
# whose /dev, /proc and /sys are empty: the emulator creates its own device nodes and
# mounts its own /proc and /sys, and the image's own nodes would shadow them.
#
# Env:
#   DOCKER_CONTEXT   docker context (default colima-linpad-fex)
#   BASE_IMAGE       builder image (default menci/archlinuxarm:latest)
#   WORK             work directory for logs and lists (default <repo>/work/omarchy-pad)
#   ROOT_VERSION     version stamp (default UTC yyyymmddHHMM)
#   PACKAGES_FILE    package manifest (default installation/omarchy/packages.txt)
#   EXTRA_PACKAGES   space-separated additions, e.g. EXTRA_PACKAGES="gdb strace"
#
# This script is the iSH-side adaptation of two upstream designs:
#   - buberlo/linpad-ipad-steam installation/steam-distro/build-arch-app-image.sh
#     (container -> pacman -> tar, leading ./, empty /dev /proc /sys, version stamp)
#   - omacom/try-omarchy guest/factory (Omarchy repositories, keyring, package-by-
#     package install so one unavailable name does not abort the whole transaction)
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
WORK=${WORK:-$ROOT/work/omarchy-pad}
DOCKER_CONTEXT=${DOCKER_CONTEXT:-colima-linpad-fex}
BASE_IMAGE=${BASE_IMAGE:-menci/archlinuxarm:latest}
PACKAGES_FILE=${PACKAGES_FILE:-$HERE/packages.txt}
EXTRA_PACKAGES=${EXTRA_PACKAGES:-}
ROOT_VERSION=${ROOT_VERSION:-$(date -u +%Y%m%d%H%M)}
OUT=${1:-$WORK/omarchy-aarch64-pad.tar.gz}

D="docker --context $DOCKER_CONTEXT"
mkdir -p "$WORK" "$(dirname "$OUT")"

say() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }

packages=$(grep -vE '^[[:space:]]*(#|$)' "$PACKAGES_FILE" | tr '\n' ' ')
[ -n "$EXTRA_PACKAGES" ] && packages="$packages $EXTRA_PACKAGES"

say "builder $BASE_IMAGE (context $DOCKER_CONTEXT), version $ROOT_VERSION"
id=$($D create "$BASE_IMAGE" sh -c 'while :; do sleep 3600; done')
cleanup() { $D rm -f "$id" >/dev/null 2>&1 || true; }
trap cleanup EXIT INT TERM
$D start "$id" >/dev/null

say "repository configuration and keyrings"
# The Omarchy keyring is imported from the reviewed files this repository carries, with no
# keyserver lookup (same digests as try-omarchy records in guest/README.md). They are staged
# in /tmp for pacman-key; the permanent files under /usr/share/pacman/keyrings belong to the
# omarchy-keyring package, and leaving our copies there makes that package conflict and the
# whole transaction fail with "exists in filesystem".
$D exec "$id" sh -c 'mkdir -p /usr/share/pacman/keyrings /tmp/omarchy-keys'
$D cp "$HERE/keys/omarchy.gpg" "$id:/tmp/omarchy-keys/omarchy.gpg"
$D cp "$HERE/keys/omarchy-trusted" "$id:/tmp/omarchy-keys/omarchy-trusted"
$D cp "$HERE/keys/omarchy-revoked" "$id:/tmp/omarchy-keys/omarchy-revoked"

$D exec "$id" sh -c '
set -e
cat > /etc/pacman.conf <<"CONF"
[options]
HoldPkg = pacman glibc
Architecture = aarch64
SigLevel = Required DatabaseOptional
LocalFileSigLevel = Optional
# pacman 7 sandboxes its download step with Landlock and drops to an unprivileged
# "alpm" user. iSH stubs the whole landlock family (ENOSYS), so the sandbox must stay
# off here and on the iPad. DownloadUser is deliberately never set.
DisableSandbox
ParallelDownloads = 5

[omarchy]
Server = https://pkgs.omarchy.org/edge/$arch

[core]
Server = https://arm-mirror.omarchy.org/$arch/$repo

[extra]
Server = https://arm-mirror.omarchy.org/$arch/$repo

[alarm]
Server = https://arm-mirror.omarchy.org/$arch/$repo
CONF
pacman-key --init >/dev/null 2>&1
pacman-key --populate archlinuxarm >/dev/null 2>&1
# Populate reads /usr/share/pacman/keyrings, so stage the reviewed files, populate, then
# take them away again for omarchy-keyring to own.
cp /tmp/omarchy-keys/omarchy.gpg /tmp/omarchy-keys/omarchy-trusted /tmp/omarchy-keys/omarchy-revoked /usr/share/pacman/keyrings/
pacman-key --populate omarchy >/dev/null 2>&1
rm -f /usr/share/pacman/keyrings/omarchy.gpg /usr/share/pacman/keyrings/omarchy-trusted /usr/share/pacman/keyrings/omarchy-revoked
pacman -Sy --noconfirm --disable-sandbox >/dev/null
echo "archlinuxarm and omarchy keyrings populated"
'

say "installing the manifest (one transaction, falling back to one package at a time)"
if ! $D exec "$id" sh -c "pacman -S --needed --noconfirm --disable-sandbox --overwrite '/usr/share/pacman/keyrings/*' $packages" >"$WORK/pacman-install.log" 2>&1; then
    say "the whole transaction failed; retrying package by package (see unavailable.txt)"
    : >"$WORK/unavailable.txt"
    for p in $packages; do
        if ! $D exec "$id" sh -c "pacman -S --needed --noconfirm --disable-sandbox --overwrite '/usr/share/pacman/keyrings/*' $p" >>"$WORK/pacman-install.log" 2>&1; then
            printf '%s\n' "$p" | tee -a "$WORK/unavailable.txt"
        fi
    done
fi
# A manifest package that is still missing (usually after a provider prompt or a file
# conflict) is reported before the image is exported, not discovered on the iPad.
$D exec "$id" sh -c "for p in $packages; do pacman -Q \$p >/dev/null 2>&1 || echo \"\$p\"; done" >"$WORK/missing-packages.txt" 2>/dev/null || true
if [ -s "$WORK/missing-packages.txt" ]; then
    say "still missing:"
    cat "$WORK/missing-packages.txt"
fi

say "image essentials"
$D exec "$id" sh -c "
set -e
# PID 1 of every root the emulator boots. systemd cannot run under iSH (no cgroups,
# no /proc/self/cgroup, no unshare, no kobject uevent netlink), so the real supervisor
# replaces this placeholder in the omarchy-pad package; until then it must simply stay
# alive and reap, or the kernel treats the first exit as a shutdown.
printf '#!/bin/sh\ntrap : TERM INT\nwhile :; do sleep 3600 & wait \$!; done\n' > /sbin/init
chmod 755 /sbin/init

# Omarchy's platform detection: it reads this manifest when the root is not booted by
# systemd (bin/omarchy-hw-platform), so aarch64 is answered without PID 1 being systemd.
mkdir -p /var/lib/omarchy/image
printf 'format=1\nplatform=aarch64\n' > /var/lib/omarchy/image/target
chmod 644 /var/lib/omarchy/image/target

mkdir -p /usr/share/ish
printf '%s\n' '$ROOT_VERSION' > /usr/share/ish/rootfs-version

# The emulator creates its own device nodes, but it can only do that where the
# directories already exist: generic_mknodat does not create parents, so an image without
# /dev boots with no /dev/null and no render node at all. /dev/fd is what bash process
# substitution needs (Omarchy scripts use <(...)), and /dev/shm is where wl_shm pools and
# memfd fallbacks live.
mkdir -p /dev /dev/dri /dev/pts /dev/shm /proc /sys /tmp /run
chmod 1777 /tmp
chmod 1777 /dev/shm
ln -sfn /proc/self/fd /dev/fd
ln -sfn /proc/self/fd/0 /dev/stdin
ln -sfn /proc/self/fd/1 /dev/stdout
ln -sfn /proc/self/fd/2 /dev/stderr
"

say "installing the omarchy-pad session layer"
# Explicit parents first: the image has no /usr/local at all, and neither a tar pipe nor
# docker cp reliably creates every parent of a deep tree in a minimal container.
$D exec "$id" mkdir -p /usr/local/bin /usr/local/lib/omarchy-pad /usr/share/omarchy-pad \
    /root/.config/hypr /root/.local/state/omarchy/toggles/hypr
# COPYFILE_DISABLE/--no-xattrs keep macOS AppleDouble sidecars (._name) and xattr records
# out of the image.
COPYFILE_DISABLE=1 tar --no-xattrs -C "$ROOT/omarchy-pad/files" -cf - . |
    $D exec -i "$id" tar -xpf - -C /
$D exec "$id" sh -c '
set -e
chmod 755 /usr/local/bin/omarchy-pad-init /usr/local/bin/omarchy-pad-session /usr/local/lib/omarchy-pad/shim
# Every systemd-shaped command Omarchy calls, answered by the shim. /usr/local/bin comes
# before /usr/bin in the session PATH, so these win over the systemd package own binaries.
for c in systemctl systemd-run systemd-cat uwsm-app journalctl loginctl hostnamectl \
         timedatectl udevadm ufw bluetoothctl rfkill brightnessctl; do
    ln -sfn /usr/local/lib/omarchy-pad/shim "/usr/local/bin/$c"
done
# PID 1 is the supervisor, not systemd (which cannot run here at all).
ln -sfn /usr/local/bin/omarchy-pad-init /sbin/init

# Seed the owner configuration from the package templates. Omarchy normally does this
# while provisioning a human user; nothing on this image runs that provisioning, so the
# session would otherwise start with no ~/.config/hypr at all. -n keeps the iPad
# overrides installed by the step above.
mkdir -p /root/.config/hypr /root/.local/state/omarchy/toggles/hypr
if [ -d /usr/share/omarchy/config/hypr ]; then
    cp -rn /usr/share/omarchy/config/hypr/. /root/.config/hypr/
fi
# The emulator renders on the CPU/GPU path, so start without animations and with opaque
# windows, exactly as Omarchy does for a virtual machine.
for t in no-animations window-opaque; do
    if [ -f "/usr/share/omarchy/default/hypr/toggles/$t.lua" ]; then
        cp -n "/usr/share/omarchy/default/hypr/toggles/$t.lua" /root/.local/state/omarchy/toggles/hypr/
    fi
done

# Fail the build here, not on the iPad, if the layer did not land.
for f in /usr/local/bin/omarchy-pad-init /usr/local/bin/omarchy-pad-session \
         /usr/local/lib/omarchy-pad/shim /usr/share/omarchy-pad/hyprland-pad.conf; do
    [ -e "$f" ] || { echo "missing after install: $f" >&2; exit 1; }
done
test -x /usr/local/bin/omarchy-pad-init
test -L /sbin/init
'

say "baking local artifacts (patched compositor and guest session binaries)"
# The patched Hyprland and aquamarine packages (installation/omarchy/hyprland-pad): a
# headless output that allocates CPU-readable buffers, an EGL device from the render node,
# and a shm render target, without which no compositor can draw a frame here.
#
# Their payload is extracted over the tree rather than installed with `pacman -U`: the
# stock hyprland/aquamarine are dependencies of omarchy-dev, and a -pad package that
# replaces the same files is an unresolvable conflict. Overlaying the files keeps the
# package database coherent (`pacman -Q hyprland` still answers) at the cost of a later
# `pacman -Syu` overwriting them -- which is why the packages and this script carry the
# patches, not the image alone. They may sit under this checkout's work/ or in the
# workspace-level work/ the gate scripts used first, so both are searched.
for dir in "$ROOT/work/omarchy-pad/gate-0.4/pkgs" "$(dirname "$ROOT")/omarchy-pad/gate-0.4/pkgs"; do
    for pkg in "$dir"/*.pkg.tar.zst; do
        [ -f "$pkg" ] || continue
        base=$(basename "$pkg")
        $D cp "$pkg" "$id:/tmp/$base"
        $D exec "$id" sh -c "
            set -e
            cd /
            tar --exclude=.PKGINFO --exclude=.MTREE --exclude=.BUILDINFO -xf /tmp/$base
            rm -f /tmp/$base
            echo \"overlaid $base\"
        "
        say "overlaid $base"
    done
done

# The guest session binaries for glibc: ishwl (LinPad's compositor, which the app starts as
# ishwl-session) and omarchy-pad-bridge (which copies Hyprland's output into an ishwl
# surface). Built by installation/omarchy/build-guest-tools.sh.
tools=$ROOT/work/omarchy-pad/guest-tools/arch
if [ -d "$tools" ]; then
    $D exec "$id" mkdir -p /usr/local/bin /usr/local/lib /etc/ishwl
    for f in ishwl ishwl-x11-wm omarchy-pad-bridge; do
        if [ -f "$tools/$f" ]; then
            $D cp "$tools/$f" "$id:/usr/local/bin/$f"
        else
            say "warning: $f not built yet"
        fi
    done
    # libishwl-compat.so answers geteuid()==1000 for apps that refuse root; libishwl-scm.so
    # is deliberately NOT installed: the Arch probe showed glibc passes descriptors
    # natively, and the musl-era shim is wrong here.
    [ -f "$tools/libishwl-compat.so" ] && $D cp "$tools/libishwl-compat.so" "$id:/usr/local/lib/libishwl-compat.so"
    COPYFILE_DISABLE=1 tar --no-xattrs -C "$ROOT/wl-bridge" -cf - ishwl-session ishwl-launch ishwl-x11 apps |
        $D exec -i "$id" tar -xpf - -C /usr/local/bin
    $D exec "$id" sh -c '
        set -e
        chmod 755 /usr/local/bin/*
        if [ -f /usr/local/bin/apps ]; then mkdir -p /etc/ishwl; mv /usr/local/bin/apps /etc/ishwl/apps; fi
        # ishwl spawns commands with a PATH that does not necessarily include
        # /usr/local/bin (a spawned `pad-spawn-probe` exited 127 while the same command ran
        # from a shell), so every pad command is also linked into /usr/bin, which is always
        # searched. /usr/local/bin stays canonical.
        for c in omarchy-pad-init omarchy-pad-session omarchy-pad-bridge pad-spawn-probe ishwl ishwl-launch ishwl-session ishwl-x11 ishwl-x11-wm; do
            [ -e "/usr/local/bin/$c" ] && ln -sfn "/usr/local/bin/$c" "/usr/bin/$c"
        done
        for c in systemctl systemd-run systemd-cat uwsm-app journalctl loginctl hostnamectl timedatectl udevadm ufw bluetoothctl rfkill brightnessctl; do
            ln -sfn /usr/local/lib/omarchy-pad/shim "/usr/bin/$c"
        done
        for f in /usr/local/bin/ishwl /usr/local/bin/omarchy-pad-bridge; do
            [ -x "$f" ] || { echo "missing guest binary: $f" >&2; exit 1; }
        done
        echo "guest session binaries:"; ls -l /usr/local/bin | tail -8
    '
else
    say "warning: no guest tools built; ishwl and the bridge will be missing from the image"
fi

say "exporting the tree"
$D cp "$HERE/resolve-case-collisions.sh" "$id:/tmp/resolve-case-collisions.sh"
# The import into the iPad's case-insensitive container cannot hold two paths that differ
# only in case; Arch's tree has dozens, and the first simulator import died on the first
# pair it met. Resolve them here, on the case-sensitive build filesystem, and fail the
# build if any remain.
$D exec "$id" sh /tmp/resolve-case-collisions.sh | tee "$WORK/case-collisions-resolution.txt"
$D exec "$id" sh -c '
set -e
rm -rf /var/cache/pacman/pkg/* /tmp/* 2>/dev/null || true
# /dev belongs to the emulator, but it creates nodes, not their parents, and bash process
# substitution needs /dev/fd. Empty the directory (the container own nodes must not be
# shipped) and put back only the empty directories and the fd links.
rm -rf /dev/* 2>/dev/null || true
mkdir -p /dev /dev/dri /dev/pts /dev/shm
ln -sfn /proc/self/fd /dev/fd
ln -sfn /proc/self/fd/0 /dev/stdin
ln -sfn /proc/self/fd/1 /dev/stdout
ln -sfn /proc/self/fd/2 /dev/stderr
chmod 1777 /dev/shm
cd /
# /proc, /sys, /tmp and /run are the emulator own: it mounts its own procfs/sysfs, and a
# tar that walks the live /proc both fails on kernel files and would bake stale entries
# into the image. Their empty directory entries stay (only the contents are excluded).
# The archive is written to /tmp so it is never part of its own input.
tar -czf /tmp/rootfs.tar.gz \
    --exclude=./proc/* --exclude=./sys/* \
    --exclude=./tmp/* --exclude=./run/* .
du -sh /tmp/rootfs.tar.gz
ls /var/lib/pacman/local | wc -l
'
$D cp "$id:/tmp/rootfs.tar.gz" "$OUT"
$D exec "$id" sh -c 'pacman -Q' >"$WORK/packages-installed.txt" 2>/dev/null || true
printf '%s\n' "$ROOT_VERSION" >"${OUT%.tar.gz}.version"

say "done: $OUT ($(du -h "$OUT" | cut -f1), version $ROOT_VERSION, $(wc -l <"$WORK/packages-installed.txt") packages)"
