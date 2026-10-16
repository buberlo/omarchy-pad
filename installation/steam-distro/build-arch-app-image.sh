#!/bin/sh
# Build the Arch Linux ARM root that LinPad Steam boots, ready for the app to *run* (not just to host a
# CLI test): launcher, session layer, GPU hook, init, Steam client, compatibility layer, stamps.
#
#   sh installation/steam-distro/build-arch-app-image.sh [output.tar.gz]
#
# Inputs it expects to exist (all produced earlier by this effort and committed):
#   work/steam-distro/guest-tools/arch/{ishwl,ishwl-x11-wm,libishwl-compat.so}   glibc session layer
#   work/steam-distro/compat/usr/lib/aarch64-linux-gnu/                          the 524-file compat layer
#   work/steam/serve/steam-client.tar.gz                                         Valve's ARM64 client
#   release/guest/{steam-bigpicture,steam-local,steam.desktop,steam.svg}         the app's guest files
#   wl-bridge/{ishwl-session,ishwl-launch,ishwl-x11,apps}                        session plumbing
#
# Everything runs in a throwaway `menci/archlinuxarm` container (docker context below); the result is a
# gzipped tar whose FIRST member is "./" -- without that the kernel's chdir("/") fails with ENOTDIR and
# pacman reports ALPM_ERR_NOT_A_DIR (Round 10). /dev, /proc and /sys are emptied before tarring, or the
# image's own device nodes shadow the kernel's and PTYs stop working (Round 12).
#
# Deliberately NOT installed: libishwl-scm.so (glibc passes descriptors natively -- Round 19) and
# steam.cfg's BootStrapperInhibitAll (a Mac-harness workaround; the device must be free to bootstrap,
# which is how CEF arrives).
#
# Then, to ship it:
#   ROOTFS_ARCHIVE=$PWD/<output.tar.gz> sh installation/steam-app/build-local.sh steam
#   cp <root.version>          "work/performance/steam-app-steam/Payload/LinPad Steam.app/root.version"
#   python3 installation/steam-app/sign-variant.py --variant steam --app "…/Payload/LinPad Steam.app"
#   python3 installation/steam-app/cut-ipa.py --variant steam
set -e

DOCKER_CONTEXT=${DOCKER_CONTEXT:-colima-linpad-fex}
BASE_IMAGE=${BASE_IMAGE:-menci/archlinuxarm:latest}
HERE=$(cd "$(dirname "$0")/../.." && pwd)          # the repository root
ROOT_VERSION=${ROOT_VERSION:-$(date -u +%Y%m%d%H%M)}
OUT=${1:-$HERE/work/steam-distro/arch-app.tar.gz}

GUEST_TOOLS=$HERE/work/steam-distro/guest-tools/arch
COMPAT=$HERE/work/steam-distro/compat/usr/lib/aarch64-linux-gnu
CLIENT=$HERE/work/steam/serve/steam-client.tar.gz
GUEST=$HERE/release/guest
BRIDGE=$HERE/wl-bridge

# The Steam client still needs an FFmpeg 8 vintage Arch has moved past, plus GTK2, which Arch dropped.
# Those libraries live in the compat layer and are placed on the launcher's LD_LIBRARY_PATH.
COMPAT_LIBDIR=/opt/steam-compat/usr/lib/aarch64-linux-gnu
STEAM_HOME=/root/.local/share/Steam

PACKAGES="sdl3 libglvnd libx11 libxext libxrender libxrandr libxfixes libxtst libxi libxcursor \
libxinerama libxcomposite libxdamage libxss libxkbcommon wayland mesa zlib fontconfig ttf-dejavu \
tigervnc systemd-libs libnm libibus nss libcups alsa-lib libpulse dbus xkeyboard-config xorg-xwininfo \
xorg-xwd lsof ca-certificates gdk-pixbuf2 gtk3"

die() { echo "build-arch-app-image: $*" >&2; exit 1; }

for f in "$GUEST_TOOLS/ishwl" "$GUEST_TOOLS/ishwl-x11-wm" "$GUEST_TOOLS/libishwl-compat.so" \
         "$CLIENT" "$GUEST/steam-bigpicture" "$GUEST/steam-local" "$GUEST/steam.desktop" "$GUEST/steam.svg" \
         "$BRIDGE/ishwl-session" "$BRIDGE/ishwl-launch" "$BRIDGE/ishwl-x11" "$BRIDGE/apps"; do
    [ -e "$f" ] || die "missing input: $f"
done
[ -d "$COMPAT" ] || die "missing the compatibility layer: $COMPAT (see PHASE0-STATUS.md, Rounds 50-52)"

echo "building $OUT from $BASE_IMAGE (root version $ROOT_VERSION)"
id=$(docker --context "$DOCKER_CONTEXT" create "$BASE_IMAGE" sh -c 'while :; do sleep 3600; done')
# shellcheck disable=SC2064
trap "docker --context '$DOCKER_CONTEXT' rm -f '$id' >/dev/null 2>&1 || true" EXIT INT TERM
docker --context "$DOCKER_CONTEXT" start "$id" >/dev/null

# pacman's sandbox user cannot start under this container's seccomp profile; disable it rather than
# fight it. The same applies in the guest, where Landlock is stubbed.
docker --context "$DOCKER_CONTEXT" exec "$id" sh -c "
    sed -i 's/^DownloadUser/#DownloadUser/' /etc/pacman.conf
    sed -i '/^\[options\]/a DisableSandbox' /etc/pacman.conf
    pacman -Sy --noconfirm --disable-sandbox >/dev/null"

