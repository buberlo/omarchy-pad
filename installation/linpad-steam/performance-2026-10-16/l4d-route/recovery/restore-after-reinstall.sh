#!/bin/sh
# Run INSIDE the guest after a fresh LinPad install, with this archive's files copied to /root/recovery.
# It puts back everything that does not have to be downloaded. Steam and Left 4 Dead must be installed
# first (see README.md); this script only restores the helpers and the launch chain.
set -eu
B=${LINPAD_STEAM_ROOT:-/root/.local/share/Steam}
R=${LINPAD_RECOVERY_DIR:-/root/recovery}
L=/opt/steam-host/usr/lib/aarch64-linux-gnu

[ -d "$B" ] || { echo "Steam is not installed under $B -- install the client first" >&2; exit 80; }

echo "== helpers"
for f in steam steam-local ishwl-x11 l4d-play linpad-proton-arm64; do
    [ -f "$R/bin/$f" ] || { echo "  missing $R/bin/$f" >&2; continue; }
    cp -f "$R/bin/$f" /usr/local/bin/"$f"; chmod 755 /usr/local/bin/"$f"; echo "  installed /usr/local/bin/$f"
done

echo "== steamwebhelper wrapper (the client spawns it by this path; it is a glibc program)"
rm -f "$HOME/steamwebhelper"
cat > "$HOME/steamwebhelper" <<EOF
#!/bin/sh
L=$L
B=$B
exec "\$L/ld-linux-aarch64.so.1" --argv0 "\$B/steamrtarm64/steamwebhelper" \\
    --library-path "\$L:\$L/pulseaudio:\$B/steamrtarm64:\$B/steamrtarm64/libs" \\
    "\$B/steamrtarm64/steamwebhelper" "\$@"
EOF
chmod 755 "$HOME/steamwebhelper"

echo "== launch chain: reaper stub (must be written AFTER the client starts, it restores the ELF then)"
R2="$B/steamrtarm64/reaper"
if [ -f "$R2" ] && [ ! -f "$R2.linpad-original" ]; then cp "$R2" "$R2.linpad-original"; fi
cat > "$R2" <<'EOF'
#!/bin/sh
set -eu
while [ $# -gt 0 ] && [ "$1" != "--" ]; do shift; done
[ "${1:-}" = "--" ] && shift
[ "$#" -gt 0 ] || exit 64
exec "$@"
EOF
chmod 755 "$R2"

echo "== platform override for app 500 (Steam only uses a compat tool without it)"
for f in "$B"/userdata/*/config/compat.vdf; do
    [ -f "$f" ] || continue
    grep -q '"500"' "$f" 2>/dev/null || continue
    cp -n "$f" "$f.linpad-backup" 2>/dev/null || true
    python3 - "$f" <<'PY'
import re, sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text()
p.write_text(re.sub(r'\n\t"500"\n\t\{[^}]*\}\n', '\n', s))
print("  cleaned", p)
PY
done

echo "== done. Start with: l4d-play -g   (direct Wine) or l4d-play (full Steam route)"
