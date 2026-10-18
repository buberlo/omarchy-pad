#!/bin/sh
# Gate 0.5: does the systemd-less session layer actually come up?
#
# Starts omarchy-pad-init, then exercises every shim Omarchy's scripts depend on, then asks
# Omarchy's own platform detector what it thinks it is running on. This is deliberately
# separate from Hyprland: the supervisor, the buses, the daemons, the unit table and the
# shims must all be sound before a compositor is in the picture.
#
#   sh installation/omarchy/probe-session.sh <fakefs-dir> [ish-binary]
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
WORK=${WORK:-$ROOT/work/omarchy-pad}
ISH=${2:-${ISH:-$ROOT/build-arm64-release-gpu/ish}}
FS=${1:?usage: probe-session.sh FAKEFS [ish]}
OUT=$WORK/evidence/gate-0.5
mkdir -p "$OUT"

PROBE='
echo "## init"
/usr/local/bin/omarchy-pad-init >/dev/null 2>&1 &
sleep 25
tail -25 /run/omarchy-pad/init.log 2>&1
echo "## runtime dir"
ls /run/omarchy-pad 2>&1
echo "## shims"
systemctl --user import-environment PATH; echo "import-environment rc=$?"
systemctl --user daemon-reload; echo "daemon-reload rc=$?"
systemctl --user show-environment
systemctl --user is-active pipewire; echo "is-active rc=$?"
systemctl --user enable --now bt-agent.service; echo "enable --now rc=$?"
systemctl --user is-enabled bt-agent.service; echo "is-enabled rc=$?"
systemctl --user list-timers --output=json
systemctl --user show -p MainPID --value omarchy-fcitx5.service
systemd-run --user --quiet --collect --on-active=1 --unit=pad-probe /bin/true; echo "systemd-run rc=$?"
systemd-cat -t pad-probe /bin/echo systemd-cat-ok
uwsm-app /bin/echo uwsm-app-ok
journalctl -n 3; echo "journalctl rc=$?"
loginctl list-sessions; echo "loginctl rc=$?"
hostnamectl set-hostname omarchy-pad; hostnamectl | head -2
timedatectl list-timezones | head -3
timedatectl set-timezone Europe/Berlin; echo "set-timezone rc=$?"; cat /etc/timezone
udevadm control --reload; echo "udevadm rc=$?"
ufw status; echo "ufw rc=$?"
bluetoothctl show | head -3
rfkill list; echo "rfkill rc=$?"
brightnessctl 2>&1 | head -1
echo "## omarchy platform"
/usr/share/omarchy/bin/omarchy-hw-platform 2>&1 | head -3
echo "## guest identity"
cat /etc/os-release | head -4
echo "## processes"
ls /proc | grep -E "^[0-9]+$" | wc -l
echo SESSION_PROBE_DONE
'

{
    echo "== session probe $(date -u +%Y-%m-%dT%H:%M:%SZ) fs=$FS"
    "$ISH" -f "$FS" /bin/sh -lc "$PROBE"
    echo "== exit $?"
} 2>&1 | tee "$OUT/session-probe.txt"

grep -q SESSION_PROBE_DONE "$OUT/session-probe.txt" || { echo "probe did not finish (see $OUT/session-probe.txt)" >&2; exit 1; }
echo "ok: $OUT/session-probe.txt"
