#!/bin/sh
# Changes only the dedicated helper binary and the one missing SDK entry.
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sdk=${LINPAD_STEAM_SDK_DIR:-$HOME/.steam/sdkarm64}
helpers=${LINPAD_STEAM_HELPER_DIR:-/opt/steam-host/libexec/linpad-steam}
for path in "$sdk" "$helpers"; do
    case "$path" in /*) ;; *) printf '%s\n' 'Helper paths must be absolute.' >&2; exit 1 ;; esac
done
[ -f "$base/linuxarm64/steam-launch-wrapper" ]
sh -n "$base/steam-launch-wrapper"
mkdir -p "$sdk" "$helpers"
place() {
    source=$1; target=$2
    [ ! -L "$target" ] || { printf 'Refusing helper symlink: %s\n' "$target" >&2; exit 1; }
    if [ -e "$target" ]; then
        [ -f "$target" ] || exit 1
        if cmp -s "$source" "$target" && [ -x "$target" ]; then return; fi
        backup="$target.before-linpad-$(date +%Y%m%dT%H%M%S)-$$"
        cp -p "$target" "$backup"
        printf 'Previous file saved: %s\n' "$backup"
    fi
    cp "$source" "$target.linpad-new-$$"
    chmod 755 "$target.linpad-new-$$"
    mv "$target.linpad-new-$$" "$target"
    printf 'Installed: %s\n' "$target"
}
place "$base/linuxarm64/steam-launch-wrapper" "$helpers/steam-launch-wrapper.real"
place "$base/steam-launch-wrapper" "$sdk/steam-launch-wrapper"
printf '%s\n' 'Installed the SDK launch helper only; Steam was not restarted.'