# One package per transaction: a single unknown name aborts the whole of `pacman -S a b c` (Round 43),
# and each failure is reported here by name.
fails=
for p in $PACKAGES; do
    if ! docker --context "$DOCKER_CONTEXT" exec "$id" \
            pacman -S --noconfirm --needed --disable-sandbox "$p" >/dev/null 2>&1; then
        fails="$fails $p"
    fi
done
[ -z "$fails" ] || echo "warning: packages that did not install:$fails" >&2

docker --context "$DOCKER_CONTEXT" exec "$id" sh -c "
    mkdir -p /usr/local/bin /usr/local/lib /etc/ishwl /etc/linpad /etc/profile.d \
             /usr/share/applications /usr/share/icons/hicolor/scalable/apps /usr/share/ish \
             $COMPAT_LIBDIR $STEAM_HOME /root/.steam
    # GPU defaults: only meaningful when the guest actually has a DRM node, so the hook exits early
    # otherwise and nothing depends on it being present.
    cat > /etc/profile.d/gpu.sh <<'EOF'
#!/bin/sh
[ -c /dev/dri/renderD128 ] || exit 0
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/virtio_icd.aarch64.json
export MESA_LOADER_DRIVER_OVERRIDE=zink
export MESA_VK_WSI_DEBUG=sw
EOF
    chmod 755 /etc/profile.d/gpu.sh
    # The app talks to this; /sbin/init is required of any root this kernel boots.
    printf 'bigpicture\n' > /etc/linpad/steam-mode
    printf '#!/bin/sh\ntrap : TERM INT\nwhile :; do sleep 3600 & wait \$!; done\n' > /sbin/init
    chmod 755 /sbin/init
    printf '%s\n' '$ROOT_VERSION' > /usr/share/ish/rootfs-version"

cp_to() { docker --context "$DOCKER_CONTEXT" cp "$1" "$id:$2"; }
cp_to "$GUEST_TOOLS/ishwl"                        /usr/local/bin/ishwl
cp_to "$GUEST_TOOLS/ishwl-x11-wm"                 /usr/local/bin/ishwl-x11-wm
cp_to "$GUEST_TOOLS/libishwl-compat.so"           /usr/local/lib/libishwl-compat.so
cp_to "$BRIDGE/ishwl-session"                     /usr/local/bin/ishwl-session
cp_to "$BRIDGE/ishwl-launch"                      /usr/local/bin/ishwl-launch
cp_to "$BRIDGE/ishwl-x11"                         /usr/local/bin/ishwl-x11
cp_to "$BRIDGE/apps"                              /etc/ishwl/apps
cp_to "$GUEST/steam-bigpicture"                   /usr/local/bin/steam-bigpicture
cp_to "$GUEST/steam-local"                        /usr/local/bin/steam-local
cp_to "$GUEST/steam.desktop"                      /usr/share/applications/steam.desktop
cp_to "$GUEST/steam.svg"                          /usr/share/icons/hicolor/scalable/apps/steam.svg
# "SRC/." + a trailing slash on the destination: without the dot docker cp nests the directory inside
# itself and none of the libraries are found (Round 53).
cp_to "$COMPAT/."                                 "$COMPAT_LIBDIR/"
cp_to "$CLIENT"                                   /tmp/steam-client.tar.gz

docker --context "$DOCKER_CONTEXT" exec "$id" sh -c "
    set -e
    chmod 755 /usr/local/bin/* /usr/local/lib/*.so
    tar -xzf /tmp/steam-client.tar.gz -C $STEAM_HOME && rm -f /tmp/steam-client.tar.gz
    # Steam refuses to start without its data link to the client tree.
    ln -sfn $STEAM_HOME /root/.steam/steam
    ln -sfn $STEAM_HOME /root/.steam/root
    LP=$STEAM_HOME/steamrtarm64:$STEAM_HOME/steamrtarm64/libs:$COMPAT_LIBDIR
    missing=\$(LD_LIBRARY_PATH=\$LP ldd $STEAM_HOME/steamrtarm64/steamui.so 2>/dev/null | grep -c 'not found' || true)
    echo \"compat files: \$(ls $COMPAT_LIBDIR | wc -l), steamui.so missing: \$missing\"
    [ \"\$missing\" = 0 ] || { echo 'the compatibility layer is incomplete; see PHASE0-STATUS.md Rounds 50-52' >&2; exit 1; }
    rm -rf /dev/* /proc/* /sys/* 2>/dev/null || true
    mkdir -p /dev /proc /sys
    cd /
    tar -czf /rootfs.tar.gz . 2>/dev/null || true
    du -sh /rootfs.tar.gz"

mkdir -p "$(dirname "$OUT")"
docker --context "$DOCKER_CONTEXT" cp "$id:/rootfs.tar.gz" "$OUT"
printf '%s\n' "$ROOT_VERSION" > "$(dirname "$OUT")/root.version"

first=$(tar -tzf "$OUT" | head -1)
[ "$first" = "./" ] || { echo "build-arch-app-image: first tar member is '$first', not './' -- chdir('/') will fail" >&2; exit 1; }
echo "wrote $OUT ($(wc -c < "$OUT" | tr -d ' ') bytes), root.version $ROOT_VERSION"
