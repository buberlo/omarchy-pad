#!/bin/sh
# Runs release/guest/steam-bigpicture against a fake Steam launcher, on the host, with no
# guest involved. Everything the script touches is redirected with its LINPAD_STEAM_*
# overrides, so the test never reads or writes /etc, /tmp or /usr/local.
#
#   sh installation/steam-app/tests/steam-bigpicture-test.sh
#
# It checks the contract the game-mode app depends on: which argv Steam receives, what the
# status file says while Steam runs, when the classic UI is used instead, that the fallback
# is sticky but overridable, and that the geometry is validated before anything starts.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
SCRIPT=${1:-$(cd "$HERE/../../.." && pwd)/release/guest/steam-bigpicture}
[ -f "$SCRIPT" ] || { echo "no steam-bigpicture at $SCRIPT" >&2; exit 2; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/steam-bigpicture-test.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

FAKE="$WORK/steam"
CALLS="$WORK/calls"
MODE="$WORK/steam-mode"
FALLBACK="$WORK/fallback"
STATUS="$WORK/status"

# The fake launcher records one block per call: its arguments, the geometry it was given,
# and the status file the app would be reading at that moment.
cat > "$FAKE" <<'EOF'
#!/bin/sh
{
    printf 'argv:'
    for arg in "$@"; do printf ' %s' "$arg"; done
    printf '\ngeometry: %s\n' "${STEAM_GEOMETRY:-<unset>}"
    printf 'status-at-call: %s\n' "$(cat "${LINPAD_STEAM_STATUS:-/nonexistent}" 2>/dev/null || echo '<none>')"
} >> "$CALLS"
exit "${FAKE_EXIT:-0}"
EOF
chmod 755 "$FAKE"

run() {
    # run [NAME=value ...] -- [args ...]
    envs=
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do envs="$envs $1"; shift; done
    [ "${1:-}" = "--" ] && shift
    # shellcheck disable=SC2086
    env $envs \
        LINPAD_STEAM_LAUNCHER="$FAKE" LINPAD_STEAM_MODE_FILE="$MODE" \
        LINPAD_STEAM_FALLBACK="$FALLBACK" LINPAD_STEAM_STATUS="$STATUS" CALLS="$CALLS" \
        sh "$SCRIPT" "$@" 2>"$WORK/stderr"
}

failures=0
check() {
    if [ "$2" = "$3" ]; then
        printf '  ok   %s\n' "$1"
    else
        printf '  FAIL %s\n       expected: %s\n       actual:   %s\n' "$1" "$3" "$2"
        failures=$((failures + 1))
    fi
}
check_contains() {
    case $2 in
        *"$3"*) printf '  ok   %s\n' "$1" ;;
        *) printf '  FAIL %s\n       %s\n       does not contain: %s\n' "$1" "$2" "$3"
           failures=$((failures + 1)) ;;
    esac
}
check_lacks() {
    case $2 in
        *"$3"*) printf '  FAIL %s\n       %s\n       should not contain: %s\n' "$1" "$2" "$3"
                failures=$((failures + 1)) ;;
        *) printf '  ok   %s\n' "$1" ;;
    esac
}

# FAKE_EXIT must never leak between tests: `FAKE_EXIT=3 run ...` would keep the assignment
# in this shell after the function returns, so it is only ever passed through run's env.
reset() { rm -f "$CALLS" "$FALLBACK" "$STATUS" "$MODE"; unset FAKE_EXIT; }
status_of() { cat "$STATUS" 2>/dev/null || echo "<none>"; }
call_count() { [ -f "$CALLS" ] || { echo 0; return; }; grep -c '^argv:' "$CALLS" || true; }
field() { grep "^$1:" "$CALLS" 2>/dev/null | sed -n "${2}p" | sed "s/^$1: //"; }

echo "steam-bigpicture: $SCRIPT"

echo "1. default: Big Picture, one call"
reset
run -- || true
check "one call" "$(call_count)" 1
check_contains "argv carries -gamepadui" "$(field argv 1)" "-gamepadui"
check "geometry unset" "$(field geometry 1)" "<unset>"
check "status while Steam runs" "$(field status-at-call 1)" "bigpicture"
check "status after a clean exit" "$(status_of)" "exited 0"
check "no fallback marker" "$([ -e "$FALLBACK" ] && echo yes || echo no)" "no"

echo "2. --geometry reaches the launcher as STEAM_GEOMETRY"
reset
run -- --geometry 1366x1024 >/dev/null || true
check "geometry passed through" "$(field geometry 1)" "1366x1024"
check "status names the size" "$(field status-at-call 1)" "bigpicture 1366x1024"

echo "3. a rejected Big Picture falls back to the classic UI once, and stays there"
reset
FAKE_EXIT=3 run FAKE_EXIT=3 -- >/dev/null 2>&1 || true
check "two calls" "$(call_count)" 2
check_contains "first call has -gamepadui" "$(field argv 1)" "-gamepadui"
check_lacks "second call drops -gamepadui" "$(field argv 2)" "gamepadui"
check "marker written" "$([ -e "$FALLBACK" ] && echo yes || echo no)" "yes"
check "status while the classic UI runs" "$(field status-at-call 2)" "desktop-fallback"
check "status after the classic UI exits" "$(status_of)" "desktop-fallback"
check_contains "explained on stderr" "$(cat "$WORK/stderr")" "classic UI"

echo "4. an existing marker keeps later launches on the classic UI"
reset
: > "$FALLBACK"
run -- >/dev/null || true
check "one call" "$(call_count)" 1
check_lacks "no -gamepadui" "$(field argv 1)" "gamepadui"
check "status" "$(field status-at-call 1)" "desktop-fallback"

echo "5. --bigpicture overrides the marker; --classic forces the desktop UI"
reset
: > "$FALLBACK"
run -- --bigpicture >/dev/null || true
check "marker cleared" "$([ -e "$FALLBACK" ] && echo yes || echo no)" "no"
check_contains "Big Picture attempted" "$(field argv 1)" "-gamepadui"
reset
run -- --classic >/dev/null || true
check "one call" "$(call_count)" 1
check_lacks "classic has no -gamepadui" "$(field argv 1)" "gamepadui"
check "status" "$(field status-at-call 1)" "desktop"

echo "6. /etc/linpad/steam-mode = desktop pins the classic UI"
reset
printf 'desktop\n' > "$MODE"
run -- >/dev/null || true
check "status" "$(field status-at-call 1)" "desktop"
check_lacks "no -gamepadui" "$(field argv 1)" "gamepadui"

echo "7. Steam arguments after the options are forwarded"
reset
run -- --classic -console -dev >/dev/null || true
check_contains "forwards -console" "$(field argv 1)" "-console"
check_contains "forwards -dev" "$(field argv 1)" "-dev"

echo "8. bad geometry is refused before Steam starts; --reset clears the marker"
reset
run -- --geometry 1366 >/dev/null 2>&1 || true
check "no call for a size without x" "$(call_count)" 0
run -- --geometry 99999x99999 >/dev/null 2>&1 || true
check "no call for an out-of-range size" "$(call_count)" 0
: > "$FALLBACK"
run -- --reset >/dev/null || true
check "marker cleared by --reset" "$([ -e "$FALLBACK" ] && echo yes || echo no)" "no"

echo "9. a missing launcher reports itself instead of failing silently"
reset
env LINPAD_STEAM_LAUNCHER="$WORK/absent" LINPAD_STEAM_MODE_FILE="$MODE" \
    LINPAD_STEAM_FALLBACK="$FALLBACK" LINPAD_STEAM_STATUS="$STATUS" \
    sh "$SCRIPT" >/dev/null 2>&1 || true
check "status" "$(status_of)" "missing"

echo "10. steam-local keeps the verified default and honours STEAM_GEOMETRY"
LOCAL=$(cd "$HERE/../../.." && pwd)/release/guest/steam-local
if [ -f "$LOCAL" ]; then
    check_contains "default geometry" "$(grep 'STEAM_GEOMETRY:-' "$LOCAL")" "1024x700"
    check_contains "Xvnc uses the variable" "$(grep 'geometry "\$GEOMETRY"' "$LOCAL")" '-geometry "$GEOMETRY"'
    check_contains "viewer uses the variable" "$(grep 'ishwl-x11 -g "\$GEOMETRY"' "$LOCAL")" 'ishwl-x11 -g "$GEOMETRY"'
    check "no hard-coded size left" "$(grep -c -- '-geometry 1024x700' "$LOCAL" || true)" 0
else
    echo "  ..   no steam-local to check at $LOCAL"
fi

echo
if [ "$failures" -eq 0 ]; then
    echo "steam-bigpicture: all checks passed"
else
    echo "steam-bigpicture: $failures check(s) failed"
    exit 1
fi
